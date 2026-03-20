#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"
#include "table/table_iterator.h"

#include <string>

namespace HaruhiDB
{
namespace execution
{

class SeqScanExecutor : public AbstractExecutor
{
public:
    SeqScanExecutor(ExecutorContext* exec_ctx, catalog::TableInfo* table_info);

    void Init() override;
    bool Next(ExecutorRow* row) override;

    bool Failed() const noexcept { return failed_; }
    const std::string& LastError() const noexcept { return last_error_; }

private:
    catalog::TableInfo* table_info_{nullptr};
    table::TableHeap* table_heap_{nullptr};
    const catalog::Schema* schema_{nullptr};

    table::TableIterator iter_;
    table::TableIterator end_;
    bool initialized_{false};
    bool failed_{false};
    std::string last_error_;
};

} // namespace execution
} // namespace HaruhiDB
