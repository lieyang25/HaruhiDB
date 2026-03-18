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
 *
 *
 * ========================= 核心流程 =========================
 *
 * CreateTable:
 *   校验表名
 *   检查重名
 *   创建 TableHeap
 *   分配 table oid
 *   构造 TableInfo
 *   写入 name_to_oid_ 与 tables_
 *
 * CreateIndex:
 *   定位目标表
 *   分配 index oid
 *   委托 TableInfo 创建索引
 *
 * LoadIndex:
 *   定位目标表
 *   委托 TableInfo 加载已有索引
 *   必要时推进 next_index_oid_
 */

#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
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
    } // namespace

    /**
     * @param bpm 底层缓冲池管理器
     */
    Catalog::Catalog(buffer::BufferPoolManager* bpm)
        : bpm_(bpm)
    {
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
        return it->second->CreateIndex(index_oid, std::move(index_name), bpm_);
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