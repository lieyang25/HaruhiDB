/**
 * CXX/src/include/catalog/column.h
 *
 * ========================= 设计目标 =========================
 *
 * Column 类用于描述数据库表（Table）中的“列”的元信息（metadata）。
 *
 * 在关系型数据库中，一张表的 Schema = 多个 Column 的集合。
 *
 * 例如 SQL：
 *
 * CREATE TABLE student (
 *     id INT NOT NULL,
 *     name VARCHAR(32),
 *     age INT DEFAULT 18
 * );
 *
 * 那么 schema 中就包含三个 Column：
 *
 * Column("id", INT, NOT NULL)
 * Column("name", VARCHAR, 32)
 * Column("age", INT, DEFAULT 18)
 *
 *
 * ========================= Column的作用 =========================
 *
 * Column 并不存储真实数据，它只描述：
 *
 * 1 列名
 * 2 类型
 * 3 长度
 * 4 是否允许 NULL
 * 5 默认值
 * 6 在 tuple 中的偏移量
 * 7 是否是变长类型
 *
 *
 * ========================= 为什么需要 Column =========================
 *
 * 在数据库内部，Tuple 的存储完全依赖 Schema。
 *
 * 例如：
 *
 * Tuple Layout:
 *
 * | id | age | name |
 *
 * 解析 tuple 时需要知道：
 *
 * id offset = 0
 * age offset = 4
 * name offset = 8
 *
 * 这些信息全部来自 Column。
 *
 *
 * ========================= Column 在系统中的位置 =========================
 *
 * Table
 *   │
 *   └── Schema
 *         │
 *         ├── Column
 *         ├── Column
 *         └── Column
 *
 *
 * Schema 用 Column 描述 tuple layout
 * Tuple 根据 Schema 解析
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

    /**
     * Column
     *
     * 表示表的一列的元数据描述。
     *
     * 该类不会存储真实数据，
     * 只存储如何解释 tuple 数据。
     */
    class Column {
    public:

        /**
         * 变长列在 tuple 内只存储一个 offset/size slot
         *
         * 例如 VARCHAR 不直接存储字符串，
         * 只存储一个 4 字节 slot：
         *
         * slot -> 指向真实字符串位置
         *
         * 因此 VARLEN_SLOT_SIZE = 4 bytes
         */
        static constexpr uint32_t VARLEN_SLOT_SIZE = sizeof(uint32_t);

        /**
         * 构造函数（固定长度类型）
         *
         * 用于 INT / DOUBLE / BOOL 等固定长度类型
         *
         * 参数：
         * name           列名
         * type           类型
         * nullable       是否允许 NULL
         * default_value  默认值
         *
         * length 会自动根据 type 推导
         */
        Column(std::string name, type::TypeId type, bool nullable = true,
               std::optional<type::Value> default_value = std::nullopt);

        /**
         * 构造函数（变长类型）
         *
         * 用于 VARCHAR / TEXT 等类型
         *
         * 参数：
         *
         * name
         * type
         * length      最大长度
         * nullable
         * default_value
         */
        Column(std::string name, type::TypeId type, uint32_t length, bool nullable,
               std::optional<type::Value> default_value = std::nullopt);

        /**
         * 获取列名
         */
        const std::string& Name() const noexcept { return name_; }

        /**
         * 获取类型
         */
        type::TypeId Type() const noexcept { return type_; }

        /**
         * 获取列长度
         *
         * 对于：
         *
         * INT = 4
         * DOUBLE = 8
         * VARCHAR = max length
         */
        uint32_t Length() const noexcept { return length_; }

        /**
         * 是否允许 NULL
         */
        bool Nullable() const noexcept { return nullable_; }

        /**
         * 是否是内联存储
         *
         * 内联类型：
         *
         * INT
         * DOUBLE
         * BOOL
         *
         * 非内联：
         *
         * VARCHAR
         */
        bool IsInlined() const noexcept { return inlined_; }

        /**
         * 是否是变长类型
         */
        bool IsVarlen() const noexcept { return !inlined_; }

        /**
         * 获取 tuple 中的 offset
         *
         * Schema 会计算 offset
         */
        uint32_t Offset() const noexcept { return offset_; }

        /**
         * 设置 offset
         *
         * 仅 Schema 使用
         */
        void SetOffset(uint32_t offset) noexcept { offset_ = offset; }

        /**
         * 获取默认值
         */
        const std::optional<type::Value>& DefaultValue() const noexcept { return default_value_; }

        /**
         * 是否存在默认值
         */
        bool HasDefaultValue() const noexcept { return default_value_.has_value(); }

        /**
         * 返回在 tuple 中实际占用的空间
         *
         * 固定类型：
         *
         * INT -> 4
         *
         * 变长类型：
         *
         * VARCHAR -> 4 (slot)
         */
        uint32_t StorageSize() const noexcept { return inlined_ ? length_ : VARLEN_SLOT_SIZE; }

        /**
         * 返回 SQL 风格字符串
         *
         * 例如：
         *
         * id INT NOT NULL
         * name VARCHAR(32) NULL
         */
        std::string ToString() const;

        /**
         * 验证 column 定义是否合法
         *
         * 返回：
         *
         * success
         * 或 error message
         */
        std::expected<void, std::string> Validate() const;

    private:

        /**
         * 列名
         */
        std::string name_;

        /**
         * 类型
         */
        type::TypeId type_{type::TypeId::INVALID};

        /**
         * 长度
         *
         * INT = 4
         * DOUBLE = 8
         * VARCHAR = max length
         */
        uint32_t length_{0};

        /**
         * 是否允许 NULL
         */
        bool nullable_{true};

        /**
         * tuple 中的 offset
         */
        uint32_t offset_{0};

        /**
         * 是否内联存储
         */
        bool inlined_{true};

        /**
         * 默认值
         */
        std::optional<type::Value> default_value_;
    };

} // namespace catalog
} // namespace HaruhiDB