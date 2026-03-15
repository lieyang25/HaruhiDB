#pragma once

#include "execution/executor.h"

#include <functional>
#include <memory>

namespace HaruhiDB
{
namespace execution
{

class FilterExecutor : public AbstractExecutor
{
public:
    using Predicate = std::function<bool(const ExecutorRow&)>;

    FilterExecutor(
        ExecutorContext* exec_ctx,
        std::unique_ptr<AbstractExecutor> child,
        Predicate predicate);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    std::unique_ptr<AbstractExecutor> child_;
    Predicate predicate_;
};

} // namespace execution
} // namespace HaruhiDB
