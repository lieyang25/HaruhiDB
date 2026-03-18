#pragma once

#include "catalog/table_info.h"
#include "storage/record/rid.h"
#include "type/value.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace HaruhiDB
{
namespace execution::detail
{

std::expected<int32_t, std::string> ExtractPrimaryIndexKey(
    const catalog::Schema& schema,
    std::span<const type::Value> values);

std::vector<storage::BPlusTree*> CollectTableIndexes(catalog::TableInfo* table_info);

bool InsertIntoIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& rid,
    size_t* inserted_count = nullptr);

void RollbackInsertedIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    size_t inserted_count);

bool RemoveFromIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    std::vector<size_t>* removed_positions);

bool RollbackRemovedIndexesByKey(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& rid,
    std::span<const size_t> removed_positions);

bool RebindMovedRidInIndexes(
    std::span<storage::BPlusTree* const> indexes,
    int32_t key,
    const record::RID& old_rid,
    const record::RID& new_rid);

} // namespace execution::detail
} // namespace HaruhiDB
