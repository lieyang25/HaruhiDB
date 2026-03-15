#include "execution/delete_executor.h"

#include <cstdint>
#include <utility>

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

    auto* table_heap = table_info_->GetTableHeap();

    int32_t deleted_count = 0;
    ExecutorRow input;
    while (child_->Next(&input)) {
        if (!input.has_rid) {
            return false;
        }
        if (!table_heap->DeleteTuple(input.rid)) {
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
