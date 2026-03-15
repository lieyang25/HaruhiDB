#pragma once

#include "execution/executor.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace HaruhiDB
{
namespace execution
{

class ProjectionExecutor : public AbstractExecutor
{
public:
    ProjectionExecutor(
        ExecutorContext* exec_ctx,
        std::unique_ptr<AbstractExecutor> child,
        std::vector<uint32_t> column_indices);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<uint32_t> column_indices_;
};

} // namespace execution
} // namespace HaruhiDB
