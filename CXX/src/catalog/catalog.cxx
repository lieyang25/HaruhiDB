/**
 * CXX/src/catalog/catalog.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 Catalog 的对象目录管理逻辑。
 *
 * 主要完成：
 *
 * 1. 表名归一化
 * 2. 创建表
 * 3. 按名字 / oid 查表
 * 4. 枚举所有表
 * 5. 创建索引
 * 6. 加载已有索引
 * 7. table oid / index oid 分配
 * 8. catalog 元数据持久化与恢复
 */

#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
        constexpr uint32_t CATALOG_META_MAGIC = 0x4341544DU;         // "CATM"
        constexpr uint32_t CATALOG_META_VERSION = 1U;
        constexpr uint32_t CATALOG_META_PAGE_MAGIC = 0x434D4554U;    // "CMET"
        constexpr uint32_t CATALOG_META_PAGE_VERSION = 1U;

        struct CatalogMetaPageOpaque
        {
            uint32_t magic;
            uint32_t version;
            page_id_t next_page_id;
            uint32_t payload_size;
        };

        static_assert(sizeof(CatalogMetaPageOpaque) == storage::PAGE_HEADER_OPAQUE_SIZE);

        std::string NormalizeTableName(std::string_view table_name)
        {
            std::string normalized(table_name);
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return normalized;
        }

        void BestEffortRecycleNewTableHeap(
            buffer::BufferPoolManager* bpm, std::unique_ptr<table::TableHeap>& table_heap)
        {
            if (bpm == nullptr || table_heap == nullptr) {
                return;
            }

            const page_id_t first_page_id = table_heap->FirstPageId();
            table_heap.reset();

            if (first_page_id != INVALID_PAGE_ID) {
                (void)bpm->DeletePage(first_page_id);
            }
        }

        class ByteWriter
        {
        public:
            template <typename T>
            void WritePod(T value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                const auto old_size = bytes_.size();
                bytes_.resize(old_size + sizeof(T));
                std::memcpy(bytes_.data() + old_size, &value, sizeof(T));
            }

            void WriteBytes(std::span<const std::byte> bytes)
            {
                bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
            }

            void WriteString(const std::string& text)
            {
                WritePod<uint32_t>(static_cast<uint32_t>(text.size()));
                const auto* ptr = reinterpret_cast<const std::byte*>(text.data());
                WriteBytes(std::span<const std::byte>(ptr, text.size()));
            }

            const std::vector<std::byte>& Bytes() const noexcept { return bytes_; }

        private:
            std::vector<std::byte> bytes_;
        };

        class ByteReader
        {
        public:
            explicit ByteReader(std::span<const std::byte> bytes)
                : bytes_(bytes)
            {
            }

            template <typename T>
            std::expected<T, std::string> ReadPod()
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (offset_ + sizeof(T) > bytes_.size()) {
                    return std::unexpected("Catalog meta: payload truncated");
                }

                T value{};
                std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
                offset_ += sizeof(T);
                return value;
            }

            std::expected<std::span<const std::byte>, std::string> ReadBytes(size_t len)
            {
                if (offset_ + len > bytes_.size()) {
                    return std::unexpected("Catalog meta: payload truncated");
                }

                auto out = bytes_.subspan(offset_, len);
                offset_ += len;
                return out;
            }

            std::expected<std::string, std::string> ReadString()
            {
                auto len_exp = ReadPod<uint32_t>();
                if (!len_exp.has_value()) {
                    return std::unexpected(len_exp.error());
                }

                const uint32_t len = len_exp.value();
                auto bytes_exp = ReadBytes(len);
                if (!bytes_exp.has_value()) {
                    return std::unexpected(bytes_exp.error());
                }

                const auto bytes = bytes_exp.value();
                return std::string(
                    reinterpret_cast<const char*>(bytes.data()),
                    reinterpret_cast<const char*>(bytes.data()) + bytes.size());
            }

            bool Exhausted() const noexcept
            {
                return offset_ == bytes_.size();
            }

        private:
            std::span<const std::byte> bytes_;
            size_t offset_{0};
        };

        std::expected<std::vector<std::byte>, std::string> SerializeSchema(const Schema& schema)
        {
            ByteWriter writer;
            writer.WritePod<uint32_t>(static_cast<uint32_t>(schema.ColumnCount()));

            for (const auto& column : schema.Columns()) {
                writer.WriteString(column.Name());
                writer.WritePod<uint8_t>(static_cast<uint8_t>(column.Type()));
                writer.WritePod<uint8_t>(column.Nullable() ? 1U : 0U);

                const bool has_default = column.HasDefaultValue();
                const bool default_is_null =
                    has_default && column.DefaultValue().has_value() && column.DefaultValue()->IsNull();
                writer.WritePod<uint8_t>(has_default ? 1U : 0U);
                writer.WritePod<uint8_t>(default_is_null ? 1U : 0U);
                writer.WritePod<uint32_t>(column.Length());

                uint32_t default_payload_size = 0;
                std::vector<std::byte> default_payload;
                if (has_default && !default_is_null) {
                    try {
                        default_payload = column.DefaultValue()->Serialize(column.Type());
                    } catch (const std::exception& e) {
                        return std::unexpected(
                            "Catalog meta: failed to serialize default value for column '" +
                            column.Name() + "': " + e.what());
                    }

                    if (default_payload.size() > std::numeric_limits<uint32_t>::max()) {
                        return std::unexpected("Catalog meta: default value payload too large");
                    }
                    default_payload_size = static_cast<uint32_t>(default_payload.size());
                }

                writer.WritePod<uint32_t>(default_payload_size);
                if (!default_payload.empty()) {
                    writer.WriteBytes(default_payload);
                }
            }

            return writer.Bytes();
        }

        std::expected<Schema, std::string> DeserializeSchema(std::span<const std::byte> payload)
        {
            ByteReader reader(payload);

            auto column_count_exp = reader.ReadPod<uint32_t>();
            if (!column_count_exp.has_value()) {
                return std::unexpected(column_count_exp.error());
            }

            const uint32_t column_count = column_count_exp.value();
            std::vector<Column> columns;
            columns.reserve(column_count);

            for (uint32_t i = 0; i < column_count; ++i) {
                auto name_exp = reader.ReadString();
                if (!name_exp.has_value()) {
                    return std::unexpected(name_exp.error());
                }

                auto type_raw_exp = reader.ReadPod<uint8_t>();
                auto nullable_exp = reader.ReadPod<uint8_t>();
                auto has_default_exp = reader.ReadPod<uint8_t>();
                auto default_is_null_exp = reader.ReadPod<uint8_t>();
                auto length_exp = reader.ReadPod<uint32_t>();
                auto default_len_exp = reader.ReadPod<uint32_t>();
                if (!type_raw_exp.has_value() ||
                    !nullable_exp.has_value() ||
                    !has_default_exp.has_value() ||
                    !default_is_null_exp.has_value() ||
                    !length_exp.has_value() ||
                    !default_len_exp.has_value()) {
                    return std::unexpected("Catalog meta: invalid schema column payload");
                }

                const auto type_raw = static_cast<type::TypeId>(type_raw_exp.value());
                if (!type::TypeUtil::IsValid(type_raw) || type_raw == type::TypeId::INVALID) {
                    return std::unexpected("Catalog meta: invalid column type id");
                }

                const bool nullable = nullable_exp.value() != 0;
                const bool has_default = has_default_exp.value() != 0;
                const bool default_is_null = default_is_null_exp.value() != 0;
                const uint32_t length = length_exp.value();
                const uint32_t default_len = default_len_exp.value();

                std::optional<type::Value> default_value;
                if (has_default) {
                    if (default_is_null) {
                        if (default_len != 0) {
                            return std::unexpected("Catalog meta: NULL default should not have payload");
                        }
                        default_value = type::Value::Null();
                    } else {
                        auto default_bytes_exp = reader.ReadBytes(default_len);
                        if (!default_bytes_exp.has_value()) {
                            return std::unexpected(default_bytes_exp.error());
                        }

                        const auto default_bytes = default_bytes_exp.value();
                        auto value_exp = type::Value::TryDeserialize(
                            type_raw,
                            default_bytes.data(),
                            default_bytes.size());
                        if (!value_exp.has_value()) {
                            return std::unexpected(
                                "Catalog meta: default value deserialize failed: " + value_exp.error().msg);
                        }
                        default_value = value_exp.value();
                    }
                } else if (default_len != 0) {
                    return std::unexpected("Catalog meta: default payload exists but default flag is unset");
                }

                try {
                    if (type::TypeUtil::IsVariableLength(type_raw)) {
                        columns.emplace_back(
                            std::move(name_exp.value()),
                            type_raw,
                            length,
                            nullable,
                            std::move(default_value));
                    } else {
                        const int fixed_size = type::TypeUtil::FixedLengthSize(type_raw);
                        if (fixed_size <= 0 || length != static_cast<uint32_t>(fixed_size)) {
                            return std::unexpected("Catalog meta: fixed-length column size mismatch");
                        }
                        columns.emplace_back(
                            std::move(name_exp.value()),
                            type_raw,
                            nullable,
                            std::move(default_value));
                    }
                } catch (const std::exception& e) {
                    return std::unexpected(
                        "Catalog meta: failed to construct column: " + std::string(e.what()));
                }
            }

            if (!reader.Exhausted()) {
                return std::unexpected("Catalog meta: schema payload has trailing bytes");
            }

            auto schema_exp = Schema::Create(std::move(columns));
            if (!schema_exp.has_value()) {
                return std::unexpected("Catalog meta: invalid schema: " + schema_exp.error());
            }
            return std::move(schema_exp.value());
        }
    } // namespace

    /**
     * @param bpm 底层缓冲池管理器
     */
    Catalog::Catalog(buffer::BufferPoolManager* bpm)
        : bpm_(bpm)
    {
        auto loaded = LoadCatalogMeta();
        if (!loaded.has_value()) {
            throw std::runtime_error("Catalog: failed to load persisted metadata: " + loaded.error());
        }
    }

    /**
     * @param table_name 表名
     * @param schema     表结构
     */
    std::expected<TableInfo*, std::string> Catalog::CreateTable(
        std::string table_name, const Schema& schema)
    {
        // step 1: 先校验表名。
        auto validated = ValidateTableName(table_name);
        if (!validated.has_value()) {
            return std::unexpected(validated.error());
        }

        const std::string normalized_name = NormalizeTableName(table_name);

        // step 2: 先用读锁快速检查是否重名。
        {
            std::shared_lock lock(latch_);
            if (name_to_oid_.contains(normalized_name)) {
                return std::unexpected("Catalog: table already exists: " + table_name);
            }
        }

        // step 3: 先在锁外创建 TableHeap，避免长时间占用目录写锁。
        auto table_heap_exp = CreateTableHeap();
        if (!table_heap_exp.has_value()) {
            return std::unexpected(table_heap_exp.error());
        }
        std::unique_ptr<table::TableHeap> table_heap = std::move(table_heap_exp.value());

        // step 4: 加写锁后再次检查重名，防止并发竞争。
        std::unique_lock lock(latch_);
        if (name_to_oid_.contains(normalized_name)) {
            BestEffortRecycleNewTableHeap(bpm_, table_heap);
            return std::unexpected("Catalog: table already exists: " + table_name);
        }

        // step 5: 分配 oid，构造 TableInfo，并登记到目录。
        const table_oid_t table_oid = AllocateTableOid();
        auto table_info = std::make_unique<TableInfo>(
            table_oid, std::move(table_name), schema, std::move(table_heap));

        TableInfo* table_info_ptr = table_info.get();
        name_to_oid_[normalized_name] = table_oid;
        tables_[table_oid] = std::move(table_info);

        // step 6: 持久化 catalog 目录。
        auto persisted = PersistCatalogMetaLocked();
        if (!persisted.has_value()) {
            auto it = tables_.find(table_oid);
            if (it != tables_.end()) {
                auto failed_table = std::move(it->second);
                tables_.erase(it);
                name_to_oid_.erase(normalized_name);
                if (failed_table != nullptr && failed_table->GetTableHeap() != nullptr) {
                    const page_id_t first_page_id = failed_table->GetTableHeap()->FirstPageId();
                    if (first_page_id != INVALID_PAGE_ID) {
                        (void)bpm_->DeletePage(first_page_id);
                    }
                }
            }
            return std::unexpected("Catalog::CreateTable: persist catalog meta failed: " + persisted.error());
        }

        return table_info_ptr;
    }

    TableInfo* Catalog::GetTable(std::string_view table_name) noexcept
    {
        std::shared_lock lock(latch_);

        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return nullptr;
        }

        const auto table_it = tables_.find(name_it->second);
        return table_it == tables_.end() ? nullptr : table_it->second.get();
    }

    const TableInfo* Catalog::GetTable(std::string_view table_name) const noexcept
    {
        std::shared_lock lock(latch_);

        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return nullptr;
        }

        const auto table_it = tables_.find(name_it->second);
        return table_it == tables_.end() ? nullptr : table_it->second.get();
    }

    TableInfo* Catalog::GetTable(table_oid_t table_oid) noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    const TableInfo* Catalog::GetTable(table_oid_t table_oid) const noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    bool Catalog::HasTable(std::string_view table_name) const noexcept
    {
        std::shared_lock lock(latch_);
        return name_to_oid_.contains(NormalizeTableName(table_name));
    }

    std::vector<TableInfo*> Catalog::GetAllTables()
    {
        std::shared_lock lock(latch_);
        std::vector<TableInfo*> table_infos;
        table_infos.reserve(tables_.size());

        for (const auto& [oid, table_info] : tables_) {
            (void)oid;
            table_infos.push_back(table_info.get());
        }

        std::ranges::sort(table_infos, [](const TableInfo* lhs, const TableInfo* rhs) {
            return lhs->Oid() < rhs->Oid();
        });
        return table_infos;
    }

    std::vector<const TableInfo*> Catalog::GetAllTables() const
    {
        std::shared_lock lock(latch_);
        std::vector<const TableInfo*> table_infos;
        table_infos.reserve(tables_.size());

        for (const auto& [oid, table_info] : tables_) {
            (void)oid;
            table_infos.push_back(table_info.get());
        }

        std::ranges::sort(table_infos, [](const TableInfo* lhs, const TableInfo* rhs) {
            return lhs->Oid() < rhs->Oid();
        });
        return table_infos;
    }

    size_t Catalog::TableCount() const noexcept
    {
        std::shared_lock lock(latch_);
        return tables_.size();
    }

    table_oid_t Catalog::NextTableOid() const noexcept
    {
        std::shared_lock lock(latch_);
        return next_table_oid_;
    }

    /**
     * @param table_oid   表 oid
     * @param index_name  索引名
     */
    std::expected<storage::BPlusTree*, std::string> Catalog::CreateIndex(
        table_oid_t table_oid, std::string index_name)
    {
        // step 1: 定位目标表。
        std::unique_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        if (it == tables_.end() || it->second == nullptr) {
            return std::unexpected("Catalog::CreateIndex: table oid not found");
        }

        // step 2: 分配新的 index oid，并委托 TableInfo 创建索引。
        const index_oid_t index_oid = AllocateIndexOid();
        auto created = it->second->CreateIndex(index_oid, std::move(index_name), bpm_);
        if (!created.has_value()) {
            return std::unexpected(created.error());
        }

        // step 3: 持久化 catalog 目录。
        auto persisted = PersistCatalogMetaLocked();
        if (!persisted.has_value()) {
            return std::unexpected("Catalog::CreateIndex: persist catalog meta failed: " + persisted.error());
        }

        return created;
    }

    /**
     * @param table_name  表名
     * @param index_name  索引名
     */
    std::expected<storage::BPlusTree*, std::string> Catalog::CreateIndex(
        std::string_view table_name, std::string index_name)
    {
        // step 1: 先由表名查到 table oid。
        std::shared_lock lock(latch_);
        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return std::unexpected("Catalog::CreateIndex: table not found");
        }
        const table_oid_t table_oid = name_it->second;
        lock.unlock();

        // step 2: 复用按 oid 的创建逻辑。
        return CreateIndex(table_oid, std::move(index_name));
    }

    /**
     * @param table_oid       表 oid
     * @param index_oid       索引 oid
     * @param index_name      索引名
     * @param header_page_id  索引 header page
     */
    std::expected<storage::BPlusTree*, std::string> Catalog::LoadIndex(
        table_oid_t table_oid,
        index_oid_t index_oid,
        std::string index_name,
        page_id_t header_page_id)
    {
        // step 1: 定位目标表。
        std::unique_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        if (it == tables_.end() || it->second == nullptr) {
            return std::unexpected("Catalog::LoadIndex: table oid not found");
        }

        // step 2: 委托 TableInfo 加载索引。
        auto loaded = it->second->LoadIndex(
            index_oid,
            std::move(index_name),
            header_page_id,
            bpm_);
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }

        // step 3: 维护 next_index_oid_，保证后续分配不会冲突。
        if (index_oid >= next_index_oid_) {
            next_index_oid_ = index_oid + 1;
        }

        // step 4: 持久化 catalog 目录。
        auto persisted = PersistCatalogMetaLocked();
        if (!persisted.has_value()) {
            return std::unexpected("Catalog::LoadIndex: persist catalog meta failed: " + persisted.error());
        }

        return loaded;
    }

    /**
     * @param table_name      表名
     * @param index_oid       索引 oid
     * @param index_name      索引名
     * @param header_page_id  索引 header page
     */
    std::expected<storage::BPlusTree*, std::string> Catalog::LoadIndex(
        std::string_view table_name,
        index_oid_t index_oid,
        std::string index_name,
        page_id_t header_page_id)
    {
        // step 1: 先由表名查到 table oid。
        std::shared_lock lock(latch_);
        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return std::unexpected("Catalog::LoadIndex: table not found");
        }
        const table_oid_t table_oid = name_it->second;
        lock.unlock();

        // step 2: 复用按 oid 的加载逻辑。
        return LoadIndex(table_oid, index_oid, std::move(index_name), header_page_id);
    }

    storage::BPlusTree* Catalog::GetIndex(table_oid_t table_oid, index_oid_t index_oid) noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        if (it == tables_.end() || it->second == nullptr) {
            return nullptr;
        }
        return it->second->GetIndex(index_oid);
    }

    const storage::BPlusTree* Catalog::GetIndex(table_oid_t table_oid, index_oid_t index_oid) const noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        if (it == tables_.end() || it->second == nullptr) {
            return nullptr;
        }
        return it->second->GetIndex(index_oid);
    }

    index_oid_t Catalog::NextIndexOid() const noexcept
    {
        std::shared_lock lock(latch_);
        return next_index_oid_;
    }

    std::expected<void, std::string> Catalog::LoadCatalogMeta()
    {
        if (bpm_ == nullptr) {
            return {};
        }

        auto* disk_manager = bpm_->GetDiskManager();
        if (disk_manager == nullptr) {
            return std::unexpected("Catalog::LoadCatalogMeta: disk manager is null");
        }

        const page_id_t catalog_meta_page_id = disk_manager->CatalogMetaPageId();
        if (catalog_meta_page_id == INVALID_PAGE_ID) {
            return {};
        }

        auto payload_exp = ReadCatalogMetaPayload(catalog_meta_page_id);
        if (!payload_exp.has_value()) {
            return std::unexpected(payload_exp.error());
        }

        auto snapshot_exp = DeserializeCatalogMeta(payload_exp.value());
        if (!snapshot_exp.has_value()) {
            return std::unexpected(snapshot_exp.error());
        }

        std::unique_lock lock(latch_);
        return BuildRuntimeFromMetaLocked(snapshot_exp.value());
    }

    std::expected<void, std::string> Catalog::PersistCatalogMetaLocked()
    {
        auto snapshot = BuildMetaSnapshotLocked();

        auto serialized_exp = SerializeCatalogMeta(snapshot);
        if (!serialized_exp.has_value()) {
            return std::unexpected(serialized_exp.error());
        }

        return WriteCatalogMetaPayloadLocked(serialized_exp.value());
    }

    std::expected<std::vector<std::byte>, std::string> Catalog::ReadCatalogMetaPayload(
        page_id_t catalog_meta_page_id) const
    {
        if (bpm_ == nullptr) {
            return std::unexpected("Catalog::ReadCatalogMetaPayload: buffer pool manager is null");
        }
        if (catalog_meta_page_id == INVALID_PAGE_ID || catalog_meta_page_id == 0) {
            return std::unexpected("Catalog::ReadCatalogMetaPayload: invalid catalog meta page id");
        }

        std::vector<std::byte> payload;
        std::unordered_set<page_id_t> visited;

        page_id_t current_page_id = catalog_meta_page_id;
        while (current_page_id != INVALID_PAGE_ID) {
            if (!visited.insert(current_page_id).second) {
                return std::unexpected("Catalog::ReadCatalogMetaPayload: detected catalog meta page cycle");
            }

            auto page_exp = bpm_->FetchPage(current_page_id);
            if (!page_exp.has_value()) {
                return std::unexpected(
                    "Catalog::ReadCatalogMetaPayload: fetch page failed: " + page_exp.error().msg);
            }

            storage::Page* page = page_exp.value();
            page->RLock();

            const auto* persistent = page->Header();
            if (persistent->page_type != storage::PageType::HEADER) {
                page->RUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return std::unexpected("Catalog::ReadCatalogMetaPayload: page type is not HEADER");
            }

            CatalogMetaPageOpaque opaque{};
            std::memcpy(&opaque, persistent->opaque, sizeof(opaque));
            if (opaque.magic != CATALOG_META_PAGE_MAGIC ||
                opaque.version != CATALOG_META_PAGE_VERSION) {
                page->RUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return std::unexpected("Catalog::ReadCatalogMetaPayload: meta page magic/version mismatch");
            }

            const size_t max_payload_size = PAGE_SIZE - HEADER_SIZE;
            if (opaque.payload_size > max_payload_size) {
                page->RUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return std::unexpected("Catalog::ReadCatalogMetaPayload: payload_size exceeds page capacity");
            }

            const std::byte* page_payload = page->RawData() + HEADER_SIZE;
            payload.insert(
                payload.end(),
                page_payload,
                page_payload + static_cast<size_t>(opaque.payload_size));

            const page_id_t next_page_id = opaque.next_page_id;

            page->RUnLock();
            if (!bpm_->UnpinPage(current_page_id, false)) {
                return std::unexpected("Catalog::ReadCatalogMetaPayload: unpin page failed");
            }

            current_page_id = next_page_id;
        }

        return payload;
    }

    std::expected<void, std::string> Catalog::WriteCatalogMetaPayloadLocked(
        std::span<const std::byte> payload)
    {
        auto entry_page_exp = EnsureCatalogMetaEntryPageLocked();
        if (!entry_page_exp.has_value()) {
            return std::unexpected(entry_page_exp.error());
        }

        const page_id_t entry_page_id = entry_page_exp.value();
        const size_t page_payload_capacity = PAGE_SIZE - HEADER_SIZE;
        const size_t required_page_count = std::max<size_t>(
            1,
            (payload.size() + page_payload_capacity - 1) / page_payload_capacity);

        // step 1: 读取当前 catalog 元数据页链。
        std::vector<page_id_t> chain;
        std::unordered_set<page_id_t> visited;
        page_id_t current_page_id = entry_page_id;
        while (current_page_id != INVALID_PAGE_ID) {
            if (!visited.insert(current_page_id).second) {
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: detected catalog meta page cycle");
            }
            chain.push_back(current_page_id);

            auto page_exp = bpm_->FetchPage(current_page_id);
            if (!page_exp.has_value()) {
                return std::unexpected(
                    "Catalog::WriteCatalogMetaPayloadLocked: fetch chain page failed: " + page_exp.error().msg);
            }

            storage::Page* page = page_exp.value();
            page->RLock();
            const auto* persistent = page->Header();
            if (persistent->page_type != storage::PageType::HEADER) {
                page->RUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: chain page is not HEADER");
            }

            CatalogMetaPageOpaque opaque{};
            std::memcpy(&opaque, persistent->opaque, sizeof(opaque));
            const page_id_t next_page_id = opaque.next_page_id;
            page->RUnLock();

            if (!bpm_->UnpinPage(current_page_id, false)) {
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: unpin chain page failed");
            }

            current_page_id = next_page_id;
        }

        // step 2: 不足时扩展页链。
        while (chain.size() < required_page_count) {
            page_id_t new_page_id = INVALID_PAGE_ID;
            auto new_page_exp = bpm_->NewPage(&new_page_id, storage::PageType::HEADER);
            if (!new_page_exp.has_value()) {
                return std::unexpected(
                    "Catalog::WriteCatalogMetaPayloadLocked: allocate new meta page failed: " +
                    new_page_exp.error().msg);
            }

            storage::Page* new_page = new_page_exp.value();
            new_page->WLock();
            auto* persistent = new_page->Header();
            persistent->page_type = storage::PageType::HEADER;

            CatalogMetaPageOpaque opaque{
                .magic = CATALOG_META_PAGE_MAGIC,
                .version = CATALOG_META_PAGE_VERSION,
                .next_page_id = INVALID_PAGE_ID,
                .payload_size = 0,
            };
            std::memcpy(persistent->opaque, &opaque, sizeof(opaque));
            std::memset(new_page->RawData() + HEADER_SIZE, 0, page_payload_capacity);
            new_page->MarkDirty();
            new_page->WUnLock();

            if (!bpm_->UnpinPage(new_page_id, true)) {
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: unpin new meta page failed");
            }

            chain.push_back(new_page_id);
        }

        // step 3: 写入 payload 到所需页，并重新串联 next 指针。
        size_t payload_offset = 0;
        for (size_t i = 0; i < required_page_count; ++i) {
            const page_id_t page_id = chain[i];
            const page_id_t next_page_id = (i + 1 < required_page_count)
                ? chain[i + 1]
                : INVALID_PAGE_ID;

            const size_t remain = payload.size() - payload_offset;
            const size_t chunk_size = std::min(remain, page_payload_capacity);

            auto page_exp = bpm_->FetchPage(page_id);
            if (!page_exp.has_value()) {
                return std::unexpected(
                    "Catalog::WriteCatalogMetaPayloadLocked: fetch writable meta page failed: " +
                    page_exp.error().msg);
            }

            storage::Page* page = page_exp.value();
            page->WLock();
            auto* persistent = page->Header();
            persistent->page_type = storage::PageType::HEADER;

            CatalogMetaPageOpaque opaque{
                .magic = CATALOG_META_PAGE_MAGIC,
                .version = CATALOG_META_PAGE_VERSION,
                .next_page_id = next_page_id,
                .payload_size = static_cast<uint32_t>(chunk_size),
            };
            std::memcpy(persistent->opaque, &opaque, sizeof(opaque));

            std::byte* page_payload = page->RawData() + HEADER_SIZE;
            std::memset(page_payload, 0, page_payload_capacity);
            if (chunk_size > 0) {
                std::memcpy(page_payload, payload.data() + payload_offset, chunk_size);
                payload_offset += chunk_size;
            }

            page->MarkDirty();
            page->WUnLock();

            if (!bpm_->UnpinPage(page_id, true)) {
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: unpin writable meta page failed");
            }
            auto flush_exp = bpm_->FlushPage(page_id);
            if (!flush_exp.has_value()) {
                return std::unexpected(
                    "Catalog::WriteCatalogMetaPayloadLocked: flush meta page failed: " + flush_exp.error().msg);
            }
        }

        // step 4: 删除多余旧页。
        for (size_t i = required_page_count; i < chain.size(); ++i) {
            if (!bpm_->DeletePage(chain[i])) {
                return std::unexpected("Catalog::WriteCatalogMetaPayloadLocked: delete extra meta page failed");
            }
        }

        return {};
    }

    std::expected<page_id_t, std::string> Catalog::EnsureCatalogMetaEntryPageLocked()
    {
        if (bpm_ == nullptr) {
            return std::unexpected("Catalog::EnsureCatalogMetaEntryPageLocked: buffer pool manager is null");
        }

        auto* disk_manager = bpm_->GetDiskManager();
        if (disk_manager == nullptr) {
            return std::unexpected("Catalog::EnsureCatalogMetaEntryPageLocked: disk manager is null");
        }

        page_id_t catalog_meta_page_id = disk_manager->CatalogMetaPageId();
        if (catalog_meta_page_id != INVALID_PAGE_ID) {
            return catalog_meta_page_id;
        }

        page_id_t new_page_id = INVALID_PAGE_ID;
        auto page_exp = bpm_->NewPage(&new_page_id, storage::PageType::HEADER);
        if (!page_exp.has_value()) {
            return std::unexpected(
                "Catalog::EnsureCatalogMetaEntryPageLocked: allocate entry page failed: " + page_exp.error().msg);
        }

        storage::Page* page = page_exp.value();
        page->WLock();
        auto* persistent = page->Header();
        persistent->page_type = storage::PageType::HEADER;

        CatalogMetaPageOpaque opaque{
            .magic = CATALOG_META_PAGE_MAGIC,
            .version = CATALOG_META_PAGE_VERSION,
            .next_page_id = INVALID_PAGE_ID,
            .payload_size = 0,
        };
        std::memcpy(persistent->opaque, &opaque, sizeof(opaque));
        std::memset(page->RawData() + HEADER_SIZE, 0, PAGE_SIZE - HEADER_SIZE);
        page->MarkDirty();
        page->WUnLock();

        if (!bpm_->UnpinPage(new_page_id, true)) {
            return std::unexpected("Catalog::EnsureCatalogMetaEntryPageLocked: unpin entry page failed");
        }

        auto flush_page_exp = bpm_->FlushPage(new_page_id);
        if (!flush_page_exp.has_value()) {
            return std::unexpected(
                "Catalog::EnsureCatalogMetaEntryPageLocked: flush entry page failed: " + flush_page_exp.error().msg);
        }

        auto set_header_exp = disk_manager->SetCatalogMetaPageId(new_page_id);
        if (!set_header_exp.has_value()) {
            return std::unexpected(
                "Catalog::EnsureCatalogMetaEntryPageLocked: persist header pointer failed: " +
                set_header_exp.error().msg);
        }

        auto flush_header_exp = disk_manager->Flush();
        if (!flush_header_exp.has_value()) {
            return std::unexpected(
                "Catalog::EnsureCatalogMetaEntryPageLocked: flush header failed: " +
                flush_header_exp.error().msg);
        }

        return new_page_id;
    }

    Catalog::CatalogMetaSnapshot Catalog::BuildMetaSnapshotLocked() const
    {
        CatalogMetaSnapshot snapshot;
        snapshot.next_table_oid = next_table_oid_;
        snapshot.next_index_oid = next_index_oid_;

        std::vector<TableInfo*> table_infos;
        table_infos.reserve(tables_.size());
        for (const auto& [table_oid, table_info] : tables_) {
            (void)table_oid;
            if (table_info != nullptr) {
                table_infos.push_back(table_info.get());
            }
        }
        std::ranges::sort(table_infos, [](const TableInfo* lhs, const TableInfo* rhs) {
            return lhs->Oid() < rhs->Oid();
        });

        snapshot.tables.reserve(table_infos.size());
        for (const TableInfo* table_info : table_infos) {
            const page_id_t first_page_id =
                table_info->GetTableHeap() != nullptr
                    ? table_info->GetTableHeap()->FirstPageId()
                    : INVALID_PAGE_ID;

            snapshot.tables.push_back(TableMeta{
                .table_oid = table_info->Oid(),
                .table_name = table_info->Name(),
                .schema = table_info->GetSchema(),
                .first_page_id = first_page_id,
            });

            std::vector<const TableInfo::IndexEntry*> index_entries;
            index_entries.reserve(table_info->IndexEntries().size());
            for (const auto& entry : table_info->IndexEntries()) {
                index_entries.push_back(&entry);
            }
            std::ranges::sort(index_entries, [](const TableInfo::IndexEntry* lhs, const TableInfo::IndexEntry* rhs) {
                return lhs->index_oid < rhs->index_oid;
            });

            for (const auto* entry : index_entries) {
                snapshot.indexes.push_back(IndexMeta{
                    .index_oid = entry->index_oid,
                    .table_oid = table_info->Oid(),
                    .index_name = entry->index_name,
                    .header_page_id = entry->header_page_id,
                });
            }
        }

        return snapshot;
    }

    std::expected<std::vector<std::byte>, std::string> Catalog::SerializeCatalogMeta(
        const CatalogMetaSnapshot& snapshot) const
    {
        ByteWriter writer;
        writer.WritePod<uint32_t>(CATALOG_META_MAGIC);
        writer.WritePod<uint32_t>(CATALOG_META_VERSION);
        writer.WritePod<table_oid_t>(snapshot.next_table_oid);
        writer.WritePod<index_oid_t>(snapshot.next_index_oid);
        writer.WritePod<uint32_t>(static_cast<uint32_t>(snapshot.tables.size()));
        writer.WritePod<uint32_t>(static_cast<uint32_t>(snapshot.indexes.size()));

        for (const auto& table_meta : snapshot.tables) {
            writer.WritePod<table_oid_t>(table_meta.table_oid);
            writer.WritePod<page_id_t>(table_meta.first_page_id);
            writer.WriteString(table_meta.table_name);

            auto schema_bytes_exp = SerializeSchema(table_meta.schema);
            if (!schema_bytes_exp.has_value()) {
                return std::unexpected(schema_bytes_exp.error());
            }

            const auto& schema_bytes = schema_bytes_exp.value();
            writer.WritePod<uint32_t>(static_cast<uint32_t>(schema_bytes.size()));
            writer.WriteBytes(schema_bytes);
        }

        for (const auto& index_meta : snapshot.indexes) {
            writer.WritePod<index_oid_t>(index_meta.index_oid);
            writer.WritePod<table_oid_t>(index_meta.table_oid);
            writer.WritePod<page_id_t>(index_meta.header_page_id);
            writer.WriteString(index_meta.index_name);
        }

        return writer.Bytes();
    }

    std::expected<Catalog::CatalogMetaSnapshot, std::string> Catalog::DeserializeCatalogMeta(
        std::span<const std::byte> payload) const
    {
        ByteReader reader(payload);

        auto magic_exp = reader.ReadPod<uint32_t>();
        auto version_exp = reader.ReadPod<uint32_t>();
        auto next_table_oid_exp = reader.ReadPod<table_oid_t>();
        auto next_index_oid_exp = reader.ReadPod<index_oid_t>();
        auto table_count_exp = reader.ReadPod<uint32_t>();
        auto index_count_exp = reader.ReadPod<uint32_t>();

        if (!magic_exp.has_value() ||
            !version_exp.has_value() ||
            !next_table_oid_exp.has_value() ||
            !next_index_oid_exp.has_value() ||
            !table_count_exp.has_value() ||
            !index_count_exp.has_value()) {
            return std::unexpected("Catalog meta: malformed header");
        }

        if (magic_exp.value() != CATALOG_META_MAGIC ||
            version_exp.value() != CATALOG_META_VERSION) {
            return std::unexpected("Catalog meta: magic/version mismatch");
        }

        CatalogMetaSnapshot snapshot;
        snapshot.next_table_oid = next_table_oid_exp.value();
        snapshot.next_index_oid = next_index_oid_exp.value();

        const uint32_t table_count = table_count_exp.value();
        const uint32_t index_count = index_count_exp.value();

        snapshot.tables.reserve(table_count);
        for (uint32_t i = 0; i < table_count; ++i) {
            auto table_oid_exp = reader.ReadPod<table_oid_t>();
            auto first_page_id_exp = reader.ReadPod<page_id_t>();
            auto table_name_exp = reader.ReadString();
            auto schema_len_exp = reader.ReadPod<uint32_t>();

            if (!table_oid_exp.has_value() ||
                !first_page_id_exp.has_value() ||
                !table_name_exp.has_value() ||
                !schema_len_exp.has_value()) {
                return std::unexpected("Catalog meta: malformed table meta");
            }

            const uint32_t schema_len = schema_len_exp.value();
            auto schema_bytes_exp = reader.ReadBytes(schema_len);
            if (!schema_bytes_exp.has_value()) {
                return std::unexpected(schema_bytes_exp.error());
            }

            auto schema_exp = DeserializeSchema(schema_bytes_exp.value());
            if (!schema_exp.has_value()) {
                return std::unexpected(schema_exp.error());
            }

            snapshot.tables.push_back(TableMeta{
                .table_oid = table_oid_exp.value(),
                .table_name = std::move(table_name_exp.value()),
                .schema = std::move(schema_exp.value()),
                .first_page_id = first_page_id_exp.value(),
            });
        }

        snapshot.indexes.reserve(index_count);
        for (uint32_t i = 0; i < index_count; ++i) {
            auto index_oid_exp = reader.ReadPod<index_oid_t>();
            auto table_oid_exp = reader.ReadPod<table_oid_t>();
            auto header_page_id_exp = reader.ReadPod<page_id_t>();
            auto index_name_exp = reader.ReadString();

            if (!index_oid_exp.has_value() ||
                !table_oid_exp.has_value() ||
                !header_page_id_exp.has_value() ||
                !index_name_exp.has_value()) {
                return std::unexpected("Catalog meta: malformed index meta");
            }

            snapshot.indexes.push_back(IndexMeta{
                .index_oid = index_oid_exp.value(),
                .table_oid = table_oid_exp.value(),
                .index_name = std::move(index_name_exp.value()),
                .header_page_id = header_page_id_exp.value(),
            });
        }

        if (!reader.Exhausted()) {
            return std::unexpected("Catalog meta: payload has trailing bytes");
        }

        return snapshot;
    }

    std::expected<void, std::string> Catalog::BuildRuntimeFromMetaLocked(
        const CatalogMetaSnapshot& snapshot)
    {
        name_to_oid_.clear();
        tables_.clear();
        next_table_oid_ = 0;
        next_index_oid_ = 0;

        table_oid_t max_table_oid = 0;
        bool has_table = false;

        for (const auto& table_meta : snapshot.tables) {
            if (table_meta.table_name.empty()) {
                return std::unexpected("Catalog meta: empty table name");
            }
            if (table_meta.first_page_id == INVALID_PAGE_ID || table_meta.first_page_id == 0) {
                return std::unexpected("Catalog meta: invalid table first_page_id");
            }

            const std::string normalized_name = NormalizeTableName(table_meta.table_name);
            if (name_to_oid_.contains(normalized_name)) {
                return std::unexpected("Catalog meta: duplicated table name: " + table_meta.table_name);
            }
            if (tables_.contains(table_meta.table_oid)) {
                return std::unexpected("Catalog meta: duplicated table oid");
            }

            auto table_heap = std::make_unique<table::TableHeap>(bpm_, table_meta.first_page_id);
            auto table_info = std::make_unique<TableInfo>(
                table_meta.table_oid,
                table_meta.table_name,
                table_meta.schema,
                std::move(table_heap));

            name_to_oid_[normalized_name] = table_meta.table_oid;
            tables_[table_meta.table_oid] = std::move(table_info);

            if (!has_table || table_meta.table_oid > max_table_oid) {
                max_table_oid = table_meta.table_oid;
                has_table = true;
            }
        }

        index_oid_t max_index_oid = 0;
        bool has_index = false;

        for (const auto& index_meta : snapshot.indexes) {
            const auto it = tables_.find(index_meta.table_oid);
            if (it == tables_.end() || it->second == nullptr) {
                return std::unexpected("Catalog meta: index references unknown table oid");
            }
            if (index_meta.header_page_id == INVALID_PAGE_ID || index_meta.header_page_id == 0) {
                return std::unexpected("Catalog meta: invalid index header_page_id");
            }

            auto loaded = it->second->LoadIndex(
                index_meta.index_oid,
                index_meta.index_name,
                index_meta.header_page_id,
                bpm_);
            if (!loaded.has_value()) {
                return std::unexpected(
                    "Catalog meta: failed to load index '" + index_meta.index_name + "': " + loaded.error());
            }

            if (!has_index || index_meta.index_oid > max_index_oid) {
                max_index_oid = index_meta.index_oid;
                has_index = true;
            }
        }

        next_table_oid_ = snapshot.next_table_oid;
        if (has_table && next_table_oid_ <= max_table_oid) {
            next_table_oid_ = max_table_oid + 1;
        }

        next_index_oid_ = snapshot.next_index_oid;
        if (has_index && next_index_oid_ <= max_index_oid) {
            next_index_oid_ = max_index_oid + 1;
        }

        return {};
    }

    std::expected<std::unique_ptr<table::TableHeap>, std::string> Catalog::CreateTableHeap()
    {
        return table::TableHeap::Create(bpm_);
    }

    table_oid_t Catalog::AllocateTableOid() noexcept
    {
        return next_table_oid_++;
    }

    index_oid_t Catalog::AllocateIndexOid() noexcept
    {
        return next_index_oid_++;
    }

    /**
     * @param table_name 表名
     */
    std::expected<void, std::string> Catalog::ValidateTableName(std::string_view table_name)
    {
        // step 1: 不能为空。
        if (table_name.empty()) {
            return std::unexpected("Catalog: table name must not be empty");
        }

        // step 2: 不能全为空白。
        const bool all_space = std::ranges::all_of(table_name, [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
        if (all_space) {
            return std::unexpected("Catalog: table name must not be blank");
        }

        // step 3: 不能包含控制字符。
        for (unsigned char ch : table_name) {
            if (std::iscntrl(ch) != 0) {
                return std::unexpected("Catalog: table name must not contain control characters");
            }
        }

        return {};
    }

} // namespace catalog
} // namespace HaruhiDB
