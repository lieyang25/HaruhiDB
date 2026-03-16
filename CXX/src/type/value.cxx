/**
 * CXX/src/type/value.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 Value 的运行时行为。
 *
 * 主要完成：
 *
 * 1. 数值类型转换判断
 * 2. 数值提升为 long double
 * 3. Value 比较
 * 4. 字符串化
 * 5. 按指定类型序列化
 * 6. 从原始字节反序列化
 *
 *
 * ========================= 核心机制 =========================
 *
 * CanCastTo:
 *   判断当前 Value 能否安全转换到目标 TypeId
 *
 * Compare:
 *   - NULL 与非 NULL 无序
 *   - 数值类型统一提升后比较
 *   - VARCHAR 按字符串字典序比较
 *   - 其余不兼容类型返回无序
 *
 * Serialize:
 *   - 数值类型按目标类型写入定长字节
 *   - VARCHAR 写入原始字符串字节
 *
 * TryDeserialize:
 *   - 按 TypeId 从给定字节区间恢复 Value
 *   - 长度不足时返回错误
 */

#include "type/value.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace HaruhiDB
{
namespace type
{
    namespace
    {
        template <typename... Ts>
        struct Overloaded : Ts... {
            using Ts::operator()...;
        };
        template <typename... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        template <typename T>
        std::vector<std::byte> SerializePod(const T& value)
        {
            std::vector<std::byte> out(sizeof(T));
            std::memcpy(out.data(), &value, sizeof(T));
            return out;
        }

        template <typename T>
        T DeserializePod(const std::byte* ptr)
        {
            T out{};
            std::memcpy(&out, ptr, sizeof(T));
            return out;
        }

        template <typename T>
        std::optional<T> CastToNumeric(const Value::Container& data)
        {
            return std::visit(
                Overloaded{
                    [](std::monostate) -> std::optional<T> { return std::nullopt; },
                    [](const std::string&) -> std::optional<T> { return std::nullopt; },
                    []<typename U>(U value) -> std::optional<T> {
                        if constexpr (!std::is_arithmetic_v<U>) {
                            return std::nullopt;
                        } else if constexpr (std::is_same_v<T, bool>) {
                            return static_cast<long double>(value) != 0.0L;
                        } else if constexpr (std::is_integral_v<T>) {
                            const long double as_long_double = static_cast<long double>(value);
                            if (!std::isfinite(as_long_double)) {
                                return std::nullopt;
                            }
                            if constexpr (std::is_floating_point_v<U>) {
                                if (std::trunc(as_long_double) != as_long_double) {
                                    return std::nullopt;
                                }
                            }

                            constexpr long double lower =
                                static_cast<long double>(std::numeric_limits<T>::min());
                            constexpr long double upper =
                                static_cast<long double>(std::numeric_limits<T>::max());
                            if (as_long_double < lower || as_long_double > upper) {
                                return std::nullopt;
                            }
                            return static_cast<T>(value);
                        } else if constexpr (std::is_floating_point_v<T>) {
                            const long double as_long_double = static_cast<long double>(value);
                            if (!std::isfinite(as_long_double)) {
                                return std::nullopt;
                            }
                            constexpr long double upper =
                                static_cast<long double>(std::numeric_limits<T>::max());
                            if (as_long_double < -upper || as_long_double > upper) {
                                return std::nullopt;
                            }
                            return static_cast<T>(value);
                        } else {
                            return std::nullopt;
                        }
                    }},
                data);
        }

        template <typename T>
        T GetOrThrowCast(const Value& value, TypeId target_type)
        {
            auto casted = CastToNumeric<T>(value.Raw());
            if (!casted.has_value()) {
                throw std::invalid_argument(
                    "Value::Serialize: value type " + std::string(TypeUtil::TypeName(value.Type())) +
                    " cannot cast to " + std::string(TypeUtil::TypeName(target_type)));
            }
            return casted.value();
        }

        template <typename T>
        std::string FloatingToString(T value)
        {
            std::ostringstream oss;
            oss.precision(std::numeric_limits<T>::max_digits10);
            oss << value;
            return oss.str();
        }

        std::unexpected<ValueDeserializeErr> MakeDeserializeErr(
            ValueDeserializeErrCode code,
            std::string msg)
        {
            return std::unexpected(ValueDeserializeErr{
                .msg = std::move(msg),
                .err_code = code,
            });
        }
    } // namespace

    /**
     * @param target 目标类型
     */
    bool Value::CanCastTo(TypeId target) const noexcept
    {
        if (target == TypeId::INVALID) {
            return IsNull();
        }
        if (IsNull()) {
            return true;
        }
        if (target == TypeId::VARCHAR) {
            return true;
        }
        if (target == TypeId::BOOLEAN) {
            return CastToNumeric<bool>(data_).has_value();
        }
        if (target == TypeId::DECIMAL) {
            return false;
        }
        if (TypeUtil::IsNumeric(target)) {
            return std::visit(
                Overloaded{
                    [](const std::string&) { return false; },
                    [](std::monostate) { return true; },
                    []<typename U>(const U&) {
                        return std::is_arithmetic_v<std::decay_t<U>>;
                    }},
                data_);
        }
        return Type() == target;
    }

    /**
     * @note 仅数值类型支持提升
     */
    std::optional<long double> Value::AsLongDouble() const noexcept
    {
        return std::visit(
            Overloaded{
                [](std::monostate) -> std::optional<long double> { return std::nullopt; },
                [](const std::string&) -> std::optional<long double> { return std::nullopt; },
                []<typename U>(U value) -> std::optional<long double> {
                    if constexpr (std::is_arithmetic_v<U>) {
                        return static_cast<long double>(value);
                    } else {
                        return std::nullopt;
                    }
                }},
            data_);
    }

    /**
     * @param rhs 右值
     */
    std::partial_ordering Value::Compare(const Value& rhs) const noexcept
    {
        // step 1: NULL 参与比较时，只有 NULL == NULL 才等价，其余均无序。
        if (IsNull() || rhs.IsNull()) {
            if (IsNull() && rhs.IsNull()) {
                return std::partial_ordering::equivalent;
            }
            return std::partial_ordering::unordered;
        }

        // step 2: 若两侧都属于数值类，则统一提升到 long double 比较。
        const bool lhs_numeric = TypeUtil::IsNumeric(Type()) || Type() == TypeId::BOOLEAN;
        const bool rhs_numeric = TypeUtil::IsNumeric(rhs.Type()) || rhs.Type() == TypeId::BOOLEAN;
        if (lhs_numeric && rhs_numeric) {
            const auto lhs = AsLongDouble();
            const auto rhs_value = rhs.AsLongDouble();
            if (!lhs.has_value() || !rhs_value.has_value()) {
                return std::partial_ordering::unordered;
            }
            if (lhs.value() < rhs_value.value()) {
                return std::partial_ordering::less;
            }
            if (lhs.value() > rhs_value.value()) {
                return std::partial_ordering::greater;
            }
            return std::partial_ordering::equivalent;
        }

        // step 3: 若两侧都是字符串，则按字典序比较。
        const auto* lhs_str = TryAs<std::string>();
        const auto* rhs_str = rhs.TryAs<std::string>();
        if (lhs_str != nullptr && rhs_str != nullptr) {
            if (*lhs_str < *rhs_str) {
                return std::partial_ordering::less;
            }
            if (*lhs_str > *rhs_str) {
                return std::partial_ordering::greater;
            }
            return std::partial_ordering::equivalent;
        }

        // step 4: 其他不兼容类型返回无序。
        return std::partial_ordering::unordered;
    }

    std::string Value::ToString() const
    {
        return std::visit(
            Overloaded{
                [](std::monostate) { return std::string("NULL"); },
                [](bool value) { return value ? std::string("true") : std::string("false"); },
                [](int8_t value) { return std::to_string(static_cast<int>(value)); },
                [](int16_t value) { return std::to_string(value); },
                [](int32_t value) { return std::to_string(value); },
                [](int64_t value) { return std::to_string(value); },
                [](float value) { return FloatingToString(value); },
                [](double value) { return FloatingToString(value); },
                [](const std::string& value) { return value; }},
            data_);
    }

    /**
     * @param type 目标类型
     */
    std::vector<std::byte> Value::Serialize(TypeId type) const
    {
        // step 1: 检查目标类型是否合法；NULL 序列化为空字节序列。
        if (type == TypeId::INVALID) {
            throw std::invalid_argument("Value::Serialize: invalid target type");
        }
        if (IsNull()) {
            return {};
        }

        // step 2: 按目标类型执行转换并写出原始字节。
        switch (type) {
            case TypeId::BOOLEAN: {
                const uint8_t v = static_cast<uint8_t>(GetOrThrowCast<bool>(*this, type) ? 1 : 0);
                return SerializePod(v);
            }
            case TypeId::TINYINT:
                return SerializePod(GetOrThrowCast<int8_t>(*this, type));
            case TypeId::SMALLINT:
                return SerializePod(GetOrThrowCast<int16_t>(*this, type));
            case TypeId::INTEGER:
                return SerializePod(GetOrThrowCast<int32_t>(*this, type));
            case TypeId::BIGINT:
                return SerializePod(GetOrThrowCast<int64_t>(*this, type));
            case TypeId::FLOAT:
                return SerializePod(GetOrThrowCast<float>(*this, type));
            case TypeId::DOUBLE:
                return SerializePod(GetOrThrowCast<double>(*this, type));
            case TypeId::DECIMAL:
                throw std::invalid_argument(
                    "Value::Serialize: DECIMAL is not implemented yet; "
                    "requires fixed-point semantics");
            case TypeId::VARCHAR: {
                std::string text = std::visit(
                    Overloaded{
                        [](const std::string& value) { return value; },
                        [this](const auto&) { return ToString(); }},
                    data_);
                std::vector<std::byte> out(text.size());
                if (!text.empty()) {
                    std::memcpy(out.data(), text.data(), text.size());
                }
                return out;
            }
            case TypeId::INVALID:
                break;
        }

        return {};
    }

    /**
     * @param type 目标类型
     * @param ptr  数据起始地址
     * @param len  数据长度
     */
    std::expected<Value, ValueDeserializeErr> Value::TryDeserialize(
        TypeId type, const std::byte* ptr, size_t len)
    {
        // step 1: 检查目标类型与输入指针/长度组合是否合法。
        if (type == TypeId::INVALID) {
            return MakeDeserializeErr(
                ValueDeserializeErrCode::InvalidType,
                "Value::TryDeserialize: invalid target type");
        }
        if (len == 0) {
            if (type == TypeId::VARCHAR) {
                return Value::VarChar({});
            }
            return Value::Null();
        }
        if (ptr == nullptr) {
            return MakeDeserializeErr(
                ValueDeserializeErrCode::NullPointerWithNonZeroLength,
                "Value::TryDeserialize: null pointer with non-zero length");
        }

        // step 2: 按 TypeId 解释原始字节并构造 Value。
        switch (type) {
            case TypeId::BOOLEAN:
                if (len >= sizeof(uint8_t)) {
                    return Value::Boolean(DeserializePod<uint8_t>(ptr) != 0);
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: BOOLEAN buffer too short");
            case TypeId::TINYINT:
                if (len >= sizeof(int8_t)) {
                    return Value::Int8(DeserializePod<int8_t>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: TINYINT buffer too short");
            case TypeId::SMALLINT:
                if (len >= sizeof(int16_t)) {
                    return Value::Int16(DeserializePod<int16_t>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: SMALLINT buffer too short");
            case TypeId::INTEGER:
                if (len >= sizeof(int32_t)) {
                    return Value::Int32(DeserializePod<int32_t>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: INTEGER buffer too short");
            case TypeId::BIGINT:
                if (len >= sizeof(int64_t)) {
                    return Value::Int64(DeserializePod<int64_t>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: BIGINT buffer too short");
            case TypeId::FLOAT:
                if (len >= sizeof(float)) {
                    return Value::Float(DeserializePod<float>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: FLOAT buffer too short");
            case TypeId::DOUBLE:
                if (len >= sizeof(double)) {
                    return Value::Double(DeserializePod<double>(ptr));
                }
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::BufferTooShort,
                    "Value::TryDeserialize: DOUBLE buffer too short");
            case TypeId::DECIMAL:
                return MakeDeserializeErr(
                    ValueDeserializeErrCode::InvalidType,
                    "Value::TryDeserialize: DECIMAL is not implemented yet");
            case TypeId::VARCHAR: {
                std::string text(len, '\0');
                std::memcpy(text.data(), ptr, len);
                return Value::VarChar(std::move(text));
            }
            case TypeId::INVALID:
                break;
        }

        return MakeDeserializeErr(
            ValueDeserializeErrCode::InvalidType,
            "Value::TryDeserialize: unsupported type");
    }

    /**
     * @param type 目标类型
     * @param ptr  数据起始地址
     * @param len  数据长度
     * @note 失败时抛出异常
     */
    Value Value::Deserialize(TypeId type, const std::byte* ptr, size_t len)
    {
        auto parsed = TryDeserialize(type, ptr, len);
        if (!parsed.has_value()) {
            throw std::invalid_argument(parsed.error().msg);
        }
        return parsed.value();
    }

} // namespace type
} // namespace HaruhiDB