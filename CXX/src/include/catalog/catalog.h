/**
 * CXX/src/include/catalog/catalog.h
 *
 * ========================= 设计目标 =========================
 *
 * Catalog 用于管理数据库中的表对象与索引对象。
 *
 * 它负责把“名字 / oid / 运行时对象”组织起来，
 * 作为数据库对象管理层的统一入口。
 *
 * 核心职责：
 *
 * 1. 创建表
 * 2. 按表名查表
 * 3. 按 table oid 查表
 * 4. 获取所有表
 * 5. 创建索引
 * 6. 加载已有索引
 *
 *
 * ========================= 为什么需要 Catalog =========================
 *
 * 当前系统已经具备：
 *
 * - Schema / Column：描述表结构
 * - TableHeap：组织表数据
 * - BPlusTree：组织索引数据
 *
 * 但还缺少“数据库对象管理层”：
 *
 * - 数据库里有哪些表
 * - 表名对应哪个 TableInfo
 * - table oid 对应哪个对象
 * - 某张表下有哪些索引
 *
 * Catalog 正是这一层。
 *
 *
 * ========================= Catalog 在系统中的位置 =========================
 *
 * Database
 *   └── Catalog
 *         ├── TableInfo(student)
 *         ├── TableInfo(course)
 *         └── TableInfo(score)
 *
 * Catalog 内部维护：
 *
 *   name_to_oid_
 *     table_name -> table_oid
 *
 *   tables_
 *     table_oid -> TableInfo
 *
 *
 * ========================= 当前语义 =========================
 *
 * 当前为最小持久化 Catalog：
 *
 * - 支持表与索引元数据的持久化与重启自动恢复
 * - 持久化入口由数据库头页中的 catalog_meta_page_id 指向
 * - 不做系统表 SQL 自举
 * - 不做复杂 DDL 历史管理
 * - 表与索引对象生命周期由 Catalog 持有
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/table_info.h"
#include "common/config.h"

#include <expected>
#include <memory>
#include <span>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{

    class Catalog
    {
    public:
        /**
         * @param bpm 底层缓冲池管理器
         */
        explicit Catalog(buffer::BufferPoolManager* bpm);

        Catalog(const Catalog&) = delete;
        Catalog& operator=(const Catalog&) = delete;
        Catalog(Catalog&&) = delete;
        Catalog& operator=(Catalog&&) = delete;

        /**
         * 创建表。
         *
         * @param table_name 表名
         * @param schema     表结构
         * @return 成功返回 TableInfo*，失败返回错误信息
         */
        std::expected<TableInfo*, std::string>
        CreateTable(std::string table_name, const Schema& schema);

        /**
         * 按表名查表。
         *
         * @param table_name 表名
         * @return 不存在返回 nullptr
         */
        TableInfo* GetTable(std::string_view table_name) noexcept;

        /**
         * 按表名查表。
         *
         * @param table_name 表名
         * @return 不存在返回 nullptr
         */
        const TableInfo* GetTable(std::string_view table_name) const noexcept;

        /**
         * 按 oid 查表。
         *
         * @param table_oid 表 oid
         * @return 不存在返回 nullptr
         */
        TableInfo* GetTable(table_oid_t table_oid) noexcept;

        /**
         * 按 oid 查表。
         *
         * @param table_oid 表 oid
         * @return 不存在返回 nullptr
         */
        const TableInfo* GetTable(table_oid_t table_oid) const noexcept;

        /**
         * 判断指定表名是否存在。
         *
         * @param table_name 表名
         */
        bool HasTable(std::string_view table_name) const noexcept;

        /**
         * 返回当前所有表。
         */
        std::vector<TableInfo*> GetAllTables();

        /**
         * 返回当前所有表。
         */
        std::vector<const TableInfo*> GetAllTables() const;

        /**
         * 返回当前表数量。
         */
        size_t TableCount() const noexcept;

        /**
         * 返回下一个可分配的 table oid。
         */
        table_oid_t NextTableOid() const noexcept;

        /**
         * 为指定表创建索引。
         *
         * @param table_oid   表 oid
         * @param index_name  索引名
         */
        std::expected<storage::BPlusTree*, std::string>
        CreateIndex(table_oid_t table_oid, std::string index_name);

        /**
         * 为指定表创建索引。
         *
         * @param table_name  表名
         * @param index_name  索引名
         */
        std::expected<storage::BPlusTree*, std::string>
        CreateIndex(std::string_view table_name, std::string index_name);

        /**
         * 加载已有索引。
         *
         * @param table_oid       表 oid
         * @param index_oid       索引 oid
         * @param index_name      索引名
         * @param header_page_id  索引 header page
         */
        std::expected<storage::BPlusTree*, std::string>
        LoadIndex(
            table_oid_t table_oid,
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id);

        /**
         * 加载已有索引。
         *
         * @param table_name      表名
         * @param index_oid       索引 oid
         * @param index_name      索引名
         * @param header_page_id  索引 header page
         */
        std::expected<storage::BPlusTree*, std::string>
        LoadIndex(
            std::string_view table_name,
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id);

        /**
         * 获取指定索引对象。
         *
         * @param table_oid 表 oid
         * @param index_oid 索引 oid
         * @return 不存在返回 nullptr
         */
        storage::BPlusTree* GetIndex(table_oid_t table_oid, index_oid_t index_oid) noexcept;

        /**
         * 获取指定索引对象。
         *
         * @param table_oid 表 oid
         * @param index_oid 索引 oid
         * @return 不存在返回 nullptr
         */
        const storage::BPlusTree* GetIndex(table_oid_t table_oid, index_oid_t index_oid) const noexcept;

        /**
         * 返回下一个可分配的 index oid。
         */
        index_oid_t NextIndexOid() const noexcept;

    private:
        struct TableMeta
        {
            table_oid_t table_oid{0};
            std::string table_name;
            Schema schema;
            page_id_t first_page_id{INVALID_PAGE_ID};
        };

        struct IndexMeta
        {
            index_oid_t index_oid{0};
            table_oid_t table_oid{0};
            std::string index_name;
            page_id_t header_page_id{INVALID_PAGE_ID};
        };

        struct CatalogMetaSnapshot
        {
            table_oid_t next_table_oid{0};
            index_oid_t next_index_oid{0};
            std::vector<TableMeta> tables;
            std::vector<IndexMeta> indexes;
        };

        /**
         * 从持久化 catalog 元数据重建运行时目录。
         */
        std::expected<void, std::string> LoadCatalogMeta();

        /**
         * 将当前运行时目录序列化并写回 catalog 元数据页链。
         *
         * @note 调用方需已持有 latch_ 写锁
         */
        std::expected<void, std::string> PersistCatalogMetaLocked();

        /**
         * 读取 catalog 元数据页链。
         */
        std::expected<std::vector<std::byte>, std::string>
        ReadCatalogMetaPayload(page_id_t catalog_meta_page_id) const;

        /**
         * 把给定 payload 写入 catalog 元数据页链。
         *
         * @note 调用方需已持有 latch_ 写锁
         */
        std::expected<void, std::string>
        WriteCatalogMetaPayloadLocked(std::span<const std::byte> payload);

        /**
         * 确保存在 catalog 元数据入口页。
         *
         * @note 调用方需已持有 latch_ 写锁
         */
        std::expected<page_id_t, std::string> EnsureCatalogMetaEntryPageLocked();

        /**
         * 采集当前目录状态为持久化快照。
         *
         * @note 调用方需已持有 latch_ 锁
         */
        CatalogMetaSnapshot BuildMetaSnapshotLocked() const;

        /**
         * 序列化 catalog 快照。
         */
        std::expected<std::vector<std::byte>, std::string>
        SerializeCatalogMeta(const CatalogMetaSnapshot& snapshot) const;

        /**
         * 反序列化 catalog 快照。
         */
        std::expected<CatalogMetaSnapshot, std::string>
        DeserializeCatalogMeta(std::span<const std::byte> payload) const;

        /**
         * 根据快照重建运行时目录。
         *
         * @note 调用方需已持有 latch_ 写锁
         */
        std::expected<void, std::string>
        BuildRuntimeFromMetaLocked(const CatalogMetaSnapshot& snapshot);

        /**
         * 创建一张新的 table heap。
         */
        std::expected<std::unique_ptr<table::TableHeap>, std::string>
        CreateTableHeap();

        /**
         * 分配新的 table oid。
         */
        table_oid_t AllocateTableOid() noexcept;

        /**
         * 分配新的 index oid。
         */
        index_oid_t AllocateIndexOid() noexcept;

        /**
         * 校验表名是否合法。
         *
         * @param table_name 表名
         */
        static std::expected<void, std::string>
        ValidateTableName(std::string_view table_name);

    private:
        buffer::BufferPoolManager* bpm_{nullptr};

        table_oid_t next_table_oid_{0};
        index_oid_t next_index_oid_{0};

        /// table_name -> table_oid
        std::unordered_map<std::string, table_oid_t> name_to_oid_;

        /// table_oid -> TableInfo
        std::unordered_map<table_oid_t, std::unique_ptr<TableInfo>> tables_;

        /// 保护目录层元数据
        mutable std::shared_mutex latch_;
    };

} // namespace catalog
} // namespace HaruhiDB
