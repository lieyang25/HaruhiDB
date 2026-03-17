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
 * 1. table oid
 * 2. table name
 * 3. table schema
 * 4. table heap
 * 5. attached indexes
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
 * - 索引入口（indexes）
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
 *   ├── table heap
 *   └── indexes
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/schema.h"
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

    class TableInfo
    {
    public:
        /**
         * 禁止默认构造。
         */
        TableInfo() = delete;

        /**
         * 构造一张表的运行时信息。
         *
         * @param oid        表 oid
         * @param name       表名
         * @param schema     表结构
         * @param table_heap 表数据入口
         */
        TableInfo(
            table_oid_t oid,
            std::string name,
            Schema schema,
            std::unique_ptr<table::TableHeap> table_heap);

        /**
         * 返回表 oid。
         */
        table_oid_t Oid() const noexcept { return oid_; }

        /**
         * 返回表名。
         */
        const std::string& Name() const noexcept { return name_; }

        /**
         * 返回表 schema。
         */
        const Schema& GetSchema() const noexcept { return schema_; }

        /**
         * 返回可修改 schema。
         */
        Schema& GetSchema() noexcept { return schema_; }

        /**
         * 返回 table heap。
         */
        table::TableHeap* GetTableHeap() const noexcept { return table_heap_.get(); }

        /**
         * 判断是否持有有效 table heap。
         */
        bool HasTableHeap() const noexcept { return table_heap_ != nullptr; }

        /**
         * 返回该表绑定的索引 oid 列表。
         */
        const std::vector<index_oid_t>& IndexOids() const noexcept { return index_oids_; }

        /**
         * 为该表登记一个索引 oid。
         *
         * @param index_oid 索引 oid
         */
        void AddIndexOid(index_oid_t index_oid);

        struct IndexEntry
        {
            index_oid_t index_oid{0};
            std::string index_name;
            page_id_t header_page_id{INVALID_PAGE_ID};
            std::unique_ptr<storage::BPlusTree> index;
        };

        /**
         * 创建一个新索引并挂接到该表。
         *
         * @param index_oid  索引 oid
         * @param index_name 索引名
         * @param bpm        缓冲池管理器
         * @return 成功返回索引指针
         */
        std::expected<storage::BPlusTree*, std::string> CreateIndex(
            index_oid_t index_oid,
            std::string index_name,
            buffer::BufferPoolManager* bpm);

        /**
         * 从已有 header page 加载一个索引并挂接到该表。
         *
         * @param index_oid       索引 oid
         * @param index_name      索引名
         * @param header_page_id  索引 header page
         * @param bpm             缓冲池管理器
         * @return 成功返回索引指针
         */
        std::expected<storage::BPlusTree*, std::string> LoadIndex(
            index_oid_t index_oid,
            std::string index_name,
            page_id_t header_page_id,
            buffer::BufferPoolManager* bpm);

        /**
         * 按 oid 获取索引。
         */
        storage::BPlusTree* GetIndex(index_oid_t index_oid) noexcept;

        /**
         * 按 oid 获取索引。
         */
        const storage::BPlusTree* GetIndex(index_oid_t index_oid) const noexcept;

        /**
         * 返回索引 header page id。
         *
         * @param index_oid 索引 oid
         * @return 找到则返回 header page id
         */
        std::optional<page_id_t> GetIndexHeaderPageId(index_oid_t index_oid) const noexcept;

        /**
         * 返回全部索引项。
         */
        const std::vector<IndexEntry>& IndexEntries() const noexcept { return indexes_; }

        /**
         * 返回便于调试的字符串描述。
         */
        std::string ToString() const;

    private:
        /// 表 oid
        table_oid_t oid_{0};

        /// 表名
        std::string name_;

        /// 表结构
        Schema schema_;

        /// 表数据入口
        std::unique_ptr<table::TableHeap> table_heap_;

        /// 已绑定索引 oid 列表
        std::vector<index_oid_t> index_oids_;

        /// 已加载索引对象
        std::vector<IndexEntry> indexes_;
    };

} // namespace catalog
} // namespace HaruhiDB