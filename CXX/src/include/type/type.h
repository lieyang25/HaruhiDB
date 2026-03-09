/**
 * CXX/src/include/type/type.h
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
    enum class TypeId : uint8_t {
        INVALID = 0,
        BOOLEAN,
        TINYINT,    // int8
        SMALLINT,   // int16
        INTEGER,    // int32
        BIGINT,     // int64
        FLOAT,      // float
        DOUBLE,     // double
        DECIMAL,    // optional
        VARCHAR,    // variable length
        // future: DATE, TIMESTAMP, BLOB, ...
    };

    struct TypeUtil {
        static constexpr int VARIABLE_LENGTH = -1;

        //在有效值范围内
        static constexpr bool IsValid(TypeId t) noexcept
        {
            return t > TypeId::INVALID && t <= TypeId::VARCHAR;
        }

        //是否可变长度
        static constexpr bool IsVariableLength(TypeId t) noexcept
        {
            return t == TypeId::VARCHAR;
        }

        //是否为整数
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

        //是否为浮点数
        static constexpr bool IsFloatingPoint(TypeId t) noexcept
        {
            return t == TypeId::FLOAT || t == TypeId::DOUBLE || t == TypeId::DECIMAL;
        }

        //是否为数字
        static constexpr bool IsNumeric(TypeId t) noexcept
        {
            return IsIntegral(t) || IsFloatingPoint(t);
        }

        //返回特定大小
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

        //返回类型名 -> string_view
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

        //解析字符串
        static inline std::optional<TypeId> ParseType(std::string_view name)
        {
            std::array<char, 32> lowered_buf{};
            const size_t n = std::min(name.size(), lowered_buf.size());
            for (size_t i = 0; i < n; i++) {
                lowered_buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
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
