/**
 * CXX/src/include/type/value.h
 *
 * ========================= 设计目标 =========================
 *
 * Value 表示数据库系统中的一个运行时值。
 *
 * 当前实现支持：
 *
 * - NULL
 * - BOOLEAN
 * - TINYINT / SMALLINT / INTEGER / BIGINT
 * - FLOAT / DOUBLE
 * - VARCHAR
 *
 * DECIMAL 暂未完整实现。
 *
 *
 * ========================= 为什么需要 Value =========================
 *
 * TypeId 只描述“值的类型”，
 * 而系统在执行表达式、比较、序列化、反序列化时，
 * 还需要一个真正承载具体数据的对象。
 *
 * Value 负责统一表示：
 *
 * - 列默认值
 * - 表达式计算结果
 * - 记录解码后的字段值
 * - 序列化/反序列化的中间对象
 *
 *
 * ========================= Value 在系统中的位置 =========================
 *
 * Column
 *   └── default_value : Value
 *
 * Schema / TupleCodec
 *   └── 编解码字段值
 *
 * Executor / Expression
 *   └── 运行时计算与比较
 *
 *
 * ========================= 当前实现语义 =========================
 *
 * 1. NULL 使用 std::monostate 表示
 * 2. 底层存储使用 std::variant
 * 3. 数值类型之间允许一定范围内的转换
 * 4. VARCHAR 以 std::string 持有
 * 5. Compare 使用 partial_ordering，因为：
 *    - NULL 与非 NULL 无序
 *    - 不兼容类型之间也可能无序
 */

#pragma once

#include "type/type.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace HaruhiDB
{
namespace type
{
    enum class ValueDeserializeErrCode : int {
        InvalidType = 1,
        NullPointerWithNonZeroLength,
        BufferTooShort
    };

    /**
     * Value 反序列化相关错误。
     */
    struct ValueDeserializeErr
    {
        std::string msg;
        ValueDeserializeErrCode err_code;
    };

    class Value
    {
    public:
        /**
         * @note monostate 表示 NULL
         */
        using Container = std::variant<
            std::monostate,
            bool,
            int8_t,
            int16_t,
            int32_t,
            int64_t,
            float,
            double,
            std::string>;

        Value() = default;

        explicit Value(bool v) : data_(v) {}
        explicit Value(int8_t v) : data_(v) {}
        explicit Value(int16_t v) : data_(v) {}
        explicit Value(int32_t v) : data_(v) {}
        explicit Value(int64_t v) : data_(v) {}
        explicit Value(float v) : data_(v) {}
        explicit Value(double v) : data_(v) {}
        explicit Value(std::string v) : data_(std::move(v)) {}

        /**
         * 构造 NULL 值。
         */
        static Value Null() { return Value(); }

        /**
         * 构造 BOOLEAN 值。
         */
        static Value Boolean(bool b) { return Value(b); }

        /**
         * 构造 TINYINT 值。
         */
        static Value Int8(int8_t i) { return Value(i); }

        /**
         * 构造 SMALLINT 值。
         */
        static Value Int16(int16_t i) { return Value(i); }

        /**
         * 构造 INTEGER 值。
         */
        static Value Int32(int32_t i) { return Value(i); }

        /**
         * 构造 BIGINT 值。
         */
        static Value Int64(int64_t i) { return Value(i); }

        /**
         * 构造 FLOAT 值。
         */
        static Value Float(float f) { return Value(f); }

        /**
         * 构造 DOUBLE 值。
         */
        static Value Double(double d) { return Value(d); }

        /**
         * 构造 VARCHAR 值。
         */
        static Value VarChar(std::string s) { return Value(std::move(s)); }

        /**
         * 判断当前值是否为 NULL。
         */
        bool IsNull() const noexcept
        {
            return std::holds_alternative<std::monostate>(data_);
        }

        /**
         * 返回当前值的逻辑类型。
         *
         * @note NULL 当前映射为 TypeId::INVALID
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

        /**
         * 返回底层 variant 容器。
         */
        const Container& Raw() const noexcept { return data_; }

        /**
         * 尝试按指定类型读取值。
         *
         * @tparam T 目标类型
         * @return 成功返回指针，否则返回 nullptr
         */
        template <typename T>
        const T* TryAs() const noexcept
        {
            return std::get_if<T>(&data_);
        }

        /**
         * 尝试按指定类型读取值。
         *
         * @tparam T 目标类型
         * @return 成功返回指针，否则返回 nullptr
         */
        template <typename T>
        T* TryAs() noexcept
        {
            return std::get_if<T>(&data_);
        }

        /**
         * 判断当前值是否可转换到目标类型。
         *
         * @param target 目标类型
         */
        bool CanCastTo(TypeId target) const noexcept;

        /**
         * 尝试把当前值提升为 long double。
         *
         * @note 仅数值类值支持
         */
        std::optional<long double> AsLongDouble() const noexcept;

        /**
         * 比较两个 Value。
         *
         * @param rhs 右值
         * @return partial_ordering 比较结果
         */
        std::partial_ordering Compare(const Value& rhs) const noexcept;

        /**
         * 转为字符串表示。
         */
        std::string ToString() const;

        /**
         * 按目标类型序列化当前值。
         *
         * @param type 目标类型
         */
        std::vector<std::byte> Serialize(TypeId type) const;

        /**
         * 尝试从字节区间反序列化 Value。
         *
         * @param type 目标类型
         * @param ptr  数据起始地址
         * @param len  数据长度
         */
        static std::expected<Value, ValueDeserializeErr> TryDeserialize(
            TypeId type, const std::byte* ptr, size_t len);

        /**
         * 从字节区间反序列化 Value。
         *
         * @param type 目标类型
         * @param ptr  数据起始地址
         * @param len  数据长度
         * @note 失败时抛出异常
         */
        static Value Deserialize(TypeId type, const std::byte* ptr, size_t len);

        friend bool operator==(const Value&, const Value&) = default;

    private:
        Container data_{std::monostate{}};
    };

} // namespace type
} // namespace HaruhiDB