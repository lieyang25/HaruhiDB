/**
 * CXX/src/include/type/value.h
 */

#pragma once
#include "type/type.h"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace HaruhiDB
{
namespace type
{
    class Value {
    public:
        /**
         * @note monostate 是NULL
         */
        using Container = std::variant<std::monostate, bool, int8_t, int16_t, int32_t, int64_t, float, double,
                                       std::string>;

        Value() = default;
        //初始化赋值
        explicit Value(bool v) : data_(v) {}
        explicit Value(int8_t v) : data_(v) {}
        explicit Value(int16_t v) : data_(v) {}
        explicit Value(int32_t v) : data_(v) {}
        explicit Value(int64_t v) : data_(v) {}
        explicit Value(float v) : data_(v) {}
        explicit Value(double v) : data_(v) {}
        explicit Value(std::string v) : data_(std::move(v)) {}

        //返回值
        static Value Null() { return Value(); }
        static Value Boolean(bool b) { return Value(b); }
        static Value Int8(int8_t i) { return Value(i); }
        static Value Int16(int16_t i) { return Value(i); }
        static Value Int32(int32_t i) { return Value(i); }
        static Value Int64(int64_t i) { return Value(i); }
        static Value Float(float f) { return Value(f); }
        static Value Double(double d) { return Value(d); }
        static Value VarChar(std::string s) { return Value(std::move(s)); }

        /**
         * 判断是否为NULL
         * @note std::holds_alternative可以检查 std::variant的值
         */
        bool IsNull() const noexcept { return std::holds_alternative<std::monostate>(data_); }

        /**
         * @param data_ 判断此变量类型
         * @note std::decay_t用于清洗类型，std::is_same_v用于判断值是否相同
         */
        TypeId Type() const noexcept
        {
            return std::visit(
                []<typename T>(const T&) -> TypeId {
                    using U = std::decay_t<T>;
                    if constexpr (std::is_same_v<U, std::monostate>) {
                        return TypeId::INVALID;
                    }
                    if constexpr (std::is_same_v<U, bool>) {
                        return TypeId::BOOLEAN;
                    }
                    if constexpr (std::is_same_v<U, int8_t>) {
                        return TypeId::TINYINT;
                    }
                    if constexpr (std::is_same_v<U, int16_t>) {
                        return TypeId::SMALLINT;
                    }
                    if constexpr (std::is_same_v<U, int32_t>) {
                        return TypeId::INTEGER;
                    }
                    if constexpr (std::is_same_v<U, int64_t>) {
                        return TypeId::BIGINT;
                    }
                    if constexpr (std::is_same_v<U, float>) {
                        return TypeId::FLOAT;
                    }
                    if constexpr (std::is_same_v<U, double>) {
                        return TypeId::DOUBLE;
                    }
                    if constexpr (std::is_same_v<U, std::string>) {
                        return TypeId::VARCHAR;
                    }
                    return TypeId::INVALID;
                },
                data_);
        }

        //返回data值
        const Container& Raw() const noexcept { return data_; }

        /**
         * @note std::get_if返回T类的地址，否则年
         */
        template <typename T>
        const T* TryAs() const noexcept
        {
            return std::get_if<T>(&data_);
        }

        template <typename T>
        T* TryAs() noexcept
        {
            return std::get_if<T>(&data_);
        }

        bool CanCastTo(TypeId target) const noexcept;
        std::optional<long double> AsLongDouble() const noexcept;
        std::partial_ordering Compare(const Value& rhs) const noexcept;
        std::string ToString() const;

        std::vector<std::byte> Serialize(TypeId type) const;
        static Value Deserialize(TypeId type, const std::byte *ptr, size_t len);

        friend bool operator==(const Value&, const Value&) = default;

    private:
        Container data_{std::monostate{}};
    };
} // namespace type
} // namespace HaruhiDB
