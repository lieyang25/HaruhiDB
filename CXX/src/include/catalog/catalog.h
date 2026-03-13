/**
 * CXX/src/include/catalog/catalog.h
 *
 * ========================= 设计目标 =========================
 *
 * Catalog 用于管理数据库中的“表对象”。
 *
 * 它负责：
 *
 * 1. 创建表
 * 2. 按表名查表
 * 3. 按 table oid 查表
 * 4. 获取所有表
 *
 *
 * ========================= 为什么需要 Catalog =========================
 *
 * 当前系统已经具备：
 *
 * - Schema / Column：描述表结构
 * - TableHeap：组织表数据
 * - TableIterator：扫描表
 *
 * 但仍然缺少“数据库对象管理层”：
 *
 * - 数据库里有哪些表
 * - 表名对应哪个 TableHeap
 * - 表 schema 在哪里
 * - 如何通过名字统一访问表
 *
 * Catalog 正是这一层。
 *
 *
 * ========================= 当前阶段说明 =========================
 *
 * 当前为最小内存版 Catalog：
 *
 * - 不做持久化 catalog
 * - 不做系统表恢复
 * - 不做复杂 DDL
 * - 提供最小索引登记/恢复入口（索引内容由 B+Tree header page 持久化）
 *
 * 先服务于最基本的表对象管理。
 *
 *
 * ========================= 典型调用流程 =========================
 *
 * CreateTable("student", schema)
 *     -> 创建新的 TableHeap
 *     -> 构造 TableInfo
 *     -> 记录 name -> oid
 *     -> 记录 oid -> TableInfo
 *
 * GetTable("student")
 *     -> 找到对应 TableInfo
 *     -> 进一步获取 schema / table heap
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/table_info.h"
#include "common/config.h"

#include <expected>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{

    /**
     * Catalog
     *
     * 数据库表目录管理器。
     */
    class Catalog {
    public:

        /**
         * 构造 Catalog
         *
         * 参数：
         *
         * bpm  底层 BufferPoolManager
         *
         * 说明：
         *
         * 创建新表时通常需要借助 BufferPoolManager
         * 初始化对应的 TableHeap / 首页面。
         */
        explicit Catalog(buffer::BufferPoolManager* bpm);

        /**
         * 禁止拷贝
         */
        Catalog(const Catalog&) = delete;
        Catalog& operator=(const Catalog&) = delete;

        /**
         * 允许移动
         */
        Catalog(Catalog&&) = delete;
        Catalog& operator=(Catalog&&) = delete;

        /**
         * 创建表
         *
         * 功能：
         *
         * - 检查表名合法性
         * - 检查表名是否重复
         * - 创建新的 TableHeap
         * - 分配新的 table oid
         * - 构造 TableInfo 并登记
         *
         * 成功：
         *
         * 返回 TableInfo*
         *
         * 失败：
         *
         * 返回错误信息
         */
        std::expected<TableInfo*, std::string>
        CreateTable(std::string table_name, const Schema& schema);

        /**
         * 按表名查找表
         *
         * 不存在返回 nullptr。
         *
         * 生命周期说明：
         *
         * 返回指针为借用指针，不拥有对象。
         * 在 Catalog 与对应 TableInfo 仍然存活期间有效。
         */
        TableInfo* GetTable(std::string_view table_name) noexcept;

        /**
         * 按表名查找表（const 版本）
         *
         * 生命周期说明同上。
         */
        const TableInfo* GetTable(std::string_view table_name) const noexcept;

        /**
         * 按 oid 查找表
         *
         * 不存在返回 nullptr。
         *
         * 生命周期说明同上。
         */
        TableInfo* GetTable(table_oid_t table_oid) noexcept;

        /**
         * 按 oid 查找表（const 版本）
         *
         * 生命周期说明同上。
         */
        const TableInfo* GetTable(table_oid_t table_oid) const noexcept;

        /**
         * 检查是否存在指定表名
         */
        bool HasTable(std::string_view table_name) const noexcept;

        /**
         * 返回当前所有表
         *
         * 注意：
         *
         * 返回的是裸指针列表，
         * 拥有关系仍由 Catalog 内部维护。
         * 指针仅在 Catalog 与对应表对象仍存活期间有效。
         */
        std::vector<TableInfo*> GetAllTables();

        /**
         * const 版本
         *
         * 生命周期说明同上。
         */
        std::vector<const TableInfo*> GetAllTables() const;

        /**
         * 返回当前表数量
         */
        size_t TableCount() const noexcept;

        /**
         * 返回用于分配下一个 table oid 的值
         *
         * 主要用于调试或测试。
         */
        table_oid_t NextTableOid() const noexcept;

        std::expected<storage::BPlusTree*, std::string>
        CreateIndex(table_oid_t table_oid, std::string index_name);

        std::expected<storage::BPlusTree*, std::string>
        CreateIndex(std::string_view table_name, std::string index_name);

        std::expected<storage::BPlusTree*, std::string>
        LoadIndex(
            table_oid_t table_oid,
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id);

        std::expected<storage::BPlusTree*, std::string>
        LoadIndex(
            std::string_view table_name,
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id);

        storage::BPlusTree* GetIndex(table_oid_t table_oid, index_oid_t index_oid) noexcept;
        const storage::BPlusTree* GetIndex(table_oid_t table_oid, index_oid_t index_oid) const noexcept;

        index_oid_t NextIndexOid() const noexcept;

    private:

        /**
         * 创建一张新的 table heap
         *
         * 当前实现会委托给 TableHeap::Create(...)，
         * 由 TableHeap 自己负责首页合法性初始化细节。
         */
        std::expected<std::unique_ptr<table::TableHeap>, std::string>
        CreateTableHeap();

        /**
         * 为新表分配一个 table oid
         */
        table_oid_t AllocateTableOid() noexcept;
        index_oid_t AllocateIndexOid() noexcept;

        /**
         * 校验表名是否合法
         */
        static std::expected<void, std::string>
        ValidateTableName(std::string_view table_name);

    private:

        /**
         * 底层缓冲池管理器
         *
         * Catalog 不拥有该对象。
         */
        buffer::BufferPoolManager* bpm_{nullptr};

        /**
         * 下一个可分配的 table oid
         */
        table_oid_t next_table_oid_{0};
        index_oid_t next_index_oid_{0};

        /**
         * name -> oid
         *
         * 用于按表名快速找到表。
         */
        std::unordered_map<std::string, table_oid_t> name_to_oid_;

        /**
         * oid -> table info
         *
         * 真正拥有所有表对象。
         */
        std::unordered_map<table_oid_t, std::unique_ptr<TableInfo>> tables_;

        /**
         * 目录层读写锁
         *
         * 用于保护：
         *
         * - name_to_oid_
         * - tables_
         * - next_table_oid_
         */
        mutable std::shared_mutex latch_;
    };

} // namespace catalog
} // namespace HaruhiDB
