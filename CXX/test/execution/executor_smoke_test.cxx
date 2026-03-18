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

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace HaruhiDB::execution
{

class ExecutorSmokeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto filename = std::string("execution_") + info->test_suite_name() + "_" + info->name() + ".db";
        db_path_ = std::filesystem::temp_directory_path() / filename;
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::filesystem::path db_path_;
};

TEST_F(ExecutorSmokeTest, MinimalPipelineWorks)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 16, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();

    auto table_exp = catalog.CreateTable("student", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(1), type::Value::VarChar("haruhi")},
            {type::Value::Int32(2), type::Value::VarChar("mio")},
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();

    ExecutorRow insert_result;
    ASSERT_TRUE(insert.Next(&insert_result));
    ASSERT_EQ(insert_result.values.size(), 1u);
    EXPECT_EQ(insert_result.values[0], type::Value::Int32(2));
    EXPECT_FALSE(insert.Next(&insert_result));

    SeqScanExecutor scan(&exec_ctx, table_info);
    scan.Init();

    int scan_count = 0;
    ExecutorRow scanned_row;
    while (scan.Next(&scanned_row)) {
        EXPECT_TRUE(scanned_row.has_rid);
        EXPECT_NE(scanned_row.rid.GetPageId(), INVALID_PAGE_ID);
        EXPECT_NE(scanned_row.rid.GetSlotId(), INVALID_SLOT_ID);
        ++scan_count;
    }
    EXPECT_EQ(scan_count, 2);

    auto filtered_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(filtered_child),
        [](const ExecutorRow& row) {
            if (row.values.empty()) {
                return false;
            }
            return row.values[0] == type::Value::Int32(1);
        });

    ProjectionExecutor projection(&exec_ctx, std::move(filter), {1});
    projection.Init();

    ExecutorRow projected_row;
    ASSERT_TRUE(projection.Next(&projected_row));
    ASSERT_EQ(projected_row.values.size(), 1u);
    EXPECT_EQ(projected_row.values[0], type::Value::VarChar("haruhi"));
    EXPECT_FALSE(projection.Next(&projected_row));

    auto delete_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    DeleteExecutor deleter(&exec_ctx, table_info, std::move(delete_child));
    deleter.Init();

    ExecutorRow delete_result;
    ASSERT_TRUE(deleter.Next(&delete_result));
    ASSERT_EQ(delete_result.values.size(), 1u);
    EXPECT_EQ(delete_result.values[0], type::Value::Int32(2));
    EXPECT_FALSE(deleter.Next(&delete_result));

    SeqScanExecutor scan_after_delete(&exec_ctx, table_info);
    scan_after_delete.Init();
    ExecutorRow row_after_delete;
    EXPECT_FALSE(scan_after_delete.Next(&row_after_delete));
}

TEST_F(ExecutorSmokeTest, IndexScanExecutorWorks)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 16, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();

    auto table_exp = catalog.CreateTable("student", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex(table_info->Oid(), "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(10), type::Value::VarChar("yui")},
            {type::Value::Int32(20), type::Value::VarChar("azusa")},
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();
    ExecutorRow insert_result;
    ASSERT_TRUE(insert.Next(&insert_result));

    IndexScanExecutor index_scan(&exec_ctx, table_info, index, 20);
    index_scan.Init();

    ExecutorRow out;
    ASSERT_TRUE(index_scan.Next(&out));
    EXPECT_EQ(out.values[0], type::Value::Int32(20));
    EXPECT_EQ(out.values[1], type::Value::VarChar("azusa"));
    EXPECT_TRUE(out.has_rid);
    EXPECT_FALSE(index_scan.Next(&out));
}

TEST_F(ExecutorSmokeTest, UpdateExecutorWorks)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(64, &dm);
    catalog::Catalog catalog(&bpm);

    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 16, false),
    });
    ASSERT_TRUE(schema_exp.has_value()) << schema_exp.error();

    auto table_exp = catalog.CreateTable("student", schema_exp.value());
    ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
    catalog::TableInfo* table_info = table_exp.value();
    ASSERT_NE(table_info, nullptr);

    ExecutorContext exec_ctx(&catalog);

    auto values = std::make_unique<ValuesExecutor>(
        &exec_ctx,
        std::vector<std::vector<type::Value>>{
            {type::Value::Int32(10), type::Value::VarChar("ui")},
            {type::Value::Int32(20), type::Value::VarChar("sawa")},
        });

    InsertExecutor insert(&exec_ctx, table_info, std::move(values));
    insert.Init();
    ExecutorRow insert_result;
    ASSERT_TRUE(insert.Next(&insert_result));

    auto scan_child = std::make_unique<SeqScanExecutor>(&exec_ctx, table_info);
    auto filter = std::make_unique<FilterExecutor>(
        &exec_ctx,
        std::move(scan_child),
        [](const ExecutorRow& row) {
            if (row.values.empty()) {
                return false;
            }
            return row.values[0] == type::Value::Int32(10);
        });

    UpdateExecutor updater(
        &exec_ctx,
        table_info,
        std::move(filter),
        [](const ExecutorRow& row) {
            auto out = row.values;
            if (out.size() > 1) {
                out[1] = type::Value::VarChar("ritsu");
            }
            return out;
        });
    updater.Init();

    ExecutorRow update_result;
    ASSERT_TRUE(updater.Next(&update_result));
    ASSERT_EQ(update_result.values.size(), 1u);
    EXPECT_EQ(update_result.values[0], type::Value::Int32(1));
    EXPECT_FALSE(updater.Next(&update_result));

    SeqScanExecutor verify_scan(&exec_ctx, table_info);
    verify_scan.Init();

    int seen = 0;
    ExecutorRow row;
    while (verify_scan.Next(&row)) {
        if (row.values[0] == type::Value::Int32(10)) {
            EXPECT_EQ(row.values[1], type::Value::VarChar("ritsu"));
            ++seen;
        }
    }
    EXPECT_EQ(seen, 1);
}

} // namespace HaruhiDB::execution
