/**
 * CXX/src/include/catalog/schema.h
 */

#pragma once

#include "catalog/column.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HaruhiDB
{
namespace catalog
{
    class Schema {
    public:
        Schema() = default;
        explicit Schema(std::vector<Column> columns);

        static std::expected<Schema, std::string> Create(std::vector<Column> columns);

        const std::vector<Column>& Columns() const noexcept { return columns_; }

        size_t ColumnCount() const noexcept { return columns_.size(); }
        bool Empty() const noexcept { return columns_.empty(); }

        const Column& GetColumn(size_t index) const;
        Column& GetColumn(size_t index);

        std::optional<size_t> TryGetColumnIndex(std::string_view name) const noexcept;
        size_t GetColumnIndex(std::string_view name) const;
        bool HasColumn(std::string_view name) const noexcept;

        const Column* FindColumn(std::string_view name) const noexcept;
        Column* FindColumn(std::string_view name) noexcept;

        bool IsTupleInlined() const noexcept { return tuple_inlined_; }
        uint32_t InlinedStorageSize() const noexcept { return inlined_storage_size_; }
        const std::vector<uint32_t>& UninlinedColumns() const noexcept { return uninlined_columns_; }

        std::vector<std::string> ColumnNames() const;
        Schema Project(std::span<const uint32_t> column_indices) const;

    private:
        void BuildLayoutOrThrow();

        std::vector<Column> columns_;
        std::unordered_map<std::string, size_t> name_to_index_;
        std::vector<uint32_t> uninlined_columns_;
        bool tuple_inlined_{true};
        uint32_t inlined_storage_size_{0};
    };
} // namespace catalog
} // namespace HaruhiDB
