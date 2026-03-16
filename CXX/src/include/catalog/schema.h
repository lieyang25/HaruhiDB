/**
 * CXX/src/include/catalog/schema.h
 *
 * ========================= 设计目标 =========================
 *
 * Schema 用于描述一张表的结构定义。
 *
 * 它由多个 Column 组成，
 * 同时负责描述 tuple 的逻辑布局与列查找关系。
 *
 * 核心职责：
 *
 * 1. 保存所有列定义
 * 2. 计算各列在 tuple 中的 offset
 * 3. 维护列名到列下标的映射
 * 4. 统计变长列信息
 * 5. 支持 schema 投影
 *
 *
 * ========================= 为什么需要 Schema =========================
 *
 * Column 只描述“单列”信息，
 * 但数据库真正操作的是“一整行记录”。
 *
 * Schema 负责把多个 Column 组织起来，
 * 并回答下面这些问题：
 *
 * - 第 i 列是什么
 * - 某列名字对应哪个下标
 * - 某列在 tuple 中的 offset 是多少
 * - 这张表是否包含变长列
 * - tuple 的 inline 区总大小是多少
 *
 *
 * ========================= Schema 在系统中的位置 =========================
 *
 * Catalog
 *   └── TableInfo
 *         └── Schema
 *               ├── Column
 *               ├── Column
 *               └── Column
 *
 * Tuple 编解码时，
 * 由 Schema 决定各列的布局与解释方式。
 *
 *
 * ========================= 组织形式 =========================
 *
 * Schema
 *   ├── columns_
 *   ├── name_to_index_
 *   ├── uninlined_columns_
 *   ├── tuple_inlined_
 *   └── inlined_storage_size_
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

    class Schema
    {
    public:
        /**
         * 默认构造。
         */
        Schema() = default;

        /**
         * 使用一组列构造 Schema。
         *
         * @param columns 列定义集合
         * @note 构造时会自动建立 layout 与名称索引
         */
        explicit Schema(std::vector<Column> columns);

        /**
         * 安全构造接口。
         *
         * @param columns 列定义集合
         * @return 成功返回 Schema，失败返回错误信息
         */
        static std::expected<Schema, std::string> Create(std::vector<Column> columns);

        /**
         * 返回所有列。
         */
        const std::vector<Column>& Columns() const noexcept { return columns_; }

        /**
         * 返回列数量。
         */
        size_t ColumnCount() const noexcept { return columns_.size(); }

        /**
         * 判断是否为空 schema。
         */
        bool Empty() const noexcept { return columns_.empty(); }

        /**
         * 按下标获取列。
         *
         * @param index 列下标
         */
        const Column& GetColumn(size_t index) const;

        /**
         * 按下标获取列。
         *
         * @param index 列下标
         */
        Column& GetColumn(size_t index);

        /**
         * 按列名查找列下标。
         *
         * @param name 列名
         * @return 找到则返回下标，否则返回 nullopt
         */
        std::optional<size_t> TryGetColumnIndex(std::string_view name) const noexcept;

        /**
         * 按列名查找列下标。
         *
         * @param name 列名
         * @note 不存在时抛异常
         */
        size_t GetColumnIndex(std::string_view name) const;

        /**
         * 判断是否包含指定列名。
         *
         * @param name 列名
         */
        bool HasColumn(std::string_view name) const noexcept;

        /**
         * 按列名查找列。
         *
         * @param name 列名
         * @return 找到则返回列指针，否则返回 nullptr
         */
        const Column* FindColumn(std::string_view name) const noexcept;

        /**
         * 按列名查找列。
         *
         * @param name 列名
         * @return 找到则返回列指针，否则返回 nullptr
         */
        Column* FindColumn(std::string_view name) noexcept;

        /**
         * 判断 tuple 是否完全由 inline 列组成。
         */
        bool IsTupleInlined() const noexcept { return tuple_inlined_; }

        /**
         * 返回 tuple inline 区总大小。
         */
        uint32_t InlinedStorageSize() const noexcept { return inlined_storage_size_; }

        /**
         * 返回所有变长列下标。
         */
        const std::vector<uint32_t>& UninlinedColumns() const noexcept { return uninlined_columns_; }

        /**
         * 返回所有列名。
         */
        std::vector<std::string> ColumnNames() const;

        /**
         * 返回 schema 的可读字符串表示。
         */
        std::string ToString() const;

        /**
         * 基于列下标做投影，生成新的 Schema。
         *
         * @param column_indices 需要保留的列下标集合
         */
        Schema Project(std::span<const uint32_t> column_indices) const;

    private:
        /**
         * 构建 layout 与辅助索引。
         *
         * @note 主要负责：
         * - 计算 offset
         * - 建立 name_to_index_
         * - 收集变长列
         * - 统计 inlined_storage_size_
         */
        void BuildLayoutOrThrow();

    private:
        /// 所有列定义
        std::vector<Column> columns_;

        /// 列名到列下标映射
        std::unordered_map<std::string, size_t> name_to_index_;

        /// 所有变长列下标
        std::vector<uint32_t> uninlined_columns_;

        /// tuple 是否完全 inline
        bool tuple_inlined_{true};

        /// tuple inline 区总大小
        uint32_t inlined_storage_size_{0};
    };

} // namespace catalog
} // namespace HaruhiDB