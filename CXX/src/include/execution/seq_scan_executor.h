#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"
#include "table/table_iterator.h"

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

private:
    catalog::TableInfo* table_info_{nullptr};
    table::TableHeap* table_heap_{nullptr};
    const catalog::Schema* schema_{nullptr};

    table::TableIterator iter_;
    table::TableIterator end_;
    bool initialized_{false};
};

} // namespace execution
} // namespace HaruhiDB
