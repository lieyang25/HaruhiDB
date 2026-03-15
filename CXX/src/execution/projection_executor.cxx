#include "execution/projection_executor.h"

#include <utility>

namespace HaruhiDB
{
namespace execution
{

ProjectionExecutor::ProjectionExecutor(
    ExecutorContext* exec_ctx,
    std::unique_ptr<AbstractExecutor> child,
    std::vector<uint32_t> column_indices)
    : AbstractExecutor(exec_ctx),
      child_(std::move(child)),
      column_indices_(std::move(column_indices))
{
}

void ProjectionExecutor::Init()
{
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool ProjectionExecutor::Next(ExecutorRow* row)
{
    if (row == nullptr || child_ == nullptr) {
        return false;
    }

    ExecutorRow input;
    if (!child_->Next(&input)) {
        return false;
    }

    std::vector<type::Value> projected;
    projected.reserve(column_indices_.size());
    for (const uint32_t index : column_indices_) {
        if (index >= input.values.size()) {
            return false;
        }
        projected.push_back(input.values[index]);
    }

    row->values = std::move(projected);
    row->rid = input.rid;
    row->has_rid = input.has_rid;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
