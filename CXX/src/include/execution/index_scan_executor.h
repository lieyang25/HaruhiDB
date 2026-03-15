#pragma once

#include "catalog/table_info.h"
#include "execution/executor.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/index_iterator.h"

#include <optional>

namespace HaruhiDB
{
namespace execution
{

class IndexScanExecutor : public AbstractExecutor
{
public:
    IndexScanExecutor(
        ExecutorContext* exec_ctx,
        catalog::TableInfo* table_info,
        storage::BPlusTree* index,
        std::optional<int32_t> start_key = std::nullopt);

    void Init() override;
    bool Next(ExecutorRow* row) override;

private:
    catalog::TableInfo* table_info_{nullptr};
    storage::BPlusTree* index_{nullptr};
    std::optional<int32_t> start_key_;

    storage::IndexIterator iter_;
    storage::IndexIterator end_;
    bool initialized_{false};
};

} // namespace execution
} // namespace HaruhiDB
