/**
 * CXX/src/include/catalog/column.h
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
    class Column {
    public:
        static constexpr uint32_t VARLEN_SLOT_SIZE = sizeof(uint32_t);

        Column(std::string name, type::TypeId type, bool nullable = true,
               std::optional<type::Value> default_value = std::nullopt);

        Column(std::string name, type::TypeId type, uint32_t length, bool nullable,
               std::optional<type::Value> default_value = std::nullopt);

        const std::string& Name() const noexcept { return name_; }
        type::TypeId Type() const noexcept { return type_; }
        uint32_t Length() const noexcept { return length_; }
        bool Nullable() const noexcept { return nullable_; }
        bool IsInlined() const noexcept { return inlined_; }
        bool IsVarlen() const noexcept { return !inlined_; }

        uint32_t Offset() const noexcept { return offset_; }
        void SetOffset(uint32_t offset) noexcept { offset_ = offset; }

        const std::optional<type::Value>& DefaultValue() const noexcept { return default_value_; }
        bool HasDefaultValue() const noexcept { return default_value_.has_value(); }

        uint32_t StorageSize() const noexcept { return inlined_ ? length_ : VARLEN_SLOT_SIZE; }
        std::string ToString() const;

        std::expected<void, std::string> Validate() const;

    private:
        std::string name_;
        type::TypeId type_{type::TypeId::INVALID};
        uint32_t length_{0};
        bool nullable_{true};
        uint32_t offset_{0};
        bool inlined_{true};
        std::optional<type::Value> default_value_;
    };
} // namespace catalog
} // namespace HaruhiDB
