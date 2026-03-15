#pragma once

#include "execution/executor.h"

#include <vector>

namespace HaruhiDB
{
namespace execution
{

class ValuesExecutor : public AbstractExecutor
{
public:
    ValuesExecutor(ExecutorContext* exec_ctx, std::vector<std::vector<type::Value>> rows);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    std::vector<std::vector<type::Value>> rows_;
    size_t cursor_{0};
};

} // namespace execution
} // namespace HaruhiDB
