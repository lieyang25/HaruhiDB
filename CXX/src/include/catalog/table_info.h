/**
 * CXX/src/include/catalog/table_info.h
 *
 * ========================= 设计目标 =========================
 *
 * TableInfo 用于描述“一张表”的运行时元信息。
 *
 * 它不是表数据本身，也不是页结构，
 * 而是把一张表相关的关键对象组织在一起：
 *
 * 1. table oid / table id
 * 2. table name
 * 3. table schema
 * 4. table heap
 *
 *
 * ========================= 为什么需要 TableInfo =========================
 *
 * 仅有 Schema 不够，因为 Schema 只描述“表长什么样”。
 *
 * 仅有 TableHeap 也不够，因为 TableHeap 只描述“数据怎么存和访问”。
 *
 * TableInfo 把：
 *
 * - 身份（oid）
 * - 名称（name）
 * - 结构（schema）
 * - 数据入口（table heap）
 *
 * 统一组织成一个对象。
 *
 *
 * ========================= TableInfo 在系统中的位置 =========================
 *
 * Catalog
 *   ├── TableInfo(student)
 *   ├── TableInfo(course)
 *   └── TableInfo(score)
 *
 * TableInfo
 *   ├── table name
 *   ├── schema
 *   └── table heap
 *
 *
 * ========================= 当前阶段说明 =========================
 *
 * 当前设计为“最小运行时版本”：
 *
 * - 不处理持久化 catalog
 * - 处理最小索引运行时元数据（index oid / name / header page id）
 * - 不处理复杂 DDL（DROP/ALTER）
 *
 * 先服务于：
 *
 * - Catalog::CreateTable
 * - Catalog::GetTable
 * - 基本表访问联动
 */

#pragma once

#include "catalog/schema.h"
#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "common/config.h"
#include "storage/index/b_plus_tree.h"
#include "table/table_heap.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{

    /**
     * TableInfo
     *
     * 表示一张表的运行时信息对象。
     */
    class TableInfo {
    public:

        /**
         * 默认构造禁用
         *
         * TableInfo 应始终处于完整有效状态，
         * 因此不建议允许空构造。
         */
        TableInfo() = delete;

        /**
         * 构造一张表的运行时信息。
         *
         * 参数：
         *
         * oid         表 oid / table id
         * name        表名
         * schema      表结构
         * table_heap  表数据入口对象
         */
        TableInfo(table_oid_t oid,
                  std::string name,
                  Schema schema,
                  std::unique_ptr<table::TableHeap> table_heap);

        /**
         * 获取表 oid
         */
        table_oid_t Oid() const noexcept { return oid_; }

        /**
         * 获取表名
         */
        const std::string& Name() const noexcept { return name_; }

        /**
         * 获取表 schema
         */
        const Schema& GetSchema() const noexcept { return schema_; }

        /**
         * 获取可修改 schema
         *
         * 当前阶段通常不需要修改 schema，
         * 保留该接口主要用于内部扩展。
         */
        Schema& GetSchema() noexcept { return schema_; }

        /**
         * 获取 table heap 指针
         *
         * 返回裸指针而不是 unique_ptr，
         * 因为拥有关系仍由 TableInfo 持有。
         */
        table::TableHeap* GetTableHeap() const noexcept { return table_heap_.get(); }

        /**
         * 当前是否持有有效 table heap
         */
        bool HasTableHeap() const noexcept { return table_heap_ != nullptr; }

        /**
         * 获取当前绑定的索引 oid 列表
         *
         * 当前阶段只是预留接口，
         * 后续可用于 CreateIndex / GetTableIndexes。
         */
        const std::vector<index_oid_t>& IndexOids() const noexcept { return index_oids_; }

        /**
         * 为该表增加一个索引 oid
         *
         * 当前阶段可选使用。
         */
        void AddIndexOid(index_oid_t index_oid);

        struct IndexEntry
        {
            index_oid_t index_oid{0};
            std::string index_name;
            page_id_t header_page_id{INVALID_PAGE_ID};
            std::unique_ptr<storage::BPlusTree> index;
        };

        std::expected<storage::BPlusTree*, std::string> CreateIndex(
            index_oid_t index_oid,
            std::string index_name,
            buffer::BufferPoolManager* bpm);

        std::expected<storage::BPlusTree*, std::string> LoadIndex(
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id,
            buffer::BufferPoolManager* bpm);

        storage::BPlusTree* GetIndex(index_oid_t index_oid) noexcept;
        const storage::BPlusTree* GetIndex(index_oid_t index_oid) const noexcept;

        std::optional<page_id_t> GetIndexHeaderPageId(index_oid_t index_oid) const noexcept;
        const std::vector<IndexEntry>& IndexEntries() const noexcept { return indexes_; }

        /**
         * 返回便于调试的字符串描述
         */
        std::string ToString() const;

    private:

        /**
         * 表 oid
         */
        table_oid_t oid_{0};

        /**
         * 表名
         */
        std::string name_;

        /**
         * 表结构定义
         */
        Schema schema_;

        /**
         * 表数据入口
         */
        std::unique_ptr<table::TableHeap> table_heap_;

        /**
         * 该表挂接的索引 oid 列表
         *
         * 当前阶段只做预留，不必过度使用。
         */
        std::vector<index_oid_t> index_oids_;
        std::vector<IndexEntry> indexes_;
    };

} // namespace catalog
} // namespace HaruhiDB
