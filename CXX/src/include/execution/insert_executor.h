#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"

#include <memory>
#include <string>

namespace HaruhiDB
{
namespace execution
{

class InsertExecutor : public AbstractExecutor
{
public:
    InsertExecutor(
        ExecutorContext* exec_ctx,
        catalog::TableInfo* table_info,
        std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(ExecutorRow* row) override;

    bool Failed() const noexcept { return failed_; }
    const std::string& LastError() const noexcept { return last_error_; }

private:
    catalog::TableInfo* table_info_{nullptr};
    std::unique_ptr<AbstractExecutor> child_;
    bool initialized_{false};
    bool done_{false};
    bool failed_{false};
    std::string last_error_;
};

} // namespace execution
} // namespace HaruhiDB
