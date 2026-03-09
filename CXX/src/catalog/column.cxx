/**
 * CXX/src/catalog/column.cxx
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
        void ThrowIfInvalidColumnDefinition(const Column& column)
        {
            auto result = column.Validate();
            if (!result.has_value()) {
                throw std::invalid_argument(result.error());
            }
        }
    } // namespace

    Column::Column(std::string name, type::TypeId type, bool nullable, std::optional<type::Value> default_value)
        : name_(std::move(name)), type_(type), nullable_(nullable), default_value_(std::move(default_value))
    {
        const int fixed_size = type::TypeUtil::FixedLengthSize(type_);
        if (fixed_size == type::TypeUtil::VARIABLE_LENGTH) {
            throw std::invalid_argument("Column: variable-length type requires explicit length");
        }
        if (fixed_size <= 0) {
            throw std::invalid_argument("Column: invalid fixed-length type");
        }

        length_ = static_cast<uint32_t>(fixed_size);
        inlined_ = true;
        ThrowIfInvalidColumnDefinition(*this);
    }

    Column::Column(
        std::string name, type::TypeId type, uint32_t length, bool nullable, std::optional<type::Value> default_value)
        : name_(std::move(name)), type_(type), length_(length), nullable_(nullable), default_value_(std::move(default_value))
    {
        inlined_ = !type::TypeUtil::IsVariableLength(type_);

        if (inlined_) {
            const int fixed_size = type::TypeUtil::FixedLengthSize(type_);
            if (fixed_size <= 0) {
                throw std::invalid_argument("Column: invalid fixed-length type");
            }
            if (length_ != static_cast<uint32_t>(fixed_size)) {
                throw std::invalid_argument("Column: fixed-length column size mismatch");
            }
        } else if (length_ == 0) {
            throw std::invalid_argument("Column: variable-length column size must be positive");
        }

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
        if (name_.empty()) {
            return std::unexpected("Column: name must not be empty");
        }

        if (!type::TypeUtil::IsValid(type_) || type_ == type::TypeId::INVALID) {
            return std::unexpected("Column: invalid type id");
        }

        const bool should_be_inlined = !type::TypeUtil::IsVariableLength(type_);
        if (should_be_inlined != inlined_) {
            return std::unexpected("Column: inlined flag does not match type");
        }

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

        if (!default_value_.has_value()) {
            return {};
        }

        const type::Value& default_value = default_value_.value();
        if (default_value.IsNull()) {
            if (!nullable_) {
                return std::unexpected("Column: NULL default value is not allowed for NOT NULL column");
            }
            return {};
        }

        if (!default_value.CanCastTo(type_)) {
            return std::unexpected("Column: default value cannot cast to column type");
        }

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
