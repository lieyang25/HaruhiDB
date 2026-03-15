#include "execution/index_scan_executor.h"

#include "storage/record/tuple_codec.h"

#include <utility>

namespace HaruhiDB
{
namespace execution
{

IndexScanExecutor::IndexScanExecutor(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    storage::BPlusTree* index,
    std::optional<int32_t> start_key)
    : AbstractExecutor(exec_ctx),
      table_info_(table_info),
      index_(index),
      start_key_(start_key)
{
}

void IndexScanExecutor::Init()
{
    initialized_ = true;
    end_ = storage::IndexIterator();

    if (index_ == nullptr) {
        iter_ = end_;
        return;
    }

    iter_ = start_key_.has_value() ? index_->Begin(start_key_.value()) : index_->Begin();
}

bool IndexScanExecutor::Next(ExecutorRow* row)
{
    if (!initialized_) {
        Init();
    }

    if (row == nullptr || index_ == nullptr) {
        return false;
    }

    while (iter_ != end_) {
        const auto [key, rid] = *iter_;
        ++iter_;

        if (table_info_ == nullptr || table_info_->GetTableHeap() == nullptr) {
            row->values = {type::Value::Int32(key)};
            row->rid = rid;
            row->has_rid = true;
            return true;
        }

        record::Tuple tuple;
        if (!table_info_->GetTableHeap()->GetTuple(rid, &tuple)) {
            continue;
        }

        auto decoded = record::TupleCodec::Decode(table_info_->GetSchema(), tuple);
        if (!decoded.has_value()) {
            continue;
        }

        row->values = std::move(decoded.value());
        row->rid = rid;
        row->has_rid = true;
        return true;
    }

    return false;
}

} // namespace execution
} // namespace HaruhiDB
