#include "execution/seq_scan_executor.h"

#include "storage/record/tuple_codec.h"

namespace HaruhiDB
{
namespace execution
{

SeqScanExecutor::SeqScanExecutor(ExecutorContext* exec_ctx, catalog::TableInfo* table_info)
    : AbstractExecutor(exec_ctx), table_info_(table_info)
{
}

void SeqScanExecutor::Init()
{
    initialized_ = true;
    table_heap_ = nullptr;
    schema_ = nullptr;

    if (table_info_ == nullptr) {
        return;
    }

    table_heap_ = table_info_->GetTableHeap();
    schema_ = &table_info_->GetSchema();
    if (table_heap_ == nullptr) {
        return;
    }

    iter_ = table_heap_->Begin();
    end_ = table_heap_->End();
}

bool SeqScanExecutor::Next(ExecutorRow* row)
{
    if (!initialized_) {
        Init();
    }

    if (row == nullptr || table_heap_ == nullptr || schema_ == nullptr) {
        return false;
    }

    if (iter_ == end_) {
        return false;
    }

    const record::RID rid = iter_.GetRID();
    const record::Tuple tuple = *iter_;
    ++iter_;

    auto decoded = record::TupleCodec::Decode(*schema_, tuple);
    if (!decoded.has_value()) {
        return false;
    }

    row->values = std::move(decoded.value());
    row->rid = rid;
    row->has_rid = rid.GetPageId() != INVALID_PAGE_ID;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
