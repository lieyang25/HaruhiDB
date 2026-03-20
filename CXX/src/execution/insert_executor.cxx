#include "execution/insert_executor.h"

#include "execution/index_maintenance.h"
#include "storage/record/tuple_codec.h"

#include <cstdint>
#include <optional>
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
    failed_ = false;
    last_error_.clear();
    if (child_ != nullptr) {
        child_->Init();
    }
}

bool InsertExecutor::Next(ExecutorRow* row)
{
    if (!initialized_) {
        Init();
    }

    failed_ = false;
    last_error_.clear();

    if (done_) {
        return false;
    }
    if (row == nullptr) {
        failed_ = true;
        last_error_ = "InsertExecutor::Next: row is null";
        return false;
    }

    if (table_info_ == nullptr || table_info_->GetTableHeap() == nullptr || child_ == nullptr) {
        failed_ = true;
        last_error_ = "InsertExecutor::Next: executor is not bound to a valid table heap or child";
        return false;
    }

    const auto& schema = table_info_->GetSchema();
    auto* table_heap = table_info_->GetTableHeap();
    const auto indexes = detail::CollectTableIndexes(table_info_);

    int32_t inserted_count = 0;
    ExecutorRow input;
    while (child_->Next(&input)) {
        std::optional<int32_t> key;
        if (!indexes.empty()) {
            auto key_exp = detail::ExtractPrimaryIndexKey(schema, input.values);
            if (!key_exp.has_value()) {
                failed_ = true;
                last_error_ = "InsertExecutor::Next: extract primary index key failed: " + key_exp.error();
                return false;
            }
            key = key_exp.value();
        }

        auto tuple_exp = record::TupleCodec::Encode(schema, input.values);
        if (!tuple_exp.has_value()) {
            failed_ = true;
            last_error_ = "InsertExecutor::Next: encode tuple failed: " + tuple_exp.error().msg;
            return false;
        }

        record::RID rid;
        if (!table_heap->InsertTuple(tuple_exp.value(), &rid)) {
            failed_ = true;
            last_error_ = "InsertExecutor::Next: insert tuple into table heap failed";
            return false;
        }

        if (key.has_value() && !detail::InsertIntoIndexesByKey(indexes, key.value(), rid)) {
            (void)table_heap->DeleteTuple(rid);
            failed_ = true;
            last_error_ = "InsertExecutor::Next: insert into indexes failed";
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
