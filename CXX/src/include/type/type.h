/**
 * CXX/src/include/type/type.h
 *
 * ========================= 设计目标 =========================
 *
 * TypeId 用于表示数据库中的基础数据类型。
 *
 * TypeUtil 提供类型相关的基础判断与辅助函数，
 * 例如：
 *
 * - 类型是否合法
 * - 是否为可变长度类型
 * - 是否为数值类型
 * - 返回固定长度
 * - 类型名转换
 * - 从字符串解析类型
 *
 *
 * ========================= 为什么需要 Type 系统 =========================
 *
 * 在数据库系统中，列必须带有明确类型：
 *
 * 例如：
 *
 *   id        INTEGER
 *   name      VARCHAR
 *   score     DOUBLE
 *
 * Schema、Column、Value 等模块都需要依赖 TypeId
 * 来确定：
 *
 * - 存储长度
 * - 编码方式
 * - 比较规则
 * - 表达式计算
 *
 *
 * ========================= Type 在系统中的位置 =========================
 *
 * Schema
 *   └── Column
 *         └── TypeId
 *
 * Value
 *   └── TypeId
 *
 * Executor
 *   └── 表达式计算
 *
 *
 * ========================= 类型分类 =========================
 *
 * 固定长度类型
 *
 *   BOOLEAN
 *   TINYINT
 *   SMALLINT
 *   INTEGER
 *   BIGINT
 *   FLOAT
 *   DOUBLE
 *   DECIMAL
 *
 * 可变长度类型
 *
 *   VARCHAR
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

namespace HaruhiDB
{
namespace type
{

    /**
     * 数据类型标识。
     */
    enum class TypeId : uint8_t {
        INVALID = 0,
        BOOLEAN,
        TINYINT,    // int8
        SMALLINT,   // int16
        INTEGER,    // int32
        BIGINT,     // int64
        FLOAT,      // float
        DOUBLE,     // double
        DECIMAL,    // decimal / numeric
        VARCHAR     // variable length
    };

    /**
     * 类型辅助工具。
     */
    struct TypeUtil
    {
        /// 表示可变长度
        static constexpr int VARIABLE_LENGTH = -1;

        /**
         * 判断类型是否有效。
         */
        static constexpr bool IsValid(TypeId t) noexcept
        {
            return t > TypeId::INVALID && t <= TypeId::VARCHAR;
        }

        /**
         * 判断是否为可变长度类型。
         */
        static constexpr bool IsVariableLength(TypeId t) noexcept
        {
            return t == TypeId::VARCHAR;
        }

        /**
         * 判断是否为整数类型。
         */
        static constexpr bool IsIntegral(TypeId t) noexcept
        {
            switch (t) {
                case TypeId::BOOLEAN:
                case TypeId::TINYINT:
                case TypeId::SMALLINT:
                case TypeId::INTEGER:
                case TypeId::BIGINT:
                    return true;
                default:
                    return false;
            }
        }

        /**
         * 判断是否为浮点类型。
         */
        static constexpr bool IsFloatingPoint(TypeId t) noexcept
        {
            return t == TypeId::FLOAT || t == TypeId::DOUBLE || t == TypeId::DECIMAL;
        }

        /**
         * 判断是否为数值类型。
         */
        static constexpr bool IsNumeric(TypeId t) noexcept
        {
            return IsIntegral(t) || IsFloatingPoint(t);
        }

        /**
         * 返回固定长度类型的字节大小。
         *
         * @return 若为可变长度类型则返回 VARIABLE_LENGTH
         */
        static constexpr int FixedLengthSize(TypeId t) noexcept
        {
            switch (t) {
                case TypeId::BOOLEAN: return 1;
                case TypeId::TINYINT: return 1;
                case TypeId::SMALLINT: return 2;
                case TypeId::INTEGER: return 4;
                case TypeId::BIGINT: return 8;
                case TypeId::FLOAT: return 4;
                case TypeId::DOUBLE: return 8;
                case TypeId::DECIMAL: return 8;
                default: return VARIABLE_LENGTH;
            }
        }

        /**
         * 返回类型名称。
         */
        static constexpr std::string_view TypeName(TypeId t) noexcept
        {
            switch (t) {
                case TypeId::INVALID: return "INVALID";
                case TypeId::BOOLEAN: return "BOOLEAN";
                case TypeId::TINYINT: return "TINYINT";
                case TypeId::SMALLINT: return "SMALLINT";
                case TypeId::INTEGER: return "INTEGER";
                case TypeId::BIGINT: return "BIGINT";
                case TypeId::FLOAT: return "FLOAT";
                case TypeId::DOUBLE: return "DOUBLE";
                case TypeId::DECIMAL: return "DECIMAL";
                case TypeId::VARCHAR: return "VARCHAR";
            }
            return "INVALID";
        }

        /**
         * 从字符串解析 TypeId。
         *
         * @param name 类型名
         * @return 若成功返回 TypeId，否则返回 nullopt
         */
        static inline std::optional<TypeId> ParseType(std::string_view name)
        {
            std::array<char, 32> lowered_buf{};
            const size_t n = std::min(name.size(), lowered_buf.size());

            for (size_t i = 0; i < n; i++) {
                lowered_buf[i] =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
            }

            const std::string_view lowered(lowered_buf.data(), n);

            if (lowered == "bool" || lowered == "boolean") return TypeId::BOOLEAN;
            if (lowered == "int8" || lowered == "tinyint") return TypeId::TINYINT;
            if (lowered == "int16" || lowered == "smallint") return TypeId::SMALLINT;
            if (lowered == "int32" || lowered == "int" || lowered == "integer") return TypeId::INTEGER;
            if (lowered == "int64" || lowered == "bigint") return TypeId::BIGINT;
            if (lowered == "float") return TypeId::FLOAT;
            if (lowered == "double") return TypeId::DOUBLE;
            if (lowered == "decimal") return TypeId::DECIMAL;
            if (lowered == "varchar" || lowered == "string" || lowered == "text") return TypeId::VARCHAR;

            return std::nullopt;
        }
    };

} // namespace type
} // namespace HaruhiDB