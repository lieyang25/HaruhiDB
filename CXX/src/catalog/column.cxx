/**
 * CXX/src/catalog/column.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 Column 的构造、校验与字符串化逻辑。
 *
 * 主要完成：
 *
 * 1. 固定长度列构造
 * 2. 变长列构造
 * 3. 列定义合法性校验
 * 4. SQL 风格字符串输出
 *
 *
 * ========================= 实现说明 =========================
 *
 * Column 在构造阶段就尽量保证自身合法。
 *
 * 若定义不合法，则构造函数直接抛出异常；
 * Validate 则提供非异常形式的校验接口。
 */

#include "catalog/column.h"

#include <stdexcept>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
        /**
         * 若列定义不合法则直接抛异常。
         */
        void ThrowIfInvalidColumnDefinition(const Column& column)
        {
            auto result = column.Validate();
            if (!result.has_value()) {
                throw std::invalid_argument(result.error());
            }
        }
    } // namespace

    /**
     * @param name          列名
     * @param type          列类型
     * @param nullable      是否允许 NULL
     * @param default_value 默认值
     */
    Column::Column(
        std::string name,
        type::TypeId type,
        bool nullable,
        std::optional<type::Value> default_value)
        : name_(std::move(name)), type_(type), nullable_(nullable), default_value_(std::move(default_value))
    {
        // step 1: 读取固定长度类型的物理大小。
        const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

        // step 2: 固定长度构造函数不允许处理变长类型。
        if (fixed_size == type::TypeUtil::VARIABLE_LENGTH) {
            throw std::invalid_argument("Column: variable-length type requires explicit length");
        }

        // step 3: 检查固定长度是否合法。
        if (fixed_size <= 0) {
            throw std::invalid_argument("Column: invalid fixed-length type");
        }

        // step 4: 写入长度并标记为内联列。
        length_ = static_cast<uint32_t>(fixed_size);
        inlined_ = true;

        // step 5: 统一校验列定义。
        ThrowIfInvalidColumnDefinition(*this);
    }

    /**
     * @param name          列名
     * @param type          列类型
     * @param length        列长度或最大长度
     * @param nullable      是否允许 NULL
     * @param default_value 默认值
     */
    Column::Column(
        std::string name,
        type::TypeId type,
        uint32_t length,
        bool nullable,
        std::optional<type::Value> default_value)
        : name_(std::move(name)),
          type_(type),
          length_(length),
          nullable_(nullable),
          default_value_(std::move(default_value))
    {
        // step 1: 根据类型判断该列是否应为内联列。
        inlined_ = !type::TypeUtil::IsVariableLength(type_);

        // step 2: 固定长度列要求 length 与类型固有长度一致。
        if (inlined_) {
            const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

            if (fixed_size <= 0) {
                throw std::invalid_argument("Column: invalid fixed-length type");
            }

            if (length_ != static_cast<uint32_t>(fixed_size)) {
                throw std::invalid_argument("Column: fixed-length column size mismatch");
            }
        }
        // step 3: 变长列必须显式给出正长度。
        else if (length_ == 0) {
            throw std::invalid_argument("Column: variable-length column size must be positive");
        }

        // step 4: 统一校验列定义。
        ThrowIfInvalidColumnDefinition(*this);
    }

    std::string Column::ToString() const
    {
        std::string output = name_ + " " + std::string(type::TypeUtil::TypeName(type_));

        if (type_ == type::TypeId::VARCHAR) {
            output += "(" + std::to_string(length_) + ")";
        }

        output += nullable_ ? " NULL" : " NOT NULL";

        if (default_value_.has_value()) {
            output += " DEFAULT ";
            output += default_value_->ToString();
        }

        return output;
    }

    std::expected<void, std::string> Column::Validate() const
    {
        // step 1: 基础字段检查。
        if (name_.empty()) {
            return std::unexpected("Column: name must not be empty");
        }

        if (!type::TypeUtil::IsValid(type_) || type_ == type::TypeId::INVALID) {
            return std::unexpected("Column: invalid type id");
        }

        // step 2: 检查 inlined_ 与类型类别是否一致。
        const bool should_be_inlined = !type::TypeUtil::IsVariableLength(type_);
        if (should_be_inlined != inlined_) {
            return std::unexpected("Column: inlined flag does not match type");
        }

        // step 3: 校验长度是否合法。
        if (inlined_) {
            const int fixed_size = type::TypeUtil::FixedLengthSize(type_);

            if (fixed_size <= 0) {
                return std::unexpected("Column: invalid fixed-length type");
            }

            if (length_ != static_cast<uint32_t>(fixed_size)) {
                return std::unexpected("Column: fixed-length column size mismatch");
            }
        } else if (length_ == 0) {
            return std::unexpected("Column: variable-length column size must be positive");
        }

        // step 4: 若无默认值，则定义已经合法。
        if (!default_value_.has_value()) {
            return {};
        }

        const type::Value& default_value = default_value_.value();

        // step 5: NULL 默认值仅允许出现在 nullable 列上。
        if (default_value.IsNull()) {
            if (!nullable_) {
                return std::unexpected("Column: NULL default value is not allowed for NOT NULL column");
            }
            return {};
        }

        // step 6: 非 NULL 默认值必须能转换到列类型。
        if (!default_value.CanCastTo(type_)) {
            return std::unexpected("Column: default value cannot cast to column type");
        }

        // step 7: VARCHAR 默认值还要满足长度限制。
        if (type_ == type::TypeId::VARCHAR) {
            const auto serialized = default_value.Serialize(type_);
            if (serialized.size() > length_) {
                return std::unexpected("Column: default VARCHAR value exceeds column length");
            }
        }

        return {};
    }

} // namespace catalog
} // namespace HaruhiDB