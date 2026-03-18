#include "execution/delete_executor.h"

#include "execution/index_maintenance.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace execution
{

DeleteExecutor::DeleteExecutor(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::unique_ptr<AbstractExecutor> child)
    : AbstractExecutor(exec_ctx),
      table_info_(table_info),
      child_(std::move(child))
{
}

void DeleteExecutor::Init()
{
    initialized_ = true;
    done_ = false;
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool DeleteExecutor::Next(ExecutorRow* row)
{
    if (!initialized_) {
        Init();
    }

    if (row == nullptr || done_) {
        return false;
    }

    if (table_info_ == nullptr || table_info_->GetTableHeap() == nullptr || child_ == nullptr) {
        return false;
    }

    const auto& schema = table_info_->GetSchema();
    auto* table_heap = table_info_->GetTableHeap();
    const auto indexes = detail::CollectTableIndexes(table_info_);

    int32_t deleted_count = 0;
    ExecutorRow input;
    while (child_->Next(&input)) {
        if (!input.has_rid) {
            return false;
        }

        std::optional<int32_t> key;
        if (!indexes.empty()) {
            auto key_exp = detail::ExtractPrimaryIndexKey(schema, input.values);
            if (!key_exp.has_value()) {
                return false;
            }
            key = key_exp.value();
        }

        std::vector<size_t> removed_positions;
        if (key.has_value() &&
            !detail::RemoveFromIndexesByKey(indexes, key.value(), &removed_positions)) {
            return false;
        }
        if (!table_heap->DeleteTuple(input.rid)) {
            if (key.has_value()) {
                (void)detail::RollbackRemovedIndexesByKey(
                    indexes, key.value(), input.rid, removed_positions);
            }
            return false;
        }
        ++deleted_count;
    }

    row->values = {type::Value::Int32(deleted_count)};
    row->rid = record::RID{};
    row->has_rid = false;
    done_ = true;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
