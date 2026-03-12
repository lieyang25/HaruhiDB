/**
 * CXX/src/catalog/schema.cxx
 *
 * Schema 的实现
 *
 * 主要逻辑：
 *
 * 1 构建 column name map
 * 2 计算 column offset
 * 3 计算 tuple inline size
 * 4 记录 varlen column
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

    /**
     * 将列名统一转为小写
     *
     * SQL 中：
     *
     * SELECT NAME
     * SELECT name
     *
     * 是等价的
     */
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
    }

    /**
     * Schema 构造
     */
    Schema::Schema(std::vector<Column> columns)
        : columns_(std::move(columns))
    {
        BuildLayoutOrThrow();
    }

    /**
     * 安全构造
     */
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

    /**
     * 尝试获取 column index
     */
    std::optional<size_t> Schema::TryGetColumnIndex(std::string_view name) const noexcept
    {
        auto it = name_to_index_.find(NormalizeName(name));

        if (it == name_to_index_.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    /**
     * 获取 column index
     */
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

    /**
     * 返回所有 column name
     */
    std::vector<std::string> Schema::ColumnNames() const
    {
        std::vector<std::string> names;

        names.reserve(columns_.size());

        std::ranges::transform(columns_, std::back_inserter(names), [](const Column& column) {
            return column.Name();
        });

        return names;
    }

    std::string Schema::ToString() const
    {
        std::string text = "Schema[";
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) {
                text += ", ";
            }
            text += columns_[i].ToString();
        }
        text += "]";
        return text;
    }

    /**
     * Schema 投影
     *
     * SELECT a,b
     */
    Schema Schema::Project(std::span<const uint32_t> column_indices) const
    {
        std::vector<Column> projected;

        projected.reserve(column_indices.size());

        for (uint32_t idx : column_indices) {
            projected.push_back(GetColumn(idx));
        }

        return Schema(std::move(projected));
    }

    /**
     * 构建 tuple layout
     *
     * 这是 Schema 最核心的函数
     */
    void Schema::BuildLayoutOrThrow()
    {
        name_to_index_.clear();
        uninlined_columns_.clear();

        tuple_inlined_ = true;
        inlined_storage_size_ = 0;

        for (size_t i = 0; i < columns_.size(); i++) {

            /**
             * 验证 column
             */
            auto validated = columns_[i].Validate();

            if (!validated.has_value()) {
                throw std::invalid_argument(
                    "Schema: invalid column definition at index " +
                    std::to_string(i) +
                    ": " +
                    validated.error());
            }

            /**
             * 检查重复列名
             */
            std::string normalized_name = NormalizeName(columns_[i].Name());

            if (name_to_index_.contains(normalized_name)) {
                throw std::invalid_argument("Schema: duplicate column name: " + columns_[i].Name());
            }

            name_to_index_[std::move(normalized_name)] = i;

            /**
             * 计算 offset
             */
            columns_[i].SetOffset(inlined_storage_size_);

            /**
             * 更新 tuple size
             */
            const uint64_t next_size =
                static_cast<uint64_t>(inlined_storage_size_) +
                columns_[i].StorageSize();

            if (next_size > std::numeric_limits<uint32_t>::max()) {
                throw std::overflow_error("Schema: inlined storage size overflow");
            }

            inlined_storage_size_ = static_cast<uint32_t>(next_size);

            /**
             * 如果是 varlen column
             */
            if (!columns_[i].IsInlined()) {

                tuple_inlined_ = false;

                uninlined_columns_.push_back(static_cast<uint32_t>(i));
            }
        }
    }

} // namespace catalog
} // namespace HaruhiDB
