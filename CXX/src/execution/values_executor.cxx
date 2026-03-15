#include "execution/values_executor.h"

#include <utility>

namespace HaruhiDB
{
namespace execution
{

ValuesExecutor::ValuesExecutor(ExecutorContext* exec_ctx, std::vector<std::vector<type::Value>> rows)
    : AbstractExecutor(exec_ctx), rows_(std::move(rows))
{
}

void ValuesExecutor::Init()
{
    cursor_ = 0;
}

bool ValuesExecutor::Next(ExecutorRow* row)
{
    if (row == nullptr || cursor_ >= rows_.size()) {
        return false;
    }

    row->values = rows_[cursor_++];
    row->rid = record::RID{};
    row->has_rid = false;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
