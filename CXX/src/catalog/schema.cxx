/**
 * CXX/src/catalog/schema.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 Schema 的结构构建与查询逻辑。
 *
 * 主要完成：
 *
 * 1. 列名标准化与列名查找
 * 2. 构建 column name -> index 映射
 * 3. 计算各列 offset
 * 4. 统计 tuple inline 区大小
 * 5. 收集变长列下标
 * 6. 支持 schema 投影
 *
 *
 * ========================= 核心机制 =========================
 *
 * BuildLayoutOrThrow:
 *   遍历所有列
 *     -> 校验 Column 合法性
 *     -> 检查重名
 *     -> 写入 name_to_index_
 *     -> 计算 offset
 *     -> 更新 inline 大小
 *     -> 收集变长列
 *
 * 列名查找：
 *   所有名称先做 NormalizeName
 *   再在 name_to_index_ 中查找
 *
 *
 * ========================= 实现说明 =========================
 *
 * Schema 在构造完成后，
 * columns_、name_to_index_、uninlined_columns_、
 * tuple_inlined_、inlined_storage_size_ 需要保持一致。
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
        /**
         * 将列名标准化为小写形式。
         *
         * @param name 原始列名
         * @return 统一后的小写列名
         */
        std::string NormalizeName(std::string_view name)
        {
            std::string lowered(name);

            std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            return lowered;
        }
    } // namespace

    /**
     * @param columns 列定义集合
     */
    Schema::Schema(std::vector<Column> columns)
        : columns_(std::move(columns))
    {
        BuildLayoutOrThrow();
    }

    /**
     * @param columns 列定义集合
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
     * @param name 列名
     * @return 找到则返回列下标，否则返回 nullopt
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
     * @param name 列名
     * @note 不存在时抛异常
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
     * 返回所有列名。
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
     * @param column_indices 需要保留的列下标集合
     */
    Schema Schema::Project(std::span<const uint32_t> column_indices) const
    {
        // step 1: 按给定下标顺序拷贝目标列。
        std::vector<Column> projected;
        projected.reserve(column_indices.size());

        for (uint32_t idx : column_indices) {
            projected.push_back(GetColumn(idx));
        }

        // step 2: 用投影后的列重新构造 Schema。
        return Schema(std::move(projected));
    }

    /**
     * 构建 Schema 的 layout 与辅助索引。
     */
    void Schema::BuildLayoutOrThrow()
    {
        // step 1: 清空旧状态，准备重新构建。
        name_to_index_.clear();
        uninlined_columns_.clear();
        tuple_inlined_ = true;
        inlined_storage_size_ = 0;

        // step 2: 逐列校验、登记名称、计算 offset 与 inline 布局。
        for (size_t i = 0; i < columns_.size(); i++) {
            auto validated = columns_[i].Validate();
            if (!validated.has_value()) {
                throw std::invalid_argument(
                    "Schema: invalid column definition at index " +
                    std::to_string(i) +
                    ": " +
                    validated.error());
            }

            std::string normalized_name = NormalizeName(columns_[i].Name());
            if (name_to_index_.contains(normalized_name)) {
                throw std::invalid_argument("Schema: duplicate column name: " + columns_[i].Name());
            }
            name_to_index_[std::move(normalized_name)] = i;

            columns_[i].SetOffset(inlined_storage_size_);

            const uint64_t next_size =
                static_cast<uint64_t>(inlined_storage_size_) +
                columns_[i].StorageSize();

            if (next_size > std::numeric_limits<uint32_t>::max()) {
                throw std::overflow_error("Schema: inlined storage size overflow");
            }

            inlined_storage_size_ = static_cast<uint32_t>(next_size);

            // step 3: 收集变长列信息，并更新 tuple_inlined_ 标记。
            if (!columns_[i].IsInlined()) {
                tuple_inlined_ = false;
                uninlined_columns_.push_back(static_cast<uint32_t>(i));
            }
        }
    }

} // namespace catalog
} // namespace HaruhiDB