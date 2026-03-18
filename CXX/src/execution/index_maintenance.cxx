#include "execution/index_maintenance.h"

#include <algorithm>

namespace HaruhiDB
{
namespace execution::detail
{

std::expected<int32_t, std::string> ExtractPrimaryIndexKey(
    const catalog::Schema& schema,
    std::span<const type::Value> values)
{
    if (schema.ColumnCount() == 0) {
        return std::unexpected("indexed table schema has no columns");
    }
    const auto& key_column = schema.GetColumn(0);
    if (key_column.Type() != type::TypeId::INTEGER) {
        return std::unexpected("indexed key (first column) must be INTEGER");
    }
    if (key_column.Nullable()) {
        return std::unexpected("indexed key (first column) must be NOT NULL");
    }
    if (values.empty()) {
        return std::unexpected("executor row has no values for indexed key");
    }

    const int32_t* key = values[0].TryAs<int32_t>();
    if (key == nullptr) {
        return std::unexpected("indexed key value is not INTEGER");
    }
    return *key;
}

std::vector<storage::BPlusTree*> CollectTableIndexes(catalog::TableInfo* table_info)
{
    std::vector<storage::BPlusTree*> indexes;
    if (table_info == nullptr) {
        return indexes;
    }

    indexes.reserve(table_info->IndexEntries().size());
    for (const auto& entry : table_info->IndexEntries()) {
        indexes.push_back(entry.index.get());
    }
    return indexes;
}

void RollbackInsertedIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    size_t inserted_count)
{
    const size_t rollback_count = std::min(inserted_count, indexes.size());
    for (size_t i = rollback_count; i > 0; --i) {
        storage::BPlusTree* index = indexes[i - 1];
        if (index != nullptr) {
            (void)index->Remove(key);
        }
    }
}

bool InsertIntoIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& rid,
    size_t* inserted_count)
{
    size_t inserted = 0;
    for (storage::BPlusTree* index : indexes) {
        if (index == nullptr) {
            RollbackInsertedIndexesByKey(indexes, key, inserted);
            return false;
        }
        if (!index->Insert(key, rid)) {
            RollbackInsertedIndexesByKey(indexes, key, inserted);
            return false;
        }
        ++inserted;
    }

    if (inserted_count != nullptr) {
        *inserted_count = inserted;
    }
    return true;
}

bool RemoveFromIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    std::vector<size_t>* removed_positions)
{
    if (removed_positions != nullptr) {
        removed_positions->clear();
    }

    for (size_t i = 0; i < indexes.size(); ++i) {
        storage::BPlusTree* index = indexes[i];
        if (index == nullptr) {
            return false;
        }
        if (index->Remove(key) && removed_positions != nullptr) {
            removed_positions->push_back(i);
        }
    }
    return true;
}

bool RollbackRemovedIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& rid,
    std::span<const size_t> removed_positions)
{
    bool all_ok = true;
    for (size_t i = removed_positions.size(); i > 0; --i) {
        const size_t pos = removed_positions[i - 1];
        if (pos >= indexes.size()) {
            all_ok = false;
            continue;
        }

        storage::BPlusTree* index = indexes[pos];
        if (index == nullptr || !index->Insert(key, rid)) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool RebindMovedRidInIndexes(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& old_rid,
    const record::RID& new_rid)
{
    std::vector<bool> rebound(indexes.size(), false);

    for (size_t i = 0; i < indexes.size(); ++i) {
        storage::BPlusTree* index = indexes[i];
        if (index == nullptr) {
            for (size_t j = i; j > 0; --j) {
                if (rebound[j - 1] && indexes[j - 1] != nullptr) {
                    (void)indexes[j - 1]->Remove(key);
                    (void)indexes[j - 1]->Insert(key, old_rid);
                }
            }
            return false;
        }

        const bool removed = index->Remove(key);
        if (!index->Insert(key, new_rid)) {
            if (removed) {
                (void)index->Insert(key, old_rid);
            }
            for (size_t j = i; j > 0; --j) {
                if (rebound[j - 1] && indexes[j - 1] != nullptr) {
                    (void)indexes[j - 1]->Remove(key);
                    (void)indexes[j - 1]->Insert(key, old_rid);
                }
            }
            return false;
        }

        rebound[i] = true;
    }

    return true;
}

} // namespace execution::detail
} // namespace HaruhiDB
