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
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

struct WalDiskRecordHeaderView
{
    uint32_t magic{WAL_MAGIC};
    uint32_t version{WAL_VERSION};
    uint8_t type{0};
    uint8_t reserved[7]{};
    lsn_t lsn{0};
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t payload_len{0};
};

static_assert(sizeof(CatalogMetaPageOpaqueView) == storage::PAGE_HEADER_OPAQUE_SIZE);
static_assert(sizeof(BPlusTreeMetaOpaqueView) == storage::PAGE_HEADER_OPAQUE_SIZE);
static_assert(std::is_trivially_copyable_v<WalDiskRecordHeaderView>);
static_assert(sizeof(WalDiskRecordHeaderView) == 32);

enum class DbMode {
    Summary,
    Header,
    Catalog,
    Table,
    Index,
    Page,
};

enum class WalMode {
    Summary,
    Entry,
    Limit,
};

struct DbCommandOptions
{
    std::filesystem::path db_path;
    DbMode mode{DbMode::Summary};
    std::string table_name;
    std::string index_name;
    page_id_t page_id{INVALID_PAGE_ID};
};

struct WalCommandOptions
{
    std::filesystem::path wal_path;
    WalMode mode{WalMode::Summary};
    size_t entry_index{0};
    size_t limit{20};
};

struct DumpState
{
    std::unordered_map<page_id_t, std::vector<std::string>> page_labels;
    std::unordered_map<page_id_t, const catalog::Schema*> heap_schemas;
    std::unordered_map<page_id_t, std::string> heap_owner_names;
    std::unordered_set<page_id_t> free_list_pages;
    std::unordered_set<page_id_t> catalog_meta_pages;
    std::vector<std::string> warnings;
};

struct DbSession
{
    std::filesystem::path db_path;
    uint64_t file_size{0};
    page_id_t page_count{0};
    storage::DBHeader db_header{};
    std::unique_ptr<storage::DiskManager> disk_manager;
    std::unique_ptr<runtime::DatabaseRuntime> runtime;
    DumpState state;
    std::vector<page_id_t> free_list_chain;
    std::vector<page_id_t> catalog_meta_chain;
};

struct IndexLookupResult
{
    const catalog::TableInfo* table_info{nullptr};
    const catalog::TableInfo::IndexEntry* index_entry{nullptr};
};

struct WalEntry
{
    size_t entry_no{0};
    WalDiskRecordHeaderView header{};
    page_data_t after_image{};
    page_id_t after_page_id{INVALID_PAGE_ID};
    storage::PageType after_page_type{storage::PageType::INVALID};
    lsn_t after_page_lsn{0};
};

struct WalScanResult
{
    std::filesystem::path wal_path;
    uint64_t file_size{0};
    std::vector<WalEntry> entries;
    std::optional<std::string> tail_issue;
};

enum class CatalogLoadPolicy {
    Never,
    BestEffort,
    Required,
};

std::string NormalizeName(std::string_view input)
{
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

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

std::string WalTypeToString(uint8_t raw_type)
{
    switch (raw_type) {
        case static_cast<uint8_t>(storage::wal::LogRecordType::PUT):
            return "put";
        case static_cast<uint8_t>(storage::wal::LogRecordType::DELETE):
            return "delete";
        default:
            return "unknown";
    }
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

template <typename T>
bool ParseUnsigned(std::string_view text, T* out)
{
    if (out == nullptr || text.empty()) {
        return false;
    }
    uint64_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    if (value > static_cast<uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    *out = static_cast<T>(value);
    return true;
}

bool IsSupportedWalType(uint8_t raw_type)
{
    return raw_type == static_cast<uint8_t>(storage::wal::LogRecordType::PUT) ||
           raw_type == static_cast<uint8_t>(storage::wal::LogRecordType::DELETE);
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
        if (current == 0 || current >= page_count) {
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
        if (current == 0 || current >= page_count) {
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
            AddWarning(state, "catalog meta page " + std::to_string(current) + " has unexpected magic/version");
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
        if (current == 0 || current >= page_count) {
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
    const std::string owner_prefix = "index '" + table_info->Name() + "." + index_entry.index_name + "'";

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
        if (page_id == 0 || page_id >= page_count) {
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
                    " is neither LEAF nor INTERNAL (actual=" + PageTypeToString(persistent->page_type) + ')');
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

std::optional<DbSession> LoadDbSession(
    const std::filesystem::path& db_path,
    CatalogLoadPolicy catalog_policy,
    std::string* error)
{
    if (!std::filesystem::exists(db_path)) {
        if (error != nullptr) {
            *error = "database file does not exist: " + db_path.string();
        }
        return std::nullopt;
    }

    std::error_code ec;
    const uint64_t file_size = std::filesystem::file_size(db_path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to stat database file: " + ec.message();
        }
        return std::nullopt;
    }
    if (file_size == 0 || file_size % PAGE_SIZE != 0) {
        if (error != nullptr) {
            *error = "database file size is invalid for page-aligned inspection";
        }
        return std::nullopt;
    }

    DbSession session;
    session.db_path = db_path;
    session.file_size = file_size;
    session.page_count = static_cast<page_id_t>(file_size / PAGE_SIZE);

    try {
        session.disk_manager = std::make_unique<storage::DiskManager>(db_path);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = std::string("failed to open disk manager: ") + e.what();
        }
        return std::nullopt;
    }

    page_data_t header_raw{};
    std::string read_error;
    if (!ReadRawPage(session.disk_manager.get(), 0, &header_raw, &read_error)) {
        if (error != nullptr) {
            *error = "failed to read DBHeader page: " + read_error;
        }
        return std::nullopt;
    }
    std::memcpy(&session.db_header, header_raw.data(), sizeof(session.db_header));

    session.free_list_chain = CollectFreeListPages(
        session.disk_manager.get(), session.db_header, session.page_count, &session.state);
    session.catalog_meta_chain = CollectCatalogMetaPages(
        session.disk_manager.get(), session.db_header, session.page_count, &session.state);

    if (catalog_policy == CatalogLoadPolicy::Never) {
        return session;
    }

    std::string runtime_error;
    session.runtime = TryOpenRuntime(db_path, &runtime_error);
    if (session.runtime == nullptr) {
        if (catalog_policy == CatalogLoadPolicy::Required) {
            if (error != nullptr) {
                *error = "failed to load catalog/runtime view: " + runtime_error;
            }
            return std::nullopt;
        }
        AddWarning(&session.state, "catalog/runtime load unavailable: " + runtime_error);
        return session;
    }

    AnnotateFromCatalog(
        session.runtime->GetCatalog(), session.disk_manager.get(), session.page_count, &session.state);
    return session;
}

std::optional<IndexLookupResult> FindIndexByName(const catalog::Catalog* catalog, std::string_view index_name)
{
    if (catalog == nullptr) {
        return std::nullopt;
    }

    const std::string normalized = NormalizeName(index_name);
    auto tables = catalog->GetAllTables();
    for (const auto* table_info : tables) {
        if (table_info == nullptr) {
            continue;
        }
        for (const auto& index_entry : table_info->IndexEntries()) {
            if (NormalizeName(index_entry.index_name) == normalized) {
                return IndexLookupResult{table_info, &index_entry};
            }
        }
    }
    return std::nullopt;
}

bool ReadDbPage(const DbSession& session, page_id_t page_id, page_data_t* out, std::string* error)
{
    return ReadRawPage(session.disk_manager.get(), page_id, out, error);
}

void PrintDbHeader(const storage::DBHeader& db_header)
{
    std::cout << "kind: DBHeader\n";
    std::cout << "magic_number: " << db_header.magic_number << "\n";
    std::cout << "version: " << db_header.version << "\n";
    std::cout << "next_page_id: " << FormatPageId(db_header.next_page_id) << "\n";
    std::cout << "free_list_head: " << FormatPageId(db_header.free_list_head) << "\n";
    std::cout << "catalog_meta_page_id: " << FormatPageId(db_header.catalog_meta_page_id) << "\n";
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

    size_t index_count = 0;
    for (const auto* table_info : tables) {
        if (table_info != nullptr) {
            index_count += table_info->IndexEntries().size();
        }
    }

    std::cout << "catalog tables: " << tables.size() << "\n";
    std::cout << "catalog indexes: " << index_count << "\n";
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

void PrintPageBanner(page_id_t page_id, const DumpState& state)
{
    std::cout << "\n===== Page " << page_id << " =====\n";
    auto it = state.page_labels.find(page_id);
    if (it != state.page_labels.end() && !it->second.empty()) {
        std::cout << "labels: " << Join(it->second, ", ") << "\n";
    }
}

void DumpFreeListPage(const page_data_t& raw)
{
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
                          << HexBytes(std::span<const std::byte>(tuple.Data(), tuple.Size()), tuple.Size())
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

void PrintDbSummary(const DbSession& session)
{
    std::cout << "database file: " << session.db_path << "\n";
    std::cout << "file_size: " << session.file_size << " bytes\n";
    std::cout << "page_size: " << PAGE_SIZE << "\n";
    std::cout << "page_count: " << session.page_count << "\n\n";

    std::cout << "== Database Summary ==\n";
    PrintDbHeader(session.db_header);

    std::cout << "free_list_chain: ";
    if (session.free_list_chain.empty()) {
        std::cout << "<empty>\n";
    } else {
        for (size_t i = 0; i < session.free_list_chain.size(); ++i) {
            if (i != 0) {
                std::cout << " -> ";
            }
            std::cout << session.free_list_chain[i];
        }
        std::cout << "\n";
    }

    std::cout << "catalog_meta_chain: ";
    if (session.catalog_meta_chain.empty()) {
        std::cout << "<empty>\n";
    } else {
        for (size_t i = 0; i < session.catalog_meta_chain.size(); ++i) {
            if (i != 0) {
                std::cout << " -> ";
            }
            std::cout << session.catalog_meta_chain[i];
        }
        std::cout << "\n";
    }

    PrintCatalogSummary(session.runtime == nullptr ? nullptr : session.runtime->GetCatalog());

    if (!session.state.warnings.empty()) {
        std::cout << "warnings:\n";
        for (const auto& warning : session.state.warnings) {
            std::cout << "  - " << warning << "\n";
        }
    }
}

void PrintTableDetail(const DbSession& session, const catalog::TableInfo* table_info)
{
    if (table_info == nullptr) {
        return;
    }

    const page_id_t first_page_id = table_info->GetTableHeap() == nullptr
        ? INVALID_PAGE_ID
        : table_info->GetTableHeap()->FirstPageId();

    std::cout << "table oid: " << table_info->Oid() << "\n";
    std::cout << "table name: " << table_info->Name() << "\n";
    std::cout << "first_page_id: " << FormatPageId(first_page_id) << "\n";
    std::cout << "schema: " << table_info->GetSchema().ToString() << "\n";
    std::cout << "indexes: " << table_info->IndexEntries().size() << "\n";
    for (const auto& index_entry : table_info->IndexEntries()) {
        const page_id_t root_page_id = index_entry.index == nullptr
            ? INVALID_PAGE_ID
            : index_entry.index->RootPageId();
        std::cout << "  - oid=" << index_entry.index_oid
                  << " name='" << index_entry.index_name << "'"
                  << " header_page_id=" << FormatPageId(index_entry.header_page_id)
                  << " root_page_id=" << FormatPageId(root_page_id) << "\n";
    }

    page_id_t current = first_page_id;
    size_t heap_index = 0;
    std::unordered_set<page_id_t> visited;

    while (current != INVALID_PAGE_ID) {
        if (current == 0 || current >= session.page_count) {
            throw std::runtime_error(
                "table '" + table_info->Name() + "' heap chain points to out-of-range page " +
                std::to_string(current));
        }
        if (!visited.insert(current).second) {
            throw std::runtime_error(
                "table '" + table_info->Name() + "' heap chain cycle detected at page " +
                std::to_string(current));
        }

        page_data_t raw{};
        std::string error;
        if (!ReadDbPage(session, current, &raw, &error)) {
            throw std::runtime_error(
                "failed to read heap page " + std::to_string(current) + " for table '" +
                table_info->Name() + "': " + error);
        }

        std::cout << "\n== Heap Page #" << heap_index << " ==\n";
        PrintPageBanner(current, session.state);
        DumpGenericPage(current, raw, session.state);

        storage::Page page;
        page.Data() = raw;
        storage::TablePage table_page(&page);
        current = table_page.NextPageId();
        ++heap_index;
    }
}

void PrintIndexDetail(const DbSession& session, const IndexLookupResult& lookup)
{
    const auto* table_info = lookup.table_info;
    const auto* index_entry = lookup.index_entry;
    if (table_info == nullptr || index_entry == nullptr) {
        return;
    }

    const page_id_t root_page_id = index_entry->index == nullptr
        ? INVALID_PAGE_ID
        : index_entry->index->RootPageId();

    std::cout << "table name: " << table_info->Name() << "\n";
    std::cout << "table oid: " << table_info->Oid() << "\n";
    std::cout << "index name: " << index_entry->index_name << "\n";
    std::cout << "index oid: " << index_entry->index_oid << "\n";
    std::cout << "header_page_id: " << FormatPageId(index_entry->header_page_id) << "\n";
    std::cout << "root_page_id: " << FormatPageId(root_page_id) << "\n";

    page_data_t header_raw{};
    std::string error;
    if (!ReadDbPage(session, index_entry->header_page_id, &header_raw, &error)) {
        throw std::runtime_error(
            "failed to read index header page " + std::to_string(index_entry->header_page_id) + ": " + error);
    }

    std::cout << "\n== Index Header Page ==\n";
    PrintPageBanner(index_entry->header_page_id, session.state);
    DumpGenericPage(index_entry->header_page_id, header_raw, session.state);

    if (root_page_id == INVALID_PAGE_ID) {
        return;
    }

    page_data_t root_raw{};
    if (!ReadDbPage(session, root_page_id, &root_raw, &error)) {
        throw std::runtime_error(
            "failed to read index root page " + std::to_string(root_page_id) + ": " + error);
    }

    std::cout << "\n== Index Root Page ==\n";
    PrintPageBanner(root_page_id, session.state);
    DumpGenericPage(root_page_id, root_raw, session.state);
}

void PrintSinglePage(const DbSession& session, page_id_t page_id)
{
    if (page_id >= session.page_count) {
        throw std::runtime_error(
            "page id out of range: " + std::to_string(page_id) +
            " (page_count=" + std::to_string(session.page_count) + ")");
    }

    page_data_t raw{};
    std::string error;
    if (!ReadDbPage(session, page_id, &raw, &error)) {
        throw std::runtime_error("failed to read page " + std::to_string(page_id) + ": " + error);
    }

    PrintPageBanner(page_id, session.state);
    if (page_id == 0) {
        storage::DBHeader header{};
        std::memcpy(&header, raw.data(), sizeof(header));
        PrintDbHeader(header);
        return;
    }
    if (session.state.free_list_pages.contains(page_id)) {
        DumpFreeListPage(raw);
        return;
    }
    DumpGenericPage(page_id, raw, session.state);
}

std::optional<WalScanResult> LoadWalScanResult(const std::filesystem::path& wal_path, std::string* error)
{
    if (!std::filesystem::exists(wal_path)) {
        if (error != nullptr) {
            *error = "wal file does not exist: " + wal_path.string();
        }
        return std::nullopt;
    }

    std::error_code ec;
    const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to stat wal file: " + ec.message();
        }
        return std::nullopt;
    }

    std::ifstream in(wal_path, std::ios::binary);
    if (!in.is_open()) {
        if (error != nullptr) {
            *error = "failed to open wal file";
        }
        return std::nullopt;
    }

    WalScanResult result;
    result.wal_path = wal_path;
    result.file_size = file_size;

    size_t entry_no = 0;
    while (true) {
        WalDiskRecordHeaderView header{};
        in.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
        const auto header_bytes = in.gcount();
        if (header_bytes == 0) {
            break;
        }
        if (header_bytes != static_cast<std::streamsize>(sizeof(header))) {
            result.tail_issue = "truncated tail: partial WAL record header";
            break;
        }
        if (header.magic != WAL_MAGIC ||
            header.version != WAL_VERSION ||
            header.payload_len != WAL_PAYLOAD_LEN ||
            !IsSupportedWalType(header.type)) {
            result.tail_issue = "corrupted tail: invalid WAL record header at entry " + std::to_string(entry_no);
            break;
        }

        WalEntry entry;
        entry.entry_no = entry_no;
        entry.header = header;
        in.read(
            reinterpret_cast<char*>(entry.after_image.data()),
            static_cast<std::streamsize>(entry.after_image.size()));
        if (in.gcount() != static_cast<std::streamsize>(entry.after_image.size())) {
            result.tail_issue = "truncated tail: incomplete WAL payload at entry " + std::to_string(entry_no);
            break;
        }

        storage::Page page;
        page.Data() = entry.after_image;
        entry.after_page_id = page.Header()->page_id;
        entry.after_page_type = page.Header()->page_type;
        entry.after_page_lsn = page.Header()->lsn;

        result.entries.push_back(std::move(entry));
        ++entry_no;
    }

    return result;
}

void PrintWalSummary(const WalScanResult& scan)
{
    size_t put_count = 0;
    size_t delete_count = 0;
    std::optional<lsn_t> min_lsn;
    std::optional<lsn_t> max_lsn;

    for (const auto& entry : scan.entries) {
        if (entry.header.type == static_cast<uint8_t>(storage::wal::LogRecordType::PUT)) {
            ++put_count;
        } else if (entry.header.type == static_cast<uint8_t>(storage::wal::LogRecordType::DELETE)) {
            ++delete_count;
        }

        min_lsn = min_lsn.has_value() ? std::min(min_lsn.value(), entry.header.lsn) : entry.header.lsn;
        max_lsn = max_lsn.has_value() ? std::max(max_lsn.value(), entry.header.lsn) : entry.header.lsn;
    }

    std::cout << "wal file: " << scan.wal_path << "\n";
    std::cout << "file_size: " << scan.file_size << " bytes\n";
    std::cout << "entry_count: " << scan.entries.size() << "\n";
    std::cout << "lsn_min: " << (min_lsn.has_value() ? std::to_string(min_lsn.value()) : std::string("N/A"))
              << "\n";
    std::cout << "lsn_max: " << (max_lsn.has_value() ? std::to_string(max_lsn.value()) : std::string("N/A"))
              << "\n";
    std::cout << "put_count: " << put_count << "\n";
    std::cout << "delete_count: " << delete_count << "\n";
    std::cout << "scan_status: " << (scan.tail_issue.has_value() ? scan.tail_issue.value() : std::string("ok"))
              << "\n";
}

void PrintWalEntryDetail(const WalEntry& entry)
{
    std::cout << "entry_no: " << entry.entry_no << "\n";
    std::cout << "type: " << WalTypeToString(entry.header.type) << "\n";
    std::cout << "lsn: " << entry.header.lsn << "\n";
    std::cout << "page_id: " << entry.header.page_id << "\n";
    std::cout << "payload_len: " << entry.header.payload_len << "\n";
    std::cout << "after_image.page_id: " << entry.after_page_id << "\n";
    std::cout << "after_image.page_type: " << PageTypeToString(entry.after_page_type) << "\n";
    std::cout << "after_image.page_lsn: " << entry.after_page_lsn << "\n";
    std::cout << "payload_preview: "
              << HexBytes(std::span<const std::byte>(entry.after_image.data(), entry.after_image.size()), 96)
              << "\n";
}

void PrintWalEntryBrief(const WalEntry& entry)
{
    std::cout << "entry_no=" << entry.entry_no
              << " type=" << WalTypeToString(entry.header.type)
              << " lsn=" << entry.header.lsn
              << " page_id=" << entry.header.page_id
              << " after_page_type=" << PageTypeToString(entry.after_page_type)
              << "\n";
}

void PrintTopHelp(std::ostream& out)
{
    out << "Usage:\n"
        << "  haruhidb_inspect <command> <file> [options]\n\n"
        << "Commands:\n"
        << "  db       Inspect database file\n"
        << "  wal      Inspect WAL file\n\n"
        << "Run 'haruhidb_inspect <command> --help' for more information on a command.\n";
}

void PrintDbHelp(std::ostream& out)
{
    out << "Usage:\n"
        << "  haruhidb_inspect db <db_file> [options]\n\n"
        << "Default:\n"
        << "  Without an explicit mode, defaults to --summary\n\n"
        << "Options:\n"
        << "  --summary              Show database summary\n"
        << "  --header               Show database header only\n"
        << "  --catalog              Show catalog metadata\n"
        << "  --table <name>         Show one table\n"
        << "  --index <name>         Show one index\n"
        << "  --page <id>            Show one page\n"
        << "  --help                 Show this message\n";
}

void PrintWalHelp(std::ostream& out)
{
    out << "Usage:\n"
        << "  haruhidb_inspect wal <wal_file> [options]\n\n"
        << "Default:\n"
        << "  Without an explicit mode, defaults to --summary\n\n"
        << "Options:\n"
        << "  --summary              Show WAL summary\n"
        << "  --entry <n>            Show one entry\n"
        << "  --limit <n>            Show first n entries\n"
        << "  --help                 Show this message\n";
}

bool ParseDbOptions(int argc, char** argv, DbCommandOptions* out, std::string* error)
{
    if (out == nullptr) {
        return false;
    }

    bool mode_selected = false;
    bool file_selected = false;

    for (int i = 0; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help") {
            if (error != nullptr) {
                *error = "help";
            }
            return false;
        }
        if (!arg.empty() && arg[0] != '-') {
            if (file_selected) {
                if (error != nullptr) {
                    *error = "unexpected extra positional argument: " + std::string(arg);
                }
                return false;
            }
            out->db_path = std::filesystem::path(std::string(arg));
            file_selected = true;
            continue;
        }
        if (arg == "--summary") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            out->mode = DbMode::Summary;
            mode_selected = true;
            continue;
        }
        if (arg == "--header") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            out->mode = DbMode::Header;
            mode_selected = true;
            continue;
        }
        if (arg == "--catalog") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            out->mode = DbMode::Catalog;
            mode_selected = true;
            continue;
        }
        if (arg == "--table") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "--table requires a table name";
                }
                return false;
            }
            out->mode = DbMode::Table;
            out->table_name = argv[++i];
            mode_selected = true;
            continue;
        }
        if (arg == "--index") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "--index requires an index name";
                }
                return false;
            }
            out->mode = DbMode::Index;
            out->index_name = argv[++i];
            mode_selected = true;
            continue;
        }
        if (arg == "--page") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "db mode options are mutually exclusive";
                }
                return false;
            }
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "--page requires a page id";
                }
                return false;
            }
            page_id_t page_id = INVALID_PAGE_ID;
            if (!ParseUnsigned<page_id_t>(argv[i + 1], &page_id)) {
                if (error != nullptr) {
                    *error = "invalid page id: " + std::string(argv[i + 1]);
                }
                return false;
            }
            out->mode = DbMode::Page;
            out->page_id = page_id;
            ++i;
            mode_selected = true;
            continue;
        }

        if (error != nullptr) {
            *error = "unknown db option: " + std::string(arg);
        }
        return false;
    }

    if (!file_selected) {
        if (error != nullptr) {
            *error = "db file path is required";
        }
        return false;
    }
    if (!mode_selected) {
        out->mode = DbMode::Summary;
    }
    return true;
}

bool ParseWalOptions(int argc, char** argv, WalCommandOptions* out, std::string* error)
{
    if (out == nullptr) {
        return false;
    }

    bool mode_selected = false;
    bool file_selected = false;

    for (int i = 0; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help") {
            if (error != nullptr) {
                *error = "help";
            }
            return false;
        }
        if (!arg.empty() && arg[0] != '-') {
            if (file_selected) {
                if (error != nullptr) {
                    *error = "unexpected extra positional argument: " + std::string(arg);
                }
                return false;
            }
            out->wal_path = std::filesystem::path(std::string(arg));
            file_selected = true;
            continue;
        }
        if (arg == "--summary") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "wal mode options are mutually exclusive";
                }
                return false;
            }
            out->mode = WalMode::Summary;
            mode_selected = true;
            continue;
        }
        if (arg == "--entry") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "wal mode options are mutually exclusive";
                }
                return false;
            }
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "--entry requires an entry index";
                }
                return false;
            }
            size_t entry_index = 0;
            if (!ParseUnsigned<size_t>(argv[i + 1], &entry_index)) {
                if (error != nullptr) {
                    *error = "invalid entry index: " + std::string(argv[i + 1]);
                }
                return false;
            }
            out->mode = WalMode::Entry;
            out->entry_index = entry_index;
            ++i;
            mode_selected = true;
            continue;
        }
        if (arg == "--limit") {
            if (mode_selected) {
                if (error != nullptr) {
                    *error = "wal mode options are mutually exclusive";
                }
                return false;
            }
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "--limit requires a count";
                }
                return false;
            }
            size_t limit = 0;
            if (!ParseUnsigned<size_t>(argv[i + 1], &limit)) {
                if (error != nullptr) {
                    *error = "invalid limit: " + std::string(argv[i + 1]);
                }
                return false;
            }
            out->mode = WalMode::Limit;
            out->limit = limit;
            ++i;
            mode_selected = true;
            continue;
        }

        if (error != nullptr) {
            *error = "unknown wal option: " + std::string(arg);
        }
        return false;
    }

    if (!file_selected) {
        if (error != nullptr) {
            *error = "wal file path is required";
        }
        return false;
    }
    if (!mode_selected) {
        out->mode = WalMode::Summary;
    }
    return true;
}

int RunDbCommand(const DbCommandOptions& options)
{
    const CatalogLoadPolicy policy =
        options.mode == DbMode::Header ? CatalogLoadPolicy::Never
        : options.mode == DbMode::Page ? CatalogLoadPolicy::BestEffort
                                      : CatalogLoadPolicy::Required;

    std::string error;
    auto session_exp = LoadDbSession(options.db_path, policy, &error);
    if (!session_exp.has_value()) {
        std::cerr << "haruhidb_inspect db: " << error << '\n';
        return 1;
    }
    const DbSession& session = session_exp.value();

    try {
        switch (options.mode) {
            case DbMode::Summary:
                PrintDbSummary(session);
                return 0;
            case DbMode::Header:
                PrintDbHeader(session.db_header);
                return 0;
            case DbMode::Catalog:
                std::cout << "catalog_meta_chain: ";
                if (session.catalog_meta_chain.empty()) {
                    std::cout << "<empty>\n";
                } else {
                    for (size_t i = 0; i < session.catalog_meta_chain.size(); ++i) {
                        if (i != 0) {
                            std::cout << " -> ";
                        }
                        std::cout << session.catalog_meta_chain[i];
                    }
                    std::cout << "\n";
                }
                PrintCatalogSummary(session.runtime == nullptr ? nullptr : session.runtime->GetCatalog());
                return 0;
            case DbMode::Table: {
                auto* catalog = session.runtime == nullptr ? nullptr : session.runtime->GetCatalog();
                const auto* table_info = catalog == nullptr ? nullptr : catalog->GetTable(options.table_name);
                if (table_info == nullptr) {
                    std::cerr << "haruhidb_inspect db: table not found: " << options.table_name << '\n';
                    return 1;
                }
                PrintTableDetail(session, table_info);
                return 0;
            }
            case DbMode::Index: {
                auto* catalog = session.runtime == nullptr ? nullptr : session.runtime->GetCatalog();
                auto lookup = FindIndexByName(catalog, options.index_name);
                if (!lookup.has_value()) {
                    std::cerr << "haruhidb_inspect db: index not found: " << options.index_name << '\n';
                    return 1;
                }
                PrintIndexDetail(session, lookup.value());
                return 0;
            }
            case DbMode::Page:
                PrintSinglePage(session, options.page_id);
                return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "haruhidb_inspect db: " << e.what() << '\n';
        return 1;
    }

    return 1;
}

int RunWalCommand(const WalCommandOptions& options)
{
    std::string error;
    auto scan_exp = LoadWalScanResult(options.wal_path, &error);
    if (!scan_exp.has_value()) {
        std::cerr << "haruhidb_inspect wal: " << error << '\n';
        return 1;
    }
    const WalScanResult& scan = scan_exp.value();

    switch (options.mode) {
        case WalMode::Summary:
            PrintWalSummary(scan);
            return 0;
        case WalMode::Entry:
            if (options.entry_index >= scan.entries.size()) {
                std::cerr << "haruhidb_inspect wal: entry index out of range: " << options.entry_index << '\n';
                if (scan.tail_issue.has_value()) {
                    std::cerr << "haruhidb_inspect wal: scan stopped because " << scan.tail_issue.value() << '\n';
                }
                return 1;
            }
            PrintWalEntryDetail(scan.entries[options.entry_index]);
            if (scan.tail_issue.has_value()) {
                std::cout << "scan_status: " << scan.tail_issue.value() << "\n";
            }
            return 0;
        case WalMode::Limit: {
            const size_t limit = std::min(options.limit, scan.entries.size());
            for (size_t i = 0; i < limit; ++i) {
                PrintWalEntryBrief(scan.entries[i]);
            }
            if (scan.tail_issue.has_value()) {
                std::cout << "scan_status: " << scan.tail_issue.value() << "\n";
            }
            return 0;
        }
    }
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc <= 1) {
        PrintTopHelp(std::cout);
        return 0;
    }

    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h") {
        PrintTopHelp(std::cout);
        return 0;
    }

    if (command == "db") {
        if (argc == 3 && std::string_view(argv[2]) == "--help") {
            PrintDbHelp(std::cout);
            return 0;
        }

        DbCommandOptions options;
        std::string error;
        if (!ParseDbOptions(argc - 2, argv + 2, &options, &error)) {
            if (error == "help") {
                PrintDbHelp(std::cout);
                return 0;
            }
            std::cerr << "haruhidb_inspect db: " << error << "\n\n";
            PrintDbHelp(std::cerr);
            return 1;
        }
        return RunDbCommand(options);
    }

    if (command == "wal") {
        if (argc == 3 && std::string_view(argv[2]) == "--help") {
            PrintWalHelp(std::cout);
            return 0;
        }

        WalCommandOptions options;
        std::string error;
        if (!ParseWalOptions(argc - 2, argv + 2, &options, &error)) {
            if (error == "help") {
                PrintWalHelp(std::cout);
                return 0;
            }
            std::cerr << "haruhidb_inspect wal: " << error << "\n\n";
            PrintWalHelp(std::cerr);
            return 1;
        }
        return RunWalCommand(options);
    }

    std::cerr << "haruhidb_inspect: unknown command: " << command << "\n\n";
    PrintTopHelp(std::cerr);
    return 1;
}
