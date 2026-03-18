#include "execution/update_executor.h"

#include "execution/index_maintenance.h"
#include "storage/record/tuple_codec.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace HaruhiDB
{
namespace execution
{

UpdateExecutor::UpdateExecutor(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::unique_ptr<AbstractExecutor> child,
    Updater updater)
    : AbstractExecutor(exec_ctx),
      table_info_(table_info),
      child_(std::move(child)),
      updater_(std::move(updater))
{
}

void UpdateExecutor::Init()
{
    initialized_ = true;
    done_ = false;
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool UpdateExecutor::Next(ExecutorRow* row)
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

    int32_t updated_count = 0;
    ExecutorRow input;
    while (child_->Next(&input)) {
        if (!input.has_rid) {
            return false;
        }

        std::optional<int32_t> old_key;
        if (!indexes.empty()) {
            auto old_key_exp = detail::ExtractPrimaryIndexKey(schema, input.values);
            if (!old_key_exp.has_value()) {
                return false;
            }
            old_key = old_key_exp.value();
        }

        std::vector<type::Value> next_values = updater_ ? updater_(input) : input.values;
        if (!indexes.empty()) {
            auto new_key_exp = detail::ExtractPrimaryIndexKey(schema, next_values);
            if (!new_key_exp.has_value()) {
                return false;
            }
            if (new_key_exp.value() != old_key.value()) {
                return false;
            }
        }

        auto old_tuple_exp = record::TupleCodec::Encode(schema, input.values);
        if (!old_tuple_exp.has_value()) {
            return false;
        }

        auto tuple_exp = record::TupleCodec::Encode(schema, next_values);
        if (!tuple_exp.has_value()) {
            return false;
        }

        record::RID new_rid;
        if (!table_heap->UpdateTuple(input.rid, tuple_exp.value(), &new_rid)) {
            return false;
        }

        if (!indexes.empty() && new_rid != input.rid) {
            if (!detail::RebindMovedRidInIndexes(indexes, old_key.value(), input.rid, new_rid)) {
                // Best-effort compensation for moved updates: restore old tuple content and index key mapping.
                if (table_heap->DeleteTuple(new_rid)) {
                    record::RID restored_rid;
                    if (table_heap->InsertTuple(old_tuple_exp.value(), &restored_rid)) {
                        std::vector<size_t> removed_positions;
                        (void)detail::RemoveFromIndexesByKey(indexes, old_key.value(), &removed_positions);
                        (void)detail::InsertIntoIndexesByKey(indexes, old_key.value(), restored_rid);
                    }
                }
                return false;
            }
        }

        ++updated_count;
    }

    row->values = {type::Value::Int32(updated_count)};
    row->rid = record::RID{};
    row->has_rid = false;
    done_ = true;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
