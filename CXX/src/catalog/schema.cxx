/**
 * CXX/src/catalog/schema.cxx
 */

#include "catalog/schema.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
        std::string NormalizeName(std::string_view name)
        {
            std::string lowered(name);
            std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return lowered;
        }
    } // namespace

    Schema::Schema(std::vector<Column> columns)
        : columns_(std::move(columns))
    {
        BuildLayoutOrThrow();
    }

    std::expected<Schema, std::string> Schema::Create(std::vector<Column> columns)
    {
        try {
            return Schema(std::move(columns));
        } catch (const std::exception& e) {
            return std::unexpected(std::string(e.what()));
        }
    }

    const Column& Schema::GetColumn(size_t index) const
    {
        return columns_.at(index);
    }

    Column& Schema::GetColumn(size_t index)
    {
        return columns_.at(index);
    }

    std::optional<size_t> Schema::TryGetColumnIndex(std::string_view name) const noexcept
    {
        auto it = name_to_index_.find(NormalizeName(name));
        if (it == name_to_index_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    size_t Schema::GetColumnIndex(std::string_view name) const
    {
        const auto idx = TryGetColumnIndex(name);
        if (!idx.has_value()) {
            throw std::out_of_range("Schema: column not found: " + std::string(name));
        }
        return idx.value();
    }

    bool Schema::HasColumn(std::string_view name) const noexcept
    {
        return TryGetColumnIndex(name).has_value();
    }

    const Column* Schema::FindColumn(std::string_view name) const noexcept
    {
        const auto idx = TryGetColumnIndex(name);
        if (!idx.has_value()) {
            return nullptr;
        }
        return &columns_[idx.value()];
    }

    Column* Schema::FindColumn(std::string_view name) noexcept
    {
        const auto idx = TryGetColumnIndex(name);
        if (!idx.has_value()) {
            return nullptr;
        }
        return &columns_[idx.value()];
    }

    std::vector<std::string> Schema::ColumnNames() const
    {
        std::vector<std::string> names;
        names.reserve(columns_.size());
        std::ranges::transform(columns_, std::back_inserter(names), [](const Column& column) {
            return column.Name();
        });
        return names;
    }

    Schema Schema::Project(std::span<const uint32_t> column_indices) const
    {
        std::vector<Column> projected;
        projected.reserve(column_indices.size());
        for (uint32_t idx : column_indices) {
            projected.push_back(GetColumn(idx));
        }
        return Schema(std::move(projected));
    }

    void Schema::BuildLayoutOrThrow()
    {
        name_to_index_.clear();
        uninlined_columns_.clear();
        tuple_inlined_ = true;
        inlined_storage_size_ = 0;

        for (size_t i = 0; i < columns_.size(); i++) {
            auto validated = columns_[i].Validate();
            if (!validated.has_value()) {
                throw std::invalid_argument("Schema: invalid column definition at index " + std::to_string(i) + ": " +
                                            validated.error());
            }

            std::string normalized_name = NormalizeName(columns_[i].Name());
            if (name_to_index_.contains(normalized_name)) {
                throw std::invalid_argument("Schema: duplicate column name: " + columns_[i].Name());
            }
            name_to_index_[std::move(normalized_name)] = i;

            columns_[i].SetOffset(inlined_storage_size_);
            const uint64_t next_size = static_cast<uint64_t>(inlined_storage_size_) + columns_[i].StorageSize();
            if (next_size > std::numeric_limits<uint32_t>::max()) {
                throw std::overflow_error("Schema: inlined storage size overflow");
            }
            inlined_storage_size_ = static_cast<uint32_t>(next_size);

            if (!columns_[i].IsInlined()) {
                tuple_inlined_ = false;
                uninlined_columns_.push_back(static_cast<uint32_t>(i));
            }
        }
    }
} // namespace catalog
} // namespace HaruhiDB
