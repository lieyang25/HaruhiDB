#include "execution/insert_executor.h"

#include "storage/record/tuple_codec.h"

#include <cstdint>
#include <utility>

namespace HaruhiDB
{
namespace execution
{

InsertExecutor::InsertExecutor(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::unique_ptr<AbstractExecutor> child)
    : AbstractExecutor(exec_ctx),
      table_info_(table_info),
      child_(std::move(child))
{
}

void InsertExecutor::Init()
{
    initialized_ = true;
    done_ = false;
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool InsertExecutor::Next(ExecutorRow* row)
{
    if (!initialized_) {
        Init();
    }

    if (row == nullptr || done_) {
        return false;
    }

    if (table_info_ == nullptr || table_info_->GetTableHeap() == nullptr || child_ == nullptr) {
        return false;
    }

    const auto& schema = table_info_->GetSchema();
    auto* table_heap = table_info_->GetTableHeap();

    int32_t inserted_count = 0;
    ExecutorRow input;
    while (child_->Next(&input)) {
        auto tuple_exp = record::TupleCodec::Encode(schema, input.values);
        if (!tuple_exp.has_value()) {
            return false;
        }

        record::RID rid;
        if (!table_heap->InsertTuple(tuple_exp.value(), &rid)) {
            return false;
        }

        ++inserted_count;
    }

    row->values = {type::Value::Int32(inserted_count)};
    row->rid = record::RID{};
    row->has_rid = false;
    done_ = true;
    return true;
}

} // namespace execution
} // namespace HaruhiDB
