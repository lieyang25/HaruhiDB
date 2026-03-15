#pragma once

#include "execution/executor_context.h"
#include "storage/record/rid.h"
#include "type/value.h"

#include <vector>

namespace HaruhiDB
{
namespace execution
{

struct ExecutorRow
{
    std::vector<type::Value> values;
    record::RID rid{};
    bool has_rid{false};
};

class AbstractExecutor
{
public:
    explicit AbstractExecutor(ExecutorContext* exec_ctx)
        : exec_ctx_(exec_ctx)
    {
    }

    virtual ~AbstractExecutor() = default;

    virtual void Init() = 0;
    virtual bool Next(ExecutorRow* row) = 0;

protected:
    ExecutorContext* exec_ctx_{nullptr};
};

} // namespace execution
} // namespace HaruhiDB
