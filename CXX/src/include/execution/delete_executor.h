#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"

#include <memory>

namespace HaruhiDB
{
namespace execution
{

class DeleteExecutor : public AbstractExecutor
{
public:
    DeleteExecutor(
        ExecutorContext* exec_ctx,
        catalog::TableInfo* table_info,
        std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    catalog::TableInfo* table_info_{nullptr};
    std::unique_ptr<AbstractExecutor> child_;
    bool initialized_{false};
    bool done_{false};
};

} // namespace execution
} // namespace HaruhiDB
