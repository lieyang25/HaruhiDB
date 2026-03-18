#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "execution/delete_executor.h"
#include "execution/executor_context.h"
#include "execution/filter_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/projection_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/update_executor.h"
#include "execution/values_executor.h"
#include "storage/disk/disk_manager.h"
#include "storage/wal/wal_manager.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace HaruhiDB::execution
{
namespace
{

catalog::Schema MakeStudentSchema()
{
    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 32, false),
    });
    if (!schema_exp.has_value()) {
        throw std::runtime_error(schema_exp.error());
    }
    return std::move(schema_exp.value());
}

catalog::TableInfo* CreateStudentTable(catalog::Catalog* catalog, std::string_view table_name)
{
    if (catalog == nullptr) {
        return nullptr;
    }

    auto table_exp = catalog->CreateTable(std::string(table_name), MakeStudentSchema());
    if (!table_exp.has_value()) {
        return nullptr;
    }
    return table_exp.value();
}

int32_t InsertRows(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::vector<std::vector<type::Value>> rows)
{
    auto values = std::make_unique<ValuesExecutor>(exec_ctx, std::move(rows));
    InsertExecutor insert(exec_ctx, table_info, std::move(values));
    insert.Init();

    ExecutorRow result;
    if (!insert.Next(&result) || result.values.size() != 1) {
        return -1;
    }
    const int32_t* count = result.values[0].TryAs<int32_t>();
    return count == nullptr ? -1 : *count;
}

std::vector<int32_t> CollectIdsBySeqScan(ExecutorContext* exec_ctx, catalog::TableInfo* table_info)
{
    std::vector<int32_t> ids;
    if (exec_ctx == nullptr || table_info == nullptr) {
        return ids;
    }

    SeqScanExecutor scan(exec_ctx, table_info);
    scan.Init();

    ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values.empty() ? nullptr : row.values[0].TryAs<int32_t>();
        if (id != nullptr) {
            ids.push_back(*id);
        }
    }
    return ids;
}

std::vector<int32_t> CollectIdsByIndexScan(
    ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    storage::BPlusTree* index,
    std::optional<int32_t> start_key = std::nullopt)
{
    std::vector<int32_t> ids;
    if (exec_ctx == nullptr || table_info == nullptr || index == nullptr) {
        return ids;
    }

    IndexScanExecutor scan(exec_ctx, table_info, index, start_key);
    scan.Init();

    ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values.empty() ? nullptr : row.values[0].TryAs<int32_t>();
        if (id != nullptr) {
            ids.push_back(*id);
        }
    }
    return ids;
}

std::vector<int32_t> CollectKeysByIndexScan(
    ExecutorContext* exec_ctx,
    storage::BPlusTree* index,
    std::optional<int32_t> start_key = std::nullopt)
{
    std::vector<int32_t> keys;
    if (exec_ctx == nullptr || index == nullptr) {
        return keys;
    }

    IndexScanExecutor scan(exec_ctx, nullptr, index, start_key);
    scan.Init();

    ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* key = row.values.empty() ? nullptr : row.values[0].TryAs<int32_t>();
        if (key != nullptr) {
            keys.push_back(*key);
        }
    }
    return keys;
}

class VectorRowExecutor : public AbstractExecutor
{
public:
    VectorRowExecutor(ExecutorContext* exec_ctx, std::vector<ExecutorRow> rows)
        : AbstractExecutor(exec_ctx), rows_(std::move(rows))
    {
    }

    void Init() override
    {
        cursor_ = 0;
    }

    bool Next(ExecutorRow* row) override
    {
        if (row == nullptr || cursor_ >= rows_.size()) {
            return false;
        }
        *row = rows_[cursor_++];
        return true;
    }

private:
    std::vector<ExecutorRow> rows_;
    size_t cursor_{0};
};

} // namespace

class ExecutorFunctionalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto stem = std::string("execution_functional_") + info->test_suite_name() + "_" + info->name();
        db_path_ = std::filesystem::temp_directory_path() / (stem + ".db");
        wal_path_ = std::filesystem::temp_directory_path() / (stem + ".wal");
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(wal_path_, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(wal_path_, ec);
    }

    std::filesystem::path db_path_;
    std::filesystem::path wal_path_;
};

TEST_F(ExecutorFunctionalTest, ValuesExecutorOrderAndReinit)
{
    ExecutorContext exec_ctx(nullptr);
    ValuesExecutor values(
        &exec_ctx,
        {
            {type::Value::Int32(1), type::Value::VarChar("a")},
            {type::Value::Int32(2), type::Value::VarChar("b")},
        });

    values.Init();

    ExecutorRow row;
    ASSERT_TRUE(values.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(1));
    EXPECT_EQ(row.values[1], type::Value::VarChar("a"));

    ASSERT_TRUE(values.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(2));
    EXPECT_EQ(row.values[1], type::Value::VarChar("b"));

    EXPECT_FALSE(values.Next(&row));

    values.Init();
    ASSERT_TRUE(values.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(1));
}

TEST_F(ExecutorFunctionalTest, ValuesExecutorHandlesEmptyAndNullOutput)
{
    ExecutorContext exec_ctx(nullptr);
    ValuesExecutor values(&exec_ctx, {});
    values.Init();

    ExecutorRow row;
    EXPECT_FALSE(values.Next(&row));
    EXPECT_FALSE(values.Next(&row));
    EXPECT_FALSE(values.Next(nullptr));
}

TEST_F(ExecutorFunctionalTest, SeqScanReturnsNoRowsOnEmptyTable)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();

    ExecutorRow row;
    EXPECT_FALSE(scan.Next(&row));
}

TEST_F(ExecutorFunctionalTest, SeqScanReturnsInsertionOrderAndSupportsReinit)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(3), type::Value::VarChar("c")},
                {type::Value::Int32(1), type::Value::VarChar("a")},
                {type::Value::Int32(2), type::Value::VarChar("b")},
            }),
        3);

    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();

    std::vector<int32_t> ids;
    ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        ASSERT_NE(id, nullptr);
        ids.push_back(*id);
    }
    EXPECT_EQ(ids, (std::vector<int32_t>{3, 1, 2}));
    EXPECT_FALSE(scan.Next(&row));

    scan.Init();
    ids.clear();
    while (scan.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        ASSERT_NE(id, nullptr);
        ids.push_back(*id);
    }
    EXPECT_EQ(ids, (std::vector<int32_t>{3, 1, 2}));
}

TEST_F(ExecutorFunctionalTest, SeqScanSkipsDeletedTombstones)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
                {type::Value::Int32(2), type::Value::VarChar("b")},
                {type::Value::Int32(3), type::Value::VarChar("c")},
            }),
        3);

    record::RID rid_to_delete;
    {
        SeqScanExecutor scan(&exec_ctx, table_info);
        scan.Init();
        ExecutorRow row;
        while (scan.Next(&row)) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            ASSERT_NE(id, nullptr);
            if (*id == 2) {
                rid_to_delete = row.rid;
                break;
            }
        }
    }
    ASSERT_NE(rid_to_delete.GetPageId(), INVALID_PAGE_ID);
    ASSERT_TRUE(table_info->GetTableHeap()->DeleteTuple(rid_to_delete));

    const auto ids = CollectIdsBySeqScan(&exec_ctx, table_info);
    EXPECT_EQ(ids, (std::vector<int32_t>{1, 3}));
}

TEST_F(ExecutorFunctionalTest, InsertExecutorInsertsAndReportsCount)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    const int32_t inserted = InsertRows(
        &exec_ctx,
        table_info,
        {
            {type::Value::Int32(1), type::Value::VarChar("a")},
            {type::Value::Int32(2), type::Value::VarChar("b")},
            {type::Value::Int32(3), type::Value::VarChar("c")},
        });
    ASSERT_EQ(inserted, 3);

    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();
    int count = 0;
    ExecutorRow row;
    while (scan.Next(&row)) {
        ++count;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(ExecutorFunctionalTest, InsertExecutorRejectsSchemaMismatch)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(1)},
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();

    ExecutorRow result;
    EXPECT_FALSE(insert.Next(&result));

    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();
    EXPECT_FALSE(scan.Next(&result));
}

TEST_F(ExecutorFunctionalTest, InsertExecutorHandlesEmptyInputAndStateMachine)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto empty_values = std::make_unique<ValuesExecutor>(&exec_ctx, std::vector<std::vector<type::Value>>{});
    InsertExecutor insert(&exec_ctx, table_info, std::move(empty_values));
    insert.Init();

    ExecutorRow result;
    ASSERT_TRUE(insert.Next(&result));
    ASSERT_EQ(result.values.size(), 1u);
    EXPECT_EQ(result.values[0], type::Value::Int32(0));
    EXPECT_FALSE(insert.Next(&result));

    insert.Init();
    ASSERT_TRUE(insert.Next(&result));
    EXPECT_EQ(result.values[0], type::Value::Int32(0));
}

TEST_F(ExecutorFunctionalTest, InsertExecutorMixedValidInvalidInputLeavesPartialWrites)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(1), type::Value::VarChar("ok")},
            {type::Value::Int32(2)}, // schema mismatch
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();
    ExecutorRow result;
    EXPECT_FALSE(insert.Next(&result));

    const auto ids = CollectIdsBySeqScan(&exec_ctx, table_info);
    // 当前实现不包含事务回滚，前面成功写入会保留。
    EXPECT_EQ(ids, (std::vector<int32_t>{1}));
}

TEST_F(ExecutorFunctionalTest, InsertExecutorRejectsTypeMismatch)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::VarChar("not_int"), type::Value::VarChar("bad")},
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();
    ExecutorRow result;
    EXPECT_FALSE(insert.Next(&result));
    EXPECT_TRUE(CollectIdsBySeqScan(&exec_ctx, table_info).empty());
}

TEST_F(ExecutorFunctionalTest, FilterExecutorPredicateAndPassThrough)
{
    ExecutorContext exec_ctx(nullptr);

    auto child = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(1)},
            {type::Value::Int32(2)},
            {type::Value::Int32(3)},
        });

    FilterExecutor filter(
        &exec_ctx,
        std::move(child),
        [](const ExecutorRow& row) {
            const int32_t* v = row.values[0].TryAs<int32_t>();
            return v != nullptr && (*v % 2 == 1);
        });

    filter.Init();

    ExecutorRow row;
    ASSERT_TRUE(filter.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(1));
    ASSERT_TRUE(filter.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(3));
    EXPECT_FALSE(filter.Next(&row));

    auto passthrough_child = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(7)},
            {type::Value::Int32(8)},
        });
    FilterExecutor passthrough(&exec_ctx, std::move(passthrough_child), {});
    passthrough.Init();

    ASSERT_TRUE(passthrough.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(7));
    ASSERT_TRUE(passthrough.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(8));
    EXPECT_FALSE(passthrough.Next(&row));
}

TEST_F(ExecutorFunctionalTest, FilterExecutorAllFilteredAndNullChild)
{
    ExecutorContext exec_ctx(nullptr);

    auto child = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(1)},
            {type::Value::Int32(2)},
        });

    FilterExecutor all_filtered(
        &exec_ctx,
        std::move(child),
        [](const ExecutorRow&) {
            return false;
        });
    all_filtered.Init();
    ExecutorRow row;
    EXPECT_FALSE(all_filtered.Next(&row));

    FilterExecutor null_child(&exec_ctx, nullptr, [](const ExecutorRow&) {
        return true;
    });
    null_child.Init();
    EXPECT_FALSE(null_child.Next(&row));
}

TEST_F(ExecutorFunctionalTest, FilterExecutorPreservesRidFromChild)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(11), type::Value::VarChar("x")}}), 1);

    auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    FilterExecutor filter(
        &exec_ctx,
        std::move(scan),
        [](const ExecutorRow&) {
            return true;
        });
    filter.Init();

    ExecutorRow row;
    ASSERT_TRUE(filter.Next(&row));
    EXPECT_TRUE(row.has_rid);
    EXPECT_NE(row.rid.GetPageId(), INVALID_PAGE_ID);
}

TEST_F(ExecutorFunctionalTest, ProjectionExecutorProjectsAndPreservesRid)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(5), type::Value::VarChar("mugi")}}), 1);

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    ProjectionExecutor projection(&exec_ctx, std::move(scan_child), {1});
    projection.Init();

    ExecutorRow row;
    ASSERT_TRUE(projection.Next(&row));
    ASSERT_EQ(row.values.size(), 1u);
    EXPECT_EQ(row.values[0], type::Value::VarChar("mugi"));
    EXPECT_TRUE(row.has_rid);
    EXPECT_NE(row.rid.GetPageId(), INVALID_PAGE_ID);
    EXPECT_FALSE(projection.Next(&row));
}

TEST_F(ExecutorFunctionalTest, ProjectionExecutorOutOfRangeFails)
{
    ExecutorContext exec_ctx(nullptr);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{{type::Value::Int32(1)}});

    ProjectionExecutor projection(&exec_ctx, std::move(values), {1});
    projection.Init();

    ExecutorRow row;
    EXPECT_FALSE(projection.Next(&row));
}

TEST_F(ExecutorFunctionalTest, ProjectionExecutorSupportsOrderDuplicateAndEmpty)
{
    ExecutorContext exec_ctx(nullptr);
    ExecutorRow row;

    {
        auto values = std::make_unique<ValuesExecutor>(
            &exec_ctx,
            std::vector<std::vector<type::Value>>{{type::Value::Int32(1), type::Value::VarChar("a")}});
        ProjectionExecutor projection(&exec_ctx, std::move(values), {1, 0});
        projection.Init();
        ASSERT_TRUE(projection.Next(&row));
        ASSERT_EQ(row.values.size(), 2u);
        EXPECT_EQ(row.values[0], type::Value::VarChar("a"));
        EXPECT_EQ(row.values[1], type::Value::Int32(1));
    }

    {
        auto values = std::make_unique<ValuesExecutor>(
            &exec_ctx,
            std::vector<std::vector<type::Value>>{{type::Value::Int32(7), type::Value::VarChar("x")}});
        ProjectionExecutor projection(&exec_ctx, std::move(values), {0, 0});
        projection.Init();
        ASSERT_TRUE(projection.Next(&row));
        ASSERT_EQ(row.values.size(), 2u);
        EXPECT_EQ(row.values[0], type::Value::Int32(7));
        EXPECT_EQ(row.values[1], type::Value::Int32(7));
    }

    {
        auto values = std::make_unique<ValuesExecutor>(
            &exec_ctx,
            std::vector<std::vector<type::Value>>{{type::Value::Int32(9), type::Value::VarChar("z")}});
        ProjectionExecutor projection(&exec_ctx, std::move(values), {});
        projection.Init();
        ASSERT_TRUE(projection.Next(&row));
        EXPECT_TRUE(row.values.empty());
    }
}

TEST_F(ExecutorFunctionalTest, ProjectionExecutorFailsWhenAnyIndexIsOutOfRange)
{
    ExecutorContext exec_ctx(nullptr);
    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{{type::Value::Int32(1), type::Value::VarChar("a")}});
    ProjectionExecutor projection(&exec_ctx, std::move(values), {0, 2});
    projection.Init();

    ExecutorRow row;
    EXPECT_FALSE(projection.Next(&row));
}

TEST_F(ExecutorFunctionalTest, DeleteExecutorDeletesFilteredRows)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
                {type::Value::Int32(2), type::Value::VarChar("b")},
                {type::Value::Int32(3), type::Value::VarChar("c")},
            }),
        3);

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id >= 2;
        });

    DeleteExecutor deleter(&exec_ctx, table_info, std::move(filter));
    deleter.Init();

    ExecutorRow result;
    ASSERT_TRUE(deleter.Next(&result));
    ASSERT_EQ(result.values.size(), 1u);
    EXPECT_EQ(result.values[0], type::Value::Int32(2));
    EXPECT_FALSE(deleter.Next(&result));

    SeqScanExecutor verify_scan(&exec_ctx, table_info);
    verify_scan.Init();
    int32_t remained_id = -1;
    int remained = 0;
    ExecutorRow row;
    while (verify_scan.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        ASSERT_NE(id, nullptr);
        remained_id = *id;
        ++remained;
    }

    EXPECT_EQ(remained, 1);
    EXPECT_EQ(remained_id, 1);
}

TEST_F(ExecutorFunctionalTest, DeleteExecutorFailsWhenChildHasNoRid)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{{type::Value::Int32(1), type::Value::VarChar("x")}});

    DeleteExecutor deleter(&exec_ctx, table_info, std::move(values));
    deleter.Init();

    ExecutorRow row;
    EXPECT_FALSE(deleter.Next(&row));
}

TEST_F(ExecutorFunctionalTest, DeleteExecutorHandlesZeroRowsStateAndReinit)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(1), type::Value::VarChar("a")}}), 1);

    auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan),
        [](const ExecutorRow&) {
            return false;
        });
    DeleteExecutor deleter(&exec_ctx, table_info, std::move(filter));
    deleter.Init();

    ExecutorRow row;
    ASSERT_TRUE(deleter.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(0));
    EXPECT_FALSE(deleter.Next(&row));

    deleter.Init();
    ASSERT_TRUE(deleter.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(0));
}

TEST_F(ExecutorFunctionalTest, DeleteExecutorFailsOnStaleRid)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    std::vector<ExecutorRow> rows{
        ExecutorRow{
            .values = {type::Value::Int32(1), type::Value::VarChar("x")},
            .rid = record::RID(9999, 1),
            .has_rid = true,
        },
    };
    auto stale_child = std::make_unique<VectorRowExecutor>(&exec_ctx, std::move(rows));
    DeleteExecutor deleter(&exec_ctx, table_info, std::move(stale_child));
    deleter.Init();

    ExecutorRow row;
    EXPECT_FALSE(deleter.Next(&row));
}

TEST_F(ExecutorFunctionalTest, UpdateExecutorUpdatesFilteredRows)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(10), type::Value::VarChar("ui")},
                {type::Value::Int32(20), type::Value::VarChar("sawa")},
            }),
        2);

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id == 20;
        });

    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(filter),
        [](const ExecutorRow& row) {
            auto out = row.values;
            out[1] = type::Value::VarChar("azusa");
            return out;
        });

    updater.Init();

    ExecutorRow result;
    ASSERT_TRUE(updater.Next(&result));
    ASSERT_EQ(result.values.size(), 1u);
    EXPECT_EQ(result.values[0], type::Value::Int32(1));
    EXPECT_FALSE(updater.Next(&result));

    SeqScanExecutor verify_scan(&exec_ctx, table_info);
    verify_scan.Init();

    int updated = 0;
    ExecutorRow row;
    while (verify_scan.Next(&row)) {
        if (row.values[0] == type::Value::Int32(20)) {
            EXPECT_EQ(row.values[1], type::Value::VarChar("azusa"));
            ++updated;
        }
    }
    EXPECT_EQ(updated, 1);
}

TEST_F(ExecutorFunctionalTest, UpdateExecutorFailsWhenChildHasNoRid)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{{type::Value::Int32(1), type::Value::VarChar("x")}});

    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(values),
        [](const ExecutorRow& row) {
            return row.values;
        });

    updater.Init();

    ExecutorRow row;
    EXPECT_FALSE(updater.Next(&row));
}

TEST_F(ExecutorFunctionalTest, UpdateExecutorHandlesZeroRowsAndReinit)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(1), type::Value::VarChar("a")}}), 1);

    auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan),
        [](const ExecutorRow&) {
            return false;
        });
    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(filter),
        [](const ExecutorRow& row) {
            return row.values;
        });
    updater.Init();

    ExecutorRow row;
    ASSERT_TRUE(updater.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(0));
    EXPECT_FALSE(updater.Next(&row));

    updater.Init();
    ASSERT_TRUE(updater.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(0));
}

TEST_F(ExecutorFunctionalTest, UpdateExecutorRejectsInvalidUpdaterOutput)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(16, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(1), type::Value::VarChar("a")}}), 1);

    {
        auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
        UpdateExecutor updater(
            &exec_ctx,
            table_info,
            std::move(scan),
            [](const ExecutorRow&) {
                return std::vector<type::Value>{type::Value::Int32(1)}; // 列数错误
            });
        updater.Init();
        ExecutorRow row;
        EXPECT_FALSE(updater.Next(&row));
    }

    {
        auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
        UpdateExecutor updater(
            &exec_ctx,
            table_info,
            std::move(scan),
            [](const ExecutorRow&) {
                return std::vector<type::Value>{
                    type::Value::VarChar("bad"),
                    type::Value::VarChar("a"),
                }; // id 类型错误
            });
        updater.Init();
        ExecutorRow row;
        EXPECT_FALSE(updater.Next(&row));
    }

    {
        auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
        UpdateExecutor updater(
            &exec_ctx,
            table_info,
            std::move(scan),
            [](const ExecutorRow&) {
                return std::vector<type::Value>{
                    type::Value::Int32(1),
                    type::Value::Null(),
                }; // NULL 目前不支持
            });
        updater.Init();
        ExecutorRow row;
        EXPECT_FALSE(updater.Next(&row));
    }
}

TEST_F(ExecutorFunctionalTest, UpdateExecutorCanHandleLargerPayloadAcrossPages)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 512, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();

    auto table_exp = catalog.CreateTable("big_student", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    std::vector<std::vector<type::Value>> rows;
    rows.reserve(200);
    for (int i = 0; i < 200; ++i) {
        rows.push_back({type::Value::Int32(i), type::Value::VarChar("s")});
    }
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, std::move(rows)), 200);

    auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id == 7;
        });
    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(filter),
        [](const ExecutorRow& row) {
            auto out = row.values;
            out[1] = type::Value::VarChar(std::string(500, 'x'));
            return out;
        });
    updater.Init();

    ExecutorRow result;
    ASSERT_TRUE(updater.Next(&result));
    EXPECT_EQ(result.values[0], type::Value::Int32(1));
    EXPECT_FALSE(updater.Next(&result));

    SeqScanExecutor verify(&exec_ctx, table_info);
    verify.Init();
    int updated = 0;
    ExecutorRow row;
    while (verify.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        const std::string* name = row.values[1].TryAs<std::string>();
        ASSERT_NE(id, nullptr);
        ASSERT_NE(name, nullptr);
        if (*id == 7) {
            EXPECT_EQ(name->size(), 500u);
            ++updated;
        }
    }
    EXPECT_EQ(updated, 1);
}

TEST_F(ExecutorFunctionalTest, IndexScanRespectsStartKeyAndOrder)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(10), type::Value::VarChar("a")},
                {type::Value::Int32(20), type::Value::VarChar("b")},
                {type::Value::Int32(30), type::Value::VarChar("c")},
            }),
        3);

    IndexScanExecutor scan(&exec_ctx, table_info, index, 20);
    scan.Init();

    ExecutorRow row;
    ASSERT_TRUE(scan.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(20));

    ASSERT_TRUE(scan.Next(&row));
    EXPECT_EQ(row.values[0], type::Value::Int32(30));

    EXPECT_FALSE(scan.Next(&row));
}

TEST_F(ExecutorFunctionalTest, IndexScanSkipsStaleEntries)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(10), type::Value::VarChar("a")},
                {type::Value::Int32(20), type::Value::VarChar("b")},
            }),
        2);
    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id == 10;
        });
    DeleteExecutor deleter(&exec_ctx, table_info, std::move(filter));
    deleter.Init();

    ExecutorRow deleted;
    ASSERT_TRUE(deleter.Next(&deleted));
    EXPECT_EQ(deleted.values[0], type::Value::Int32(1));

    IndexScanExecutor scan(&exec_ctx, table_info, index, 0);
    scan.Init();

    std::vector<int32_t> ids;
    ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        ASSERT_NE(id, nullptr);
        ids.push_back(*id);
    }

    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 20);
}

TEST_F(ExecutorFunctionalTest, IndexScanWithoutTableInfoReturnsKeyOnly)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    const record::RID rid_a(101, 1);
    const record::RID rid_b(102, 2);
    ASSERT_TRUE(index->Insert(5, rid_a));
    ASSERT_TRUE(index->Insert(8, rid_b));

    ExecutorContext exec_ctx(&catalog);
    IndexScanExecutor scan(&exec_ctx, nullptr, index, 6);
    scan.Init();

    ExecutorRow row;
    ASSERT_TRUE(scan.Next(&row));
    ASSERT_EQ(row.values.size(), 1u);
    EXPECT_EQ(row.values[0], type::Value::Int32(8));
    EXPECT_TRUE(row.has_rid);
    EXPECT_EQ(row.rid, rid_b);

    EXPECT_FALSE(scan.Next(&row));
}

TEST_F(ExecutorFunctionalTest, IndexScanHandlesEmptyIndexAndBoundaryStartKey)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    {
        IndexScanExecutor empty_scan(&exec_ctx, table_info, index, 0);
        empty_scan.Init();
        ExecutorRow row;
        EXPECT_FALSE(empty_scan.Next(&row));
    }

    const record::RID rid_a(10, 1);
    const record::RID rid_b(11, 1);
    ASSERT_TRUE(index->Insert(5, rid_a));
    ASSERT_TRUE(index->Insert(8, rid_b));
    EXPECT_FALSE(index->Insert(8, rid_b)); // 重复键应失败

    {
        IndexScanExecutor beyond_max(&exec_ctx, nullptr, index, 99);
        beyond_max.Init();
        ExecutorRow row;
        EXPECT_FALSE(beyond_max.Next(&row));
    }
}

TEST_F(ExecutorFunctionalTest, IndexScanSortsUnorderedInsertsAndSkipsInvalidRid)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_TRUE(index->Insert(30, record::RID(30, 1)));
    ASSERT_TRUE(index->Insert(10, record::RID(10, 1)));
    ASSERT_TRUE(index->Insert(20, record::RID(20, 1)));

    IndexScanExecutor key_only_scan(&exec_ctx, nullptr, index, 0);
    key_only_scan.Init();

    std::vector<int32_t> keys;
    ExecutorRow row;
    while (key_only_scan.Next(&row)) {
        const int32_t* key = row.values[0].TryAs<int32_t>();
        ASSERT_NE(key, nullptr);
        keys.push_back(*key);
    }
    EXPECT_EQ(keys, (std::vector<int32_t>{10, 20, 30}));

    auto live_index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_live");
    ASSERT_TRUE(live_index_exp.has_value()) << live_index_exp.error();
    storage::BPlusTree* live_index = live_index_exp.value();
    ASSERT_NE(live_index, nullptr);

    ASSERT_EQ(InsertRows(&exec_ctx, table_info, {{type::Value::Int32(42), type::Value::VarChar("alive")}}), 1);
    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();
    ExecutorRow live_row;
    ASSERT_TRUE(scan.Next(&live_row));
    ASSERT_TRUE(live_row.has_rid);

    ASSERT_TRUE(live_index->Insert(1, record::RID{})); // 非法 RID，IndexScan(table_info 模式) 应跳过

    IndexScanExecutor with_table_scan(&exec_ctx, table_info, live_index, 0);
    with_table_scan.Init();
    std::vector<int32_t> ids;
    while (with_table_scan.Next(&row)) {
        const int32_t* id = row.values[0].TryAs<int32_t>();
        ASSERT_NE(id, nullptr);
        ids.push_back(*id);
    }
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], 42);
}

TEST_F(ExecutorFunctionalTest, PipelineSeqScanFilterProjectionAndValuesInsertSeqScan)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
                {type::Value::Int32(2), type::Value::VarChar("b")},
                {type::Value::Int32(3), type::Value::VarChar("c")},
            }),
        3);

    auto scan = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id >= 2;
        });
    ProjectionExecutor projection(&exec_ctx, std::move(filter), {1, 0});
    projection.Init();

    std::vector<std::string> names;
    std::vector<int32_t> ids;
    ExecutorRow row;
    while (projection.Next(&row)) {
        const std::string* name = row.values[0].TryAs<std::string>();
        const int32_t* id = row.values[1].TryAs<int32_t>();
        ASSERT_NE(name, nullptr);
        ASSERT_NE(id, nullptr);
        names.push_back(*name);
        ids.push_back(*id);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"b", "c"}));
    EXPECT_EQ(ids, (std::vector<int32_t>{2, 3}));
}

TEST_F(ExecutorFunctionalTest, ExecutorStateMachineInitNextEofReinitNext)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);
    ExecutorContext exec_ctx(&catalog);

    {
        auto values = std::make_unique<ValuesExecutor>(
            &exec_ctx,
            std::vector<std::vector<type::Value>>{{type::Value::Int32(1), type::Value::VarChar("a")}});
        InsertExecutor insert(&exec_ctx, table_info, std::move(values));
        insert.Init();
        ExecutorRow row;
        ASSERT_TRUE(insert.Next(&row));
        EXPECT_FALSE(insert.Next(&row)); // EOF 后继续 Next 仍 false
        insert.Init();
        ASSERT_TRUE(insert.Next(&row)); // 重新 Init 后可再次执行
    }

    {
        SeqScanExecutor scan(&exec_ctx, table_info);
        scan.Init();
        ExecutorRow row;
        int count_first = 0;
        while (scan.Next(&row)) {
            ++count_first;
        }
        EXPECT_GT(count_first, 0);
        EXPECT_FALSE(scan.Next(&row));
        scan.Init();
        int count_second = 0;
        while (scan.Next(&row)) {
            ++count_second;
        }
        EXPECT_EQ(count_second, count_first);
    }
}

TEST_F(ExecutorFunctionalTest, BoundaryLargeDataAcrossPages)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(128, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 256, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();
    auto table_exp = catalog.CreateTable("student_big", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    std::vector<std::vector<type::Value>> rows;
    rows.reserve(1200);
    for (int i = 0; i < 1200; ++i) {
        rows.push_back({
            type::Value::Int32(i),
            type::Value::VarChar(std::string(200, static_cast<char>('a' + (i % 26)))),
        });
    }
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, std::move(rows)), 1200);

    const auto ids = CollectIdsBySeqScan(&exec_ctx, table_info);
    ASSERT_EQ(ids.size(), 1200u);
    EXPECT_EQ(ids.front(), 0);
    EXPECT_EQ(ids.back(), 1199);
}

TEST_F(ExecutorFunctionalTest, RestartRecoversCatalogAndExecutorsRemainUsable)
{
    table_oid_t table_oid = 0;
    index_oid_t index_oid = 0;

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(64, &dm);
        catalog::Catalog catalog(&bpm);

        catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
        ASSERT_NE(table_info, nullptr);

        ExecutorContext exec_ctx(&catalog);
        ASSERT_EQ(
            InsertRows(
                &exec_ctx,
                table_info,
                {
                    {type::Value::Int32(1), type::Value::VarChar("a")},
                    {type::Value::Int32(2), type::Value::VarChar("b")},
                    {type::Value::Int32(3), type::Value::VarChar("c")},
                    {type::Value::Int32(4), type::Value::VarChar("d")},
                }),
            4);

        auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
        ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
        ASSERT_NE(index_exp.value(), nullptr);
        table_oid = table_info->Oid();
        ASSERT_FALSE(table_info->IndexOids().empty());
        index_oid = table_info->IndexOids().front();
        ASSERT_TRUE(bpm.FlushAllPages().has_value());
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(64, &dm);
        catalog::Catalog recovered_catalog(&bpm);

        catalog::TableInfo* recovered_table = recovered_catalog.GetTable(table_oid);
        ASSERT_NE(recovered_table, nullptr);
        storage::BPlusTree* recovered_index = recovered_catalog.GetIndex(table_oid, index_oid);
        ASSERT_NE(recovered_index, nullptr);

        ExecutorContext exec_ctx(&recovered_catalog);
        EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, recovered_table), (std::vector<int32_t>{1, 2, 3, 4}));
        EXPECT_EQ(
            CollectIdsByIndexScan(&exec_ctx, recovered_table, recovered_index, 2),
            (std::vector<int32_t>{2, 3, 4}));
    }
}

TEST_F(ExecutorFunctionalTest, WalRecoverThenCatalogLoadSupportsExecutorScan)
{
    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        catalog::Catalog catalog(&bpm);

        catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
        ASSERT_NE(table_info, nullptr);

        storage::wal::WalManager wal(wal_path_);
        table_info->GetTableHeap()->SetWalManager(&wal);

        ExecutorContext exec_ctx(&catalog);
        ASSERT_EQ(
            InsertRows(
                &exec_ctx,
                table_info,
                {
                    {type::Value::Int32(10), type::Value::VarChar("x")},
                    {type::Value::Int32(20), type::Value::VarChar("y")},
                    {type::Value::Int32(30), type::Value::VarChar("z")},
                }),
            3);
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        catalog::Catalog recovered_catalog(&bpm);
        catalog::TableInfo* recovered_table = recovered_catalog.GetTable("student");
        ASSERT_NE(recovered_table, nullptr);

        ExecutorContext exec_ctx(&recovered_catalog);
        EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, recovered_table), (std::vector<int32_t>{10, 20, 30}));
    }
}

TEST_F(ExecutorFunctionalTest, IndexAutoBackfillAndDmlAutoMaintenance)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
                {type::Value::Int32(2), type::Value::VarChar("b")},
                {type::Value::Int32(3), type::Value::VarChar("c")},
            }),
        3);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    EXPECT_EQ(CollectIdsByIndexScan(&exec_ctx, table_info, index), (std::vector<int32_t>{1, 2, 3}));
    EXPECT_EQ(CollectKeysByIndexScan(&exec_ctx, index), (std::vector<int32_t>{1, 2, 3}));

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id == 2;
        });
    DeleteExecutor deleter(&exec_ctx, table_info, std::move(filter));
    deleter.Init();
    ExecutorRow delete_result;
    ASSERT_TRUE(deleter.Next(&delete_result));
    EXPECT_EQ(delete_result.values[0], type::Value::Int32(1));

    EXPECT_EQ(CollectIdsByIndexScan(&exec_ctx, table_info, index), (std::vector<int32_t>{1, 3}));
    EXPECT_EQ(CollectKeysByIndexScan(&exec_ctx, index), (std::vector<int32_t>{1, 3}));

    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(4), type::Value::VarChar("d")},
            }),
        1);

    auto seq_ids_after_insert = CollectIdsBySeqScan(&exec_ctx, table_info);
    std::ranges::sort(seq_ids_after_insert);
    EXPECT_EQ(seq_ids_after_insert, (std::vector<int32_t>{1, 3, 4}));
    EXPECT_EQ(CollectKeysByIndexScan(&exec_ctx, index), (std::vector<int32_t>{1, 3, 4}));
    EXPECT_EQ(CollectIdsByIndexScan(&exec_ctx, table_info, index), (std::vector<int32_t>{1, 3, 4}));
}

TEST_F(ExecutorFunctionalTest, InsertWithIndexDuplicateKeyRollsBackTupleWrite)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
            }),
        1);

    EXPECT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("dup")},
            }),
        -1);

    EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, table_info), (std::vector<int32_t>{1}));
    EXPECT_EQ(CollectKeysByIndexScan(&exec_ctx, index), (std::vector<int32_t>{1}));
}

TEST_F(ExecutorFunctionalTest, UpdateRejectsIndexedKeyChangeAndKeepsDataUnchanged)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    catalog::TableInfo* table_info = CreateStudentTable(&catalog, "student");
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    ASSERT_EQ(
        InsertRows(
            &exec_ctx,
            table_info,
            {
                {type::Value::Int32(1), type::Value::VarChar("a")},
            }),
        1);

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            auto out = row.values;
            out[0] = type::Value::Int32(100);
            return out;
        });
    updater.Init();

    ExecutorRow update_result;
    EXPECT_FALSE(updater.Next(&update_result));
    EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, table_info), (std::vector<int32_t>{1}));
    EXPECT_EQ(CollectKeysByIndexScan(&exec_ctx, index), (std::vector<int32_t>{1}));
}

TEST_F(ExecutorFunctionalTest, UpdateOnIndexedTableKeepsIndexLookupReadableAfterLargePayloadUpdate)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 512, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();
    auto table_exp = catalog.CreateTable("student_idx_big", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_big_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);
    std::vector<std::vector<type::Value>> rows;
    rows.reserve(200);
    for (int i = 0; i < 200; ++i) {
        rows.push_back({type::Value::Int32(i), type::Value::VarChar("s")});
    }
    ASSERT_EQ(InsertRows(&exec_ctx, table_info, std::move(rows)), 200);

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            const int32_t* id = row.values[0].TryAs<int32_t>();
            return id != nullptr && *id == 7;
        });
    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(filter),
        [](const ExecutorRow& row) {
            auto out = row.values;
            out[1] = type::Value::VarChar(std::string(500, 'x'));
            return out;
        });
    updater.Init();

    ExecutorRow update_result;
    ASSERT_TRUE(updater.Next(&update_result));
    EXPECT_EQ(update_result.values[0], type::Value::Int32(1));
    EXPECT_FALSE(updater.Next(&update_result));

    IndexScanExecutor index_scan(&exec_ctx, table_info, index, 7);
    index_scan.Init();
    ExecutorRow row;
    ASSERT_TRUE(index_scan.Next(&row));
    ASSERT_EQ(row.values.size(), 2u);
    EXPECT_EQ(row.values[0], type::Value::Int32(7));
    const std::string* name = row.values[1].TryAs<std::string>();
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->size(), 500u);
}

TEST_F(ExecutorFunctionalTest, CatalogGetAllTablesCanDriveTopDownExecutorChecksAfterRestart)
{
    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(64, &dm);
        catalog::Catalog catalog(&bpm);
        ExecutorContext exec_ctx(&catalog);

        auto* t0 = CreateStudentTable(&catalog, "t0");
        auto* t1 = CreateStudentTable(&catalog, "t1");
        auto* t2 = CreateStudentTable(&catalog, "t2");
        ASSERT_NE(t0, nullptr);
        ASSERT_NE(t1, nullptr);
        ASSERT_NE(t2, nullptr);

        ASSERT_EQ(
            InsertRows(
                &exec_ctx,
                t0,
                {
                    {type::Value::Int32(1), type::Value::VarChar("a")},
                    {type::Value::Int32(2), type::Value::VarChar("b")},
                }),
            2);
        ASSERT_EQ(
            InsertRows(
                &exec_ctx,
                t1,
                {
                    {type::Value::Int32(10), type::Value::VarChar("x")},
                }),
            1);
        ASSERT_EQ(
            InsertRows(
                &exec_ctx,
                t2,
                {
                    {type::Value::Int32(20), type::Value::VarChar("y")},
                    {type::Value::Int32(21), type::Value::VarChar("z")},
                    {type::Value::Int32(22), type::Value::VarChar("w")},
                }),
            3);

        ASSERT_TRUE(bpm.FlushAllPages().has_value());
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(64, &dm);
        catalog::Catalog recovered_catalog(&bpm);
        ExecutorContext exec_ctx(&recovered_catalog);

        auto tables = recovered_catalog.GetAllTables();
        ASSERT_EQ(tables.size(), 3u);
        ASSERT_NE(tables[0], nullptr);
        ASSERT_NE(tables[1], nullptr);
        ASSERT_NE(tables[2], nullptr);
        EXPECT_EQ(tables[0]->Name(), "t0");
        EXPECT_EQ(tables[1]->Name(), "t1");
        EXPECT_EQ(tables[2]->Name(), "t2");

        EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, tables[0]), (std::vector<int32_t>{1, 2}));
        EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, tables[1]), (std::vector<int32_t>{10}));
        EXPECT_EQ(CollectIdsBySeqScan(&exec_ctx, tables[2]), (std::vector<int32_t>{20, 21, 22}));
    }
}

} // namespace HaruhiDB::execution
