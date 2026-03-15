#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"

#include <functional>
#include <memory>
#include <vector>

namespace HaruhiDB
{
namespace execution
{

class UpdateExecutor : public AbstractExecutor
{
public:
    using Updater = std::function<std::vector<type::Value>(const ExecutorRow&)>;

    UpdateExecutor(
        ExecutorContext* exec_ctx,
        catalog::TableInfo* table_info,
        std::unique_ptr<AbstractExecutor> child,
        Updater updater);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    catalog::TableInfo* table_info_{nullptr};
    std::unique_ptr<AbstractExecutor> child_;
    Updater updater_;
    bool initialized_{false};
    bool done_{false};
};

} // namespace execution
} // namespace HaruhiDB
