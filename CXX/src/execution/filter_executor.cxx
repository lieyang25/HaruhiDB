#include "execution/filter_executor.h"

#include <utility>

namespace HaruhiDB
{
namespace execution
{

FilterExecutor::FilterExecutor(
    ExecutorContext* exec_ctx,
    std::unique_ptr<AbstractExecutor> child,
    Predicate predicate)
    : AbstractExecutor(exec_ctx), child_(std::move(child)), predicate_(std::move(predicate))
{
}

void FilterExecutor::Init()
{
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool FilterExecutor::Next(ExecutorRow* row)
{
    if (row == nullptr || child_ == nullptr) {
        return false;
    }

    ExecutorRow candidate;
    while (child_->Next(&candidate)) {
        if (!predicate_ || predicate_(candidate)) {
            *row = std::move(candidate);
            return true;
        }
    }
    return false;
}

} // namespace execution
} // namespace HaruhiDB
