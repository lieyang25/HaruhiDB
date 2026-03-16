/**
 * CXX/src/include/catalog/column.h
 *
 * ========================= 设计目标 =========================
 *
 * Column 用于描述表中“一列”的元信息。
 *
 * 它不保存真实记录数据，
 * 只描述这一列在 schema 与 tuple layout 中应当如何被解释。
 *
 * 核心信息包括：
 *
 * 1. 列名
 * 2. 类型
 * 3. 长度
 * 4. 是否允许 NULL
 * 5. 默认值
 * 6. 在 tuple 中的偏移
 * 7. 是否为变长列
 *
 *
 * ========================= 为什么需要 Column =========================
 *
 * Schema 本质上是多个 Column 的集合。
 *
 * tuple 在存储层只是连续字节，
 * 要正确解析每一列，必须知道：
 *
 * - 这一列是什么类型
 * - 占多少空间
 * - 是否内联
 * - 在 tuple 中位于什么偏移
 *
 * 这些信息都由 Column 提供。
 *
 *
 * ========================= Column 在系统中的位置 =========================
 *
 * Table
 *   └── Schema
 *         ├── Column
 *         ├── Column
 *         └── Column
 *
 * Schema 使用 Column 描述 tuple layout，
 * 编解码逻辑再依据 Column 解释字段内容。
 */

#pragma once

#include "type/type.h"
#include "type/value.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace HaruhiDB
{
namespace catalog
{

    class Column
    {
    public:
        /**
         * 变长列在 tuple 内部占用的槽位大小。
         *
         * 当前设计中，
         * VARCHAR 等变长列在定长区域只保存一个 4 字节 slot。
         */
        static constexpr uint32_t VARLEN_SLOT_SIZE = sizeof(uint32_t);

        /**
         * 构造固定长度列。
         *
         * @param name          列名
         * @param type          列类型
         * @param nullable      是否允许 NULL
         * @param default_value 默认值
         */
        Column(std::string name, type::TypeId type, bool nullable = true,
               std::optional<type::Value> default_value = std::nullopt);

        /**
         * 构造变长列或显式长度列。
         *
         * @param name          列名
         * @param type          列类型
         * @param length        列长度或最大长度
         * @param nullable      是否允许 NULL
         * @param default_value 默认值
         */
        Column(std::string name, type::TypeId type, uint32_t length, bool nullable,
               std::optional<type::Value> default_value = std::nullopt);

        /**
         * 返回列名。
         */
        const std::string& Name() const noexcept { return name_; }

        /**
         * 返回列类型。
         */
        type::TypeId Type() const noexcept { return type_; }

        /**
         * 返回列长度。
         *
         * 固定长度类型返回物理长度；
         * 变长类型返回声明的最大长度。
         */
        uint32_t Length() const noexcept { return length_; }

        /**
         * 返回该列是否允许 NULL。
         */
        bool Nullable() const noexcept { return nullable_; }

        /**
         * 返回该列是否为内联存储。
         */
        bool IsInlined() const noexcept { return inlined_; }

        /**
         * 返回该列是否为变长列。
         */
        bool IsVarlen() const noexcept { return !inlined_; }

        /**
         * 返回该列在 tuple 中的偏移。
         */
        uint32_t Offset() const noexcept { return offset_; }

        /**
         * 设置该列在 tuple 中的偏移。
         *
         * @param offset 偏移值
         * @note 通常由 Schema 统一计算并写入
         */
        void SetOffset(uint32_t offset) noexcept { offset_ = offset; }

        /**
         * 返回默认值。
         */
        const std::optional<type::Value>& DefaultValue() const noexcept { return default_value_; }

        /**
         * 判断是否存在默认值。
         */
        bool HasDefaultValue() const noexcept { return default_value_.has_value(); }

        /**
         * 返回该列在 tuple 定长区中实际占用的空间。
         *
         * 固定长度列返回自身长度，
         * 变长列返回 slot 大小。
         */
        uint32_t StorageSize() const noexcept { return inlined_ ? length_ : VARLEN_SLOT_SIZE; }

        /**
         * 转为 SQL 风格字符串。
         */
        std::string ToString() const;

        /**
         * 校验列定义是否合法。
         */
        std::expected<void, std::string> Validate() const;

    private:
        /// 列名
        std::string name_;

        /// 列类型
        type::TypeId type_{type::TypeId::INVALID};

        /// 固定长度或变长上限
        uint32_t length_{0};

        /// 是否允许 NULL
        bool nullable_{true};

        /// 在 tuple 中的偏移
        uint32_t offset_{0};

        /// 是否内联存储
        bool inlined_{true};

        /// 默认值
        std::optional<type::Value> default_value_;
    };

} // namespace catalog
} // namespace HaruhiDB