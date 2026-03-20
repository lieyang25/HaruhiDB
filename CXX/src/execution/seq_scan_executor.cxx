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
    failed_ = false;
    last_error_.clear();

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

    failed_ = false;
    last_error_.clear();

    if (row == nullptr) {
        failed_ = true;
        last_error_ = "SeqScanExecutor::Next: row is null";
        return false;
    }
    if (table_heap_ == nullptr || schema_ == nullptr) {
        failed_ = true;
        last_error_ = "SeqScanExecutor::Next: executor is not bound to table heap or schema";
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
        failed_ = true;
        last_error_ = "SeqScanExecutor::Next: decode tuple failed: " + decoded.error().msg;
        return false;
    }

    row->values = std::move(decoded.value());
    row->rid = rid;
    row->has_rid = rid.GetPageId() != INVALID_PAGE_ID;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
