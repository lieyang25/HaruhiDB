/**
 * CXX/src/include/catalog/schema.h
 *
 * ========================= 设计目标 =========================
 *
 * Schema 用于描述一张表（Table）的结构。
 *
 * Schema = 多个 Column 的集合。
 *
 * 在 SQL 中：
 *
 * CREATE TABLE student (
 *     id INT,
 *     age INT,
 *     name VARCHAR(32)
 * );
 *
 * Schema 就是：
 *
 * [
 *   Column("id", INT),
 *   Column("age", INT),
 *   Column("name", VARCHAR(32))
 * ]
 *
 *
 * ========================= Schema 在数据库中的作用 =========================
 *
 * Schema 的核心职责有三个：
 *
 * 1 描述表结构
 *
 *   Table
 *     └── Schema
 *            ├── Column
 *            ├── Column
 *            └── Column
 *
 *
 * 2 描述 Tuple 内存布局
 *
 * Schema 会计算：
 *
 * column offset
 * tuple size
 * varlen columns
 *
 *
 * 3 提供列查找接口
 *
 * 例如：
 *
 * SELECT name FROM table
 *
 * 数据库需要知道：
 *
 * name 在 schema 中的 index
 *
 *
 *
 * ========================= Schema 与其他组件关系 =========================
 *
 * Catalog
 *    │
 *    └── Table
 *          │
 *          └── Schema
 *                │
 *                └── Column
 *
 *
 * Storage 层：
 *
 * Page
 *   └── Tuple
 *         └── Schema 负责解析
 *
 */

#pragma once

#include "catalog/column.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{

    /**
     * Schema
     *
     * 表示一张表的结构定义。
     *
     * Schema 包含：
     *
     * - 多个 Column
     * - tuple layout 信息
     * - column name -> index 映射
     */
    class Schema {
    public:

        /**
         * 默认构造
         */
        Schema() = default;

        /**
         * 使用 Column 构造 Schema
         *
         * 会自动计算：
         *
         * offset
         * tuple layout
         */
        explicit Schema(std::vector<Column> columns);

        /**
         * 安全构造接口
         *
         * 返回：
         *
         * expected<Schema, error>
         */
        static std::expected<Schema, std::string> Create(std::vector<Column> columns);

        /**
         * 获取所有列
         */
        const std::vector<Column>& Columns() const noexcept { return columns_; }

        /**
         * 获取列数量
         */
        size_t ColumnCount() const noexcept { return columns_.size(); }

        /**
         * 是否为空 schema
         */
        bool Empty() const noexcept { return columns_.empty(); }

        /**
         * 根据 index 获取 column
         */
        const Column& GetColumn(size_t index) const;

        /**
         * 可修改版本
         */
        Column& GetColumn(size_t index);

        /**
         * 根据名称获取 column index
         *
         * 不存在返回 nullopt
         */
        std::optional<size_t> TryGetColumnIndex(std::string_view name) const noexcept;

        /**
         * 根据名称获取 column index
         *
         * 不存在抛异常
         */
        size_t GetColumnIndex(std::string_view name) const;

        /**
         * 是否存在该列
         */
        bool HasColumn(std::string_view name) const noexcept;

        /**
         * 根据名称查找 column
         *
         * 不存在返回 nullptr
         */
        const Column* FindColumn(std::string_view name) const noexcept;

        /**
         * 可修改版本
         */
        Column* FindColumn(std::string_view name) noexcept;

        /**
         * tuple 是否完全 inline
         *
         * 如果包含 VARCHAR
         * 则为 false
         */
        bool IsTupleInlined() const noexcept { return tuple_inlined_; }

        /**
         * tuple inline 数据大小
         */
        uint32_t InlinedStorageSize() const noexcept { return inlined_storage_size_; }

        /**
         * 所有变长列 index
         */
        const std::vector<uint32_t>& UninlinedColumns() const noexcept { return uninlined_columns_; }

        /**
         * 返回所有列名
         */
        std::vector<std::string> ColumnNames() const;

        /**
         * Schema 投影
         *
         * SELECT a,b FROM table
         *
         * 就需要 project schema
         */
        Schema Project(std::span<const uint32_t> column_indices) const;

    private:

        /**
         * 构建 tuple layout
         *
         * 主要工作：
         *
         * - 计算 offset
         * - 构建 name map
         * - 统计 varlen column
         */
        void BuildLayoutOrThrow();

        /**
         * 所有 column
         */
        std::vector<Column> columns_;

        /**
         * column name -> index 映射
         *
         * 用于快速查找
         */
        std::unordered_map<std::string, size_t> name_to_index_;

        /**
         * 变长列列表
         */
        std::vector<uint32_t> uninlined_columns_;

        /**
         * tuple 是否完全 inline
         */
        bool tuple_inlined_{true};

        /**
         * tuple inline 部分总大小
         */
        uint32_t inlined_storage_size_{0};
    };

} // namespace catalog
} // namespace HaruhiDB