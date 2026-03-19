#include "catalog/table_info.h"
#include "common/config.h"
#include "runtime/database_runtime.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page.h"
#include "storage/page/table_page.h"
#include "storage/record/tuple_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

using namespace HaruhiDB;

constexpr uint32_t kBPlusTreeMetaMagic = 0x42505448U;
constexpr uint32_t kBPlusTreeMetaVersion = 1U;

struct CatalogMetaPageOpaqueView
{
    uint32_t magic;
    uint32_t version;
    page_id_t next_page_id;
    uint32_t payload_size;
};

struct BPlusTreeMetaOpaqueView
{
    uint32_t magic;
    uint32_t version;
    page_id_t root_page_id;
    uint32_t reserved;
};

static_assert(sizeof(CatalogMetaPageOpaqueView) == storage::PAGE_HEADER_OPAQUE_SIZE);
static_assert(sizeof(BPlusTreeMetaOpaqueView) == storage::PAGE_HEADER_OPAQUE_SIZE);

struct DumpState
{
    std::unordered_map<page_id_t, std::vector<std::string>> page_labels;
    std::unordered_map<page_id_t, const catalog::Schema*> heap_schemas;
    std::unordered_map<page_id_t, std::string> heap_owner_names;
    std::unordered_set<page_id_t> free_list_pages;
    std::unordered_set<page_id_t> catalog_meta_pages;
    std::vector<std::string> warnings;
};

void AddLabel(DumpState* state, page_id_t page_id, std::string label)
{
    if (state == nullptr) {
        return;
    }
    state->page_labels[page_id].push_back(std::move(label));
}

void AddWarning(DumpState* state, std::string warning)
{
    if (state == nullptr) {
        return;
    }
    state->warnings.push_back(std::move(warning));
}

std::string Join(const std::vector<std::string>& parts, std::string_view separator)
{
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out << separator;
        }
        out << parts[i];
    }
    return out.str();
}

std::string FormatPageId(page_id_t page_id)
{
    return page_id == INVALID_PAGE_ID ? "INVALID" : std::to_string(page_id);
}

std::string PageTypeToString(storage::PageType page_type)
{
    switch (page_type) {
        case storage::PageType::INVALID:
            return "INVALID";
        case storage::PageType::HEAP:
            return "HEAP";
        case storage::PageType::INTERNAL:
            return "INTERNAL";
        case storage::PageType::LEAF:
            return "LEAF";
        case storage::PageType::HEADER:
            return "HEADER";
        case storage::PageType::FREELIST:
            return "FREELIST";
    }
    return "UNKNOWN";
}

std::string HexBytes(std::span<const std::byte> bytes, size_t max_bytes = 32)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');

    const size_t limit = std::min(bytes.size(), max_bytes);
    for (size_t i = 0; i < limit; ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << std::setw(2) << static_cast<unsigned int>(std::to_integer<unsigned char>(bytes[i]));
    }
    if (bytes.size() > max_bytes) {
        out << " ...";
    }
    return out.str();
}

std::string ValuesToString(const std::vector<type::Value>& values)
{
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << values[i].ToString();
    }
    out << ']';
    return out.str();
}

std::string RidToString(const record::RID& rid)
{
    std::ostringstream out;
    out << "RID(" << rid.GetPageId() << ", " << rid.GetSlotId() << ')';
    return out.str();
}

bool ReadRawPage(storage::DiskManager* disk_manager, page_id_t page_id, page_data_t* out, std::string* error)
{
    if (disk_manager == nullptr || out == nullptr) {
        if (error != nullptr) {
            *error = "disk manager or output buffer is null";
        }
        return false;
    }

    auto read_exp = disk_manager->ReadPage(page_id, *out);
    if (!read_exp.has_value()) {
        if (error != nullptr) {
            *error = read_exp.error().msg;
        }
        return false;
    }
    return true;
}

CatalogMetaPageOpaqueView ReadCatalogMetaOpaque(const storage::PersistentHeader* persistent)
{
    CatalogMetaPageOpaqueView opaque{};
    if (persistent != nullptr) {
        std::memcpy(&opaque, persistent->opaque, sizeof(opaque));
    }
    return opaque;
}

BPlusTreeMetaOpaqueView ReadBPlusTreeMetaOpaque(const storage::PersistentHeader* persistent)
{
    BPlusTreeMetaOpaqueView opaque{};
    if (persistent != nullptr) {
        std::memcpy(&opaque, persistent->opaque, sizeof(opaque));
    }
    return opaque;
}

std::vector<page_id_t> CollectFreeListPages(
    storage::DiskManager* disk_manager,
    const storage::DBHeader& db_header,
    page_id_t page_count,
    DumpState* state)
{
    std::vector<page_id_t> chain;
    std::unordered_set<page_id_t> visited;

    page_id_t current = db_header.free_list_head;
    while (current != INVALID_PAGE_ID) {
        if (current <= 0 || current >= page_count) {
            AddWarning(state, "free list points to out-of-range page " + std::to_string(current));
            break;
        }
        if (!visited.insert(current).second) {
            AddWarning(state, "free list cycle detected at page " + std::to_string(current));
            break;
        }

        chain.push_back(current);
        state->free_list_pages.insert(current);
        AddLabel(state, current, "free-list node");

        page_data_t raw{};
        std::string error;
        if (!ReadRawPage(disk_manager, current, &raw, &error)) {
            AddWarning(state, "failed to read free-list page " + std::to_string(current) + ": " + error);
            break;
        }

        uint64_t next_raw = 0;
        std::memcpy(&next_raw, raw.data(), sizeof(next_raw));
        if (next_raw == static_cast<uint64_t>(INVALID_PAGE_ID)) {
            current = INVALID_PAGE_ID;
            continue;
        }
        if (next_raw >= static_cast<uint64_t>(page_count)) {
            AddWarning(
                state,
                "free list next pointer from page " + std::to_string(current) +
                    " is out of range: " + std::to_string(next_raw));
            break;
        }
        current = static_cast<page_id_t>(next_raw);
    }

    return chain;
}

std::vector<page_id_t> CollectCatalogMetaPages(
    storage::DiskManager* disk_manager,
    const storage::DBHeader& db_header,
    page_id_t page_count,
    DumpState* state)
{
    std::vector<page_id_t> chain;
    std::unordered_set<page_id_t> visited;

    page_id_t current = db_header.catalog_meta_page_id;
    while (current != INVALID_PAGE_ID) {
        if (current <= 0 || current >= page_count) {
            AddWarning(state, "catalog meta chain points to out-of-range page " + std::to_string(current));
            break;
        }
        if (!visited.insert(current).second) {
            AddWarning(state, "catalog meta chain cycle detected at page " + std::to_string(current));
            break;
        }

        page_data_t raw{};
        std::string error;
        if (!ReadRawPage(disk_manager, current, &raw, &error)) {
            AddWarning(state, "failed to read catalog meta page " + std::to_string(current) + ": " + error);
            break;
        }

        storage::Page page;
        page.Data() = raw;
        const auto* persistent = page.Header();
        if (persistent->page_type != storage::PageType::HEADER) {
            AddWarning(
                state,
                "catalog meta page " + std::to_string(current) +
                    " is not marked as HEADER (actual=" + PageTypeToString(persistent->page_type) + ')');
            break;
        }

        const auto opaque = ReadCatalogMetaOpaque(persistent);
        if (opaque.magic != CATALOG_META_PAGE_MAGIC || opaque.version != CATALOG_META_PAGE_VERSION) {
            AddWarning(
                state,
                "catalog meta page " + std::to_string(current) + " has unexpected magic/version");
            break;
        }

        chain.push_back(current);
        state->catalog_meta_pages.insert(current);
        AddLabel(state, current, "catalog-meta page");

        if (opaque.next_page_id == 0) {
            AddWarning(state, "catalog meta page " + std::to_string(current) + " points to page 0");
            break;
        }
        current = opaque.next_page_id;
    }

    return chain;
}

void CollectTableHeapPages(
    const catalog::TableInfo* table_info,
    storage::DiskManager* disk_manager,
    page_id_t page_count,
    DumpState* state)
{
    if (table_info == nullptr || table_info->GetTableHeap() == nullptr) {
        return;
    }

    page_id_t current = table_info->GetTableHeap()->FirstPageId();
    size_t depth = 0;
    std::unordered_set<page_id_t> visited;

    while (current != INVALID_PAGE_ID) {
        if (current <= 0 || current >= page_count) {
            AddWarning(
                state,
                "table '" + table_info->Name() + "' heap chain points to out-of-range page " +
                    std::to_string(current));
            break;
        }
        if (!visited.insert(current).second) {
            AddWarning(
                state,
                "table '" + table_info->Name() + "' heap chain cycle detected at page " +
                    std::to_string(current));
            break;
        }

        AddLabel(
            state,
            current,
            "table '" + table_info->Name() + "' heap page #" + std::to_string(depth));
        state->heap_schemas[current] = &table_info->GetSchema();
        state->heap_owner_names[current] = table_info->Name();

        page_data_t raw{};
        std::string error;
        if (!ReadRawPage(disk_manager, current, &raw, &error)) {
            AddWarning(
                state,
                "failed to read heap page " + std::to_string(current) + " for table '" +
                    table_info->Name() + "': " + error);
            break;
        }

        storage::Page page;
        page.Data() = raw;
        const auto* persistent = page.Header();
        if (persistent->page_type != storage::PageType::HEAP) {
            AddWarning(
                state,
                "table '" + table_info->Name() + "' heap page " + std::to_string(current) +
                    " is not marked as HEAP (actual=" + PageTypeToString(persistent->page_type) + ')');
            break;
        }

        storage::TablePage table_page(&page);
        current = table_page.NextPageId();
        ++depth;
    }
}

void CollectIndexTreePages(
    const catalog::TableInfo* table_info,
    const catalog::TableInfo::IndexEntry& index_entry,
    storage::DiskManager* disk_manager,
    page_id_t page_count,
    DumpState* state)
{
    const std::string owner_prefix =
        "index '" + table_info->Name() + "." + index_entry.index_name + "'";

    AddLabel(state, index_entry.header_page_id, owner_prefix + " header page");

    if (index_entry.index == nullptr) {
        AddWarning(state, owner_prefix + " has no runtime index object");
        return;
    }

    const page_id_t root_page_id = index_entry.index->RootPageId();
    if (root_page_id == INVALID_PAGE_ID) {
        AddLabel(state, index_entry.header_page_id, owner_prefix + " root=INVALID (empty tree)");
        return;
    }

    std::unordered_set<page_id_t> visited;
    std::function<void(page_id_t, size_t)> dfs = [&](page_id_t page_id, size_t depth) {
        if (page_id == INVALID_PAGE_ID) {
            return;
        }
        if (page_id <= 0 || page_id >= page_count) {
            AddWarning(
                state,
                owner_prefix + " tree points to out-of-range page " + std::to_string(page_id));
            return;
        }
        if (!visited.insert(page_id).second) {
            return;
        }

        AddLabel(
            state,
            page_id,
            owner_prefix + (page_id == root_page_id ? " root" : " node") +
                " depth=" + std::to_string(depth));

        page_data_t raw{};
        std::string error;
        if (!ReadRawPage(disk_manager, page_id, &raw, &error)) {
            AddWarning(
                state,
                "failed to read index page " + std::to_string(page_id) + " for " + owner_prefix +
                    ": " + error);
            return;
        }

        storage::Page page;
        page.Data() = raw;
        const auto* persistent = page.Header();
        if (persistent->page_type == storage::PageType::LEAF) {
            return;
        }
        if (persistent->page_type != storage::PageType::INTERNAL) {
            AddWarning(
                state,
                owner_prefix + " page " + std::to_string(page_id) +
                    " is neither LEAF nor INTERNAL (actual=" +
                    PageTypeToString(persistent->page_type) + ')');
            return;
        }

        storage::BPlusTreeInternalPage internal_page(&page);
        dfs(internal_page.GetLeftMostChild(), depth + 1);
        for (uint16_t i = 0; i < internal_page.GetSize(); ++i) {
            dfs(internal_page.ChildAt(i), depth + 1);
        }
    };

    dfs(root_page_id, 0);
}

std::unique_ptr<runtime::DatabaseRuntime> TryOpenRuntime(std::filesystem::path db_path, std::string* error)
{
    auto runtime_exp = runtime::DatabaseRuntime::Open(
        db_path,
        runtime::DatabaseOpenOptions{
            .buffer_pool_size = 64,
            .lru_k = 2,
            .enable_wal = false,
            .wal_path = std::nullopt,
        });

    if (!runtime_exp.has_value()) {
        if (error != nullptr) {
            *error = runtime_exp.error();
        }
        return nullptr;
    }

    return std::make_unique<runtime::DatabaseRuntime>(std::move(runtime_exp.value()));
}

void PrintCatalogSummary(const catalog::Catalog* catalog)
{
    if (catalog == nullptr) {
        std::cout << "catalog summary: unavailable\n";
        return;
    }

    auto tables = catalog->GetAllTables();
    std::sort(tables.begin(), tables.end(), [](const catalog::TableInfo* lhs, const catalog::TableInfo* rhs) {
        if (lhs == nullptr || rhs == nullptr) {
            return lhs < rhs;
        }
        return lhs->Oid() < rhs->Oid();
    });

    std::cout << "catalog summary: tables=" << tables.size() << "\n";
    for (const auto* table_info : tables) {
        if (table_info == nullptr) {
            continue;
        }
        const page_id_t first_page_id = table_info->GetTableHeap() == nullptr
            ? INVALID_PAGE_ID
            : table_info->GetTableHeap()->FirstPageId();
        std::cout << "  table oid=" << table_info->Oid()
                  << " name='" << table_info->Name() << "'"
                  << " first_page_id=" << FormatPageId(first_page_id)
                  << " schema=" << table_info->GetSchema().ToString() << "\n";

        for (const auto& index_entry : table_info->IndexEntries()) {
            const page_id_t root_page_id = index_entry.index == nullptr
                ? INVALID_PAGE_ID
                : index_entry.index->RootPageId();
            std::cout << "    index oid=" << index_entry.index_oid
                      << " name='" << index_entry.index_name << "'"
                      << " header_page_id=" << FormatPageId(index_entry.header_page_id)
                      << " root_page_id=" << FormatPageId(root_page_id) << "\n";
        }
    }
}

void AnnotateFromCatalog(
    const catalog::Catalog* catalog,
    storage::DiskManager* disk_manager,
    page_id_t page_count,
    DumpState* state)
{
    if (catalog == nullptr || disk_manager == nullptr || state == nullptr) {
        return;
    }

    auto tables = catalog->GetAllTables();
    for (const auto* table_info : tables) {
        if (table_info == nullptr) {
            continue;
        }
        CollectTableHeapPages(table_info, disk_manager, page_count, state);
        for (const auto& index_entry : table_info->IndexEntries()) {
            CollectIndexTreePages(table_info, index_entry, disk_manager, page_count, state);
        }
    }
}

void PrintPageBanner(page_id_t page_id, const DumpState& state)
{
    std::cout << "\n===== Page " << page_id << " =====\n";
    auto it = state.page_labels.find(page_id);
    if (it != state.page_labels.end() && !it->second.empty()) {
        std::cout << "labels: " << Join(it->second, ", ") << "\n";
    }
}

void DumpDbHeader(const storage::DBHeader& db_header)
{
    std::cout << "kind: DBHeader\n";
    std::cout << "magic_number: " << db_header.magic_number << "\n";
    std::cout << "version: " << db_header.version << "\n";
    std::cout << "next_page_id: " << FormatPageId(db_header.next_page_id) << "\n";
    std::cout << "free_list_head: " << FormatPageId(db_header.free_list_head) << "\n";
    std::cout << "catalog_meta_page_id: " << FormatPageId(db_header.catalog_meta_page_id) << "\n";
}

void DumpFreeListPage(page_id_t page_id, const page_data_t& raw)
{
    (void)page_id;
    uint64_t next_raw = 0;
    std::memcpy(&next_raw, raw.data(), sizeof(next_raw));

    std::cout << "kind: free-list node\n";
    if (next_raw == static_cast<uint64_t>(INVALID_PAGE_ID)) {
        std::cout << "next_free_page_id: INVALID\n";
    } else {
        std::cout << "next_free_page_id: " << next_raw << "\n";
    }
    std::cout << "raw_preview: " << HexBytes(std::span<const std::byte>(raw.data(), 64), 64) << "\n";
}

void DumpCatalogMetaPage(const storage::Page& page)
{
    const auto* persistent = page.Header();
    const auto opaque = ReadCatalogMetaOpaque(persistent);
    const std::byte* payload = page.RawData() + HEADER_SIZE;
    const size_t max_payload = PAGE_SIZE - HEADER_SIZE;
    const size_t payload_size = std::min(max_payload, static_cast<size_t>(opaque.payload_size));

    std::cout << "kind: catalog meta page\n";
    std::cout << "persistent.page_id: " << persistent->page_id << "\n";
    std::cout << "persistent.lsn: " << persistent->lsn << "\n";
    std::cout << "opaque.magic: " << opaque.magic << "\n";
    std::cout << "opaque.version: " << opaque.version << "\n";
    std::cout << "opaque.next_page_id: " << FormatPageId(opaque.next_page_id) << "\n";
    std::cout << "opaque.payload_size: " << opaque.payload_size << "\n";
    std::cout << "payload_preview: " << HexBytes(std::span<const std::byte>(payload, payload_size), 96) << "\n";
}

void DumpBPlusTreeHeaderPage(const storage::Page& page)
{
    const auto* persistent = page.Header();
    const auto opaque = ReadBPlusTreeMetaOpaque(persistent);

    std::cout << "kind: B+Tree header page\n";
    std::cout << "persistent.page_id: " << persistent->page_id << "\n";
    std::cout << "persistent.lsn: " << persistent->lsn << "\n";
    std::cout << "opaque.magic: " << opaque.magic << "\n";
    std::cout << "opaque.version: " << opaque.version << "\n";
    std::cout << "opaque.root_page_id: " << FormatPageId(opaque.root_page_id) << "\n";
}

void DumpUnknownHeaderPage(const storage::Page& page)
{
    const auto* persistent = page.Header();
    std::cout << "kind: unknown HEADER page\n";
    std::cout << "persistent.page_id: " << persistent->page_id << "\n";
    std::cout << "persistent.lsn: " << persistent->lsn << "\n";
    std::cout << "opaque_preview: "
              << HexBytes(
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(persistent->opaque),
                         storage::PAGE_HEADER_OPAQUE_SIZE),
                     16)
              << "\n";
    std::cout << "body_preview: "
              << HexBytes(
                     std::span<const std::byte>(page.RawData() + HEADER_SIZE, PAGE_SIZE - HEADER_SIZE),
                     64)
              << "\n";
}

void DumpHeapPage(page_id_t page_id, storage::Page& page, const DumpState& state)
{
    storage::TablePage table_page(&page);
    const auto* header = table_page.HeaderData();

    std::cout << "kind: HEAP page\n";
    std::cout << "persistent.page_id: " << page.Header()->page_id << "\n";
    std::cout << "persistent.lsn: " << page.Header()->lsn << "\n";
    std::cout << "next_page_id: " << FormatPageId(header->next_page_id) << "\n";
    std::cout << "slot_count: " << header->slot_count << "\n";
    std::cout << "alive_tuple_count: " << header->alive_tuple_count << "\n";
    std::cout << "deleted_tuple_count: " << header->deleted_tuple_count << "\n";
    std::cout << "free_space_offset: " << header->free_space_offset << "\n";
    std::cout << "free_list_head: " << header->free_list_head << "\n";

    auto schema_it = state.heap_schemas.find(page_id);
    auto owner_it = state.heap_owner_names.find(page_id);
    if (owner_it != state.heap_owner_names.end()) {
        std::cout << "decoded_as_table: '" << owner_it->second << "'\n";
    }

    for (slot_id_t slot_id = 0; slot_id < header->slot_count; ++slot_id) {
        const storage::Slot* slot = table_page.GetSlot(slot_id);
        if (slot == nullptr) {
            std::cout << "  slot[" << slot_id << "]: <null>\n";
            continue;
        }

        std::cout << "  slot[" << slot_id << "]"
                  << " offset=" << slot->GetOffset()
                  << " length=" << slot->GetLength()
                  << " deleted=" << (slot->IsDeleted() ? "true" : "false")
                  << " moved=" << (slot->IsMoved() ? "true" : "false");

        if (slot->IsDeleted()) {
            std::cout << "\n";
            continue;
        }

        record::Tuple tuple;
        const auto tuple_exp = table_page.GetTuple(slot_id, tuple);
        if (!tuple_exp.has_value()) {
            std::cout << " decode_error='" << tuple_exp.error().msg << "'\n";
            continue;
        }

        std::cout << " tuple_size=" << tuple.Size();
        if (schema_it != state.heap_schemas.end() && schema_it->second != nullptr) {
            auto decoded_exp = record::TupleCodec::Decode(*schema_it->second, tuple);
            if (decoded_exp.has_value()) {
                std::cout << " values=" << ValuesToString(decoded_exp.value()) << "\n";
            } else {
                std::cout << " values_decode_error='" << decoded_exp.error().msg << "'"
                          << " tuple_hex="
                          << HexBytes(
                                 std::span<const std::byte>(tuple.Data(), tuple.Size()),
                                 tuple.Size())
                          << "\n";
            }
        } else {
            std::cout << " tuple_hex="
                      << HexBytes(std::span<const std::byte>(tuple.Data(), tuple.Size()), tuple.Size())
                      << "\n";
        }
    }
}

void DumpLeafPage(storage::Page& page)
{
    storage::BPlusTreeLeafPage leaf_page(&page);

    std::cout << "kind: B+Tree LEAF page\n";
    std::cout << "persistent.page_id: " << page.Header()->page_id << "\n";
    std::cout << "persistent.lsn: " << page.Header()->lsn << "\n";
    std::cout << "parent_page_id: " << FormatPageId(leaf_page.GetParentPageId()) << "\n";
    std::cout << "size: " << leaf_page.GetSize() << "\n";
    std::cout << "max_size: " << leaf_page.GetMaxSize() << "\n";
    std::cout << "next_page_id: " << FormatPageId(leaf_page.GetNextPageId()) << "\n";

    for (uint16_t i = 0; i < leaf_page.GetSize(); ++i) {
        const auto& item = leaf_page.ItemAt(i);
        std::cout << "  item[" << i << "] key=" << item.key
                  << " value=" << RidToString(item.value) << "\n";
    }
}

void DumpInternalPage(storage::Page& page)
{
    storage::BPlusTreeInternalPage internal_page(&page);

    std::cout << "kind: B+Tree INTERNAL page\n";
    std::cout << "persistent.page_id: " << page.Header()->page_id << "\n";
    std::cout << "persistent.lsn: " << page.Header()->lsn << "\n";
    std::cout << "parent_page_id: " << FormatPageId(internal_page.GetParentPageId()) << "\n";
    std::cout << "size: " << internal_page.GetSize() << "\n";
    std::cout << "max_size: " << internal_page.GetMaxSize() << "\n";
    std::cout << "leftmost_child: " << FormatPageId(internal_page.GetLeftMostChild()) << "\n";

    for (uint16_t i = 0; i < internal_page.GetSize(); ++i) {
        const auto& item = internal_page.ItemAt(i);
        std::cout << "  item[" << i << "] key=" << item.key
                  << " child=" << FormatPageId(item.child_page_id) << "\n";
    }
}

void DumpGenericPage(page_id_t page_id, const page_data_t& raw, const DumpState& state)
{
    storage::Page page;
    page.Data() = raw;
    const auto* persistent = page.Header();

    std::cout << "persistent.page_id: " << persistent->page_id << "\n";
    std::cout << "persistent.page_type: " << PageTypeToString(persistent->page_type) << "\n";
    std::cout << "persistent.lsn: " << persistent->lsn << "\n";

    if (persistent->page_id != page_id) {
        std::cout << "warning: persistent page_id does not match physical page id\n";
    }

    switch (persistent->page_type) {
        case storage::PageType::HEAP:
            DumpHeapPage(page_id, page, state);
            return;
        case storage::PageType::LEAF:
            DumpLeafPage(page);
            return;
        case storage::PageType::INTERNAL:
            DumpInternalPage(page);
            return;
        case storage::PageType::HEADER: {
            const auto catalog_opaque = ReadCatalogMetaOpaque(persistent);
            const auto bptree_opaque = ReadBPlusTreeMetaOpaque(persistent);

            if (state.catalog_meta_pages.contains(page_id) ||
                (catalog_opaque.magic == CATALOG_META_PAGE_MAGIC &&
                 catalog_opaque.version == CATALOG_META_PAGE_VERSION)) {
                DumpCatalogMetaPage(page);
                return;
            }
            if (bptree_opaque.magic == kBPlusTreeMetaMagic &&
                bptree_opaque.version == kBPlusTreeMetaVersion) {
                DumpBPlusTreeHeaderPage(page);
                return;
            }
            DumpUnknownHeaderPage(page);
            return;
        }
        case storage::PageType::INVALID:
        case storage::PageType::FREELIST:
            break;
    }

    std::cout << "kind: generic page\n";
    std::cout << "opaque_preview: "
              << HexBytes(
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(persistent->opaque),
                         storage::PAGE_HEADER_OPAQUE_SIZE),
                     storage::PAGE_HEADER_OPAQUE_SIZE)
              << "\n";
    std::cout << "body_preview: "
              << HexBytes(
                     std::span<const std::byte>(page.RawData() + HEADER_SIZE, PAGE_SIZE - HEADER_SIZE),
                     64)
              << "\n";
}

void PrintUsage(const char* argv0)
{
    const char* name = argv0 == nullptr ? "haruhidb_db_dump" : argv0;
    std::cerr << "usage: " << name << " <db_path>\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        PrintUsage(argc > 0 ? argv[0] : nullptr);
        return 1;
    }

    try {
        const std::filesystem::path db_path = argv[1];
        if (!std::filesystem::exists(db_path)) {
            throw std::runtime_error("database file does not exist: " + db_path.string());
        }

        const uint64_t file_size = std::filesystem::file_size(db_path);
        if (file_size == 0 || file_size % PAGE_SIZE != 0) {
            throw std::runtime_error("database file size is invalid for page-aligned dump");
        }

        const page_id_t page_count = static_cast<page_id_t>(file_size / PAGE_SIZE);
        storage::DiskManager disk_manager(db_path);

        page_data_t header_raw{};
        std::string read_error;
        if (!ReadRawPage(&disk_manager, 0, &header_raw, &read_error)) {
            throw std::runtime_error("failed to read DBHeader page: " + read_error);
        }

        storage::DBHeader db_header{};
        std::memcpy(&db_header, header_raw.data(), sizeof(db_header));

        DumpState state;
        const auto free_list_pages = CollectFreeListPages(&disk_manager, db_header, page_count, &state);
        const auto catalog_meta_pages = CollectCatalogMetaPages(&disk_manager, db_header, page_count, &state);

        std::string runtime_error;
        auto runtime = TryOpenRuntime(db_path, &runtime_error);
        if (runtime != nullptr) {
            AnnotateFromCatalog(runtime->GetCatalog(), &disk_manager, page_count, &state);
        }

        std::cout << "database file: " << db_path << "\n";
        std::cout << "file_size: " << file_size << " bytes\n";
        std::cout << "page_size: " << PAGE_SIZE << "\n";
        std::cout << "page_count: " << page_count << "\n\n";

        std::cout << "== Database Summary ==\n";
        DumpDbHeader(db_header);
        std::cout << "free_list_chain: ";
        if (free_list_pages.empty()) {
            std::cout << "<empty>\n";
        } else {
            bool first = true;
            for (page_id_t page_id : free_list_pages) {
                if (!first) {
                    std::cout << " -> ";
                }
                std::cout << page_id;
                first = false;
            }
            std::cout << "\n";
        }
        std::cout << "catalog_meta_chain: ";
        if (catalog_meta_pages.empty()) {
            std::cout << "<empty>\n";
        } else {
            bool first = true;
            for (page_id_t page_id : catalog_meta_pages) {
                if (!first) {
                    std::cout << " -> ";
                }
                std::cout << page_id;
                first = false;
            }
            std::cout << "\n";
        }

        if (runtime != nullptr) {
            PrintCatalogSummary(runtime->GetCatalog());
        } else {
            std::cout << "catalog summary: unavailable\n";
            std::cout << "catalog/runtime load error: " << runtime_error << "\n";
        }

        if (!state.warnings.empty()) {
            std::cout << "warnings:\n";
            for (const auto& warning : state.warnings) {
                std::cout << "  - " << warning << "\n";
            }
        }

        for (page_id_t page_id = 0; page_id < page_count; ++page_id) {
            page_data_t raw{};
            std::string error;
            if (!ReadRawPage(&disk_manager, page_id, &raw, &error)) {
                std::cout << "\n===== Page " << page_id << " =====\n";
                std::cout << "read_error: " << error << "\n";
                continue;
            }

            PrintPageBanner(page_id, state);
            if (page_id == 0) {
                storage::DBHeader header{};
                std::memcpy(&header, raw.data(), sizeof(header));
                DumpDbHeader(header);
                continue;
            }
            if (state.free_list_pages.contains(page_id)) {
                DumpFreeListPage(page_id, raw);
                continue;
            }
            DumpGenericPage(page_id, raw, state);
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "db_dump failed: " << e.what() << '\n';
        return 1;
    }
}
