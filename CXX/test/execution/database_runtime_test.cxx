#include "gtest/gtest.h"

#include "catalog/column.h"
#include "catalog/schema.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/values_executor.h"
#include "runtime/database_runtime.h"
#include "storage/wal/wal_manager.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace HaruhiDB::runtime
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

int32_t InsertRows(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::vector<std::vector<type::Value>> rows)
{
    auto values = std::make_unique<execution::ValuesExecutor>(exec_ctx, std::move(rows));
    execution::InsertExecutor insert(exec_ctx, table_info, std::move(values));
    insert.Init();

    execution::ExecutorRow result;
    if (!insert.Next(&result) || result.values.size() != 1) {
        return -1;
    }
    const int32_t* count = result.values[0].TryAs<int32_t>();
    return count == nullptr ? -1 : *count;
}

std::vector<int32_t> CollectIdsBySeqScan(
    execution::ExecutorContext* exec_ctx, catalog::TableInfo* table_info)
{
    std::vector<int32_t> ids;
    if (exec_ctx == nullptr || table_info == nullptr) {
        return ids;
    }

    execution::SeqScanExecutor scan(exec_ctx, table_info);
    scan.Init();

    execution::ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values.empty() ? nullptr : row.values[0].TryAs<int32_t>();
        if (id != nullptr) {
            ids.push_back(*id);
        }
    }
    return ids;
}

std::vector<int32_t> CollectIdsByIndexScan(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    storage::BPlusTree* index,
    int32_t start_key)
{
    std::vector<int32_t> ids;
    if (exec_ctx == nullptr || table_info == nullptr || index == nullptr) {
        return ids;
    }

    execution::IndexScanExecutor scan(exec_ctx, table_info, index, start_key);
    scan.Init();

    execution::ExecutorRow row;
    while (scan.Next(&row)) {
        const int32_t* id = row.values.empty() ? nullptr : row.values[0].TryAs<int32_t>();
        if (id != nullptr) {
            ids.push_back(*id);
        }
    }
    return ids;
}

} // namespace

class DatabaseRuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto stem = std::string("database_runtime_") + info->test_suite_name() + "_" + info->name();
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

TEST_F(DatabaseRuntimeTest, OpenRecoversCatalogAndProvidesDirectlyUsableExecutors)
{
    table_oid_t table_oid = 0;
    index_oid_t index_oid = 0;

    {
        auto open_exp = DatabaseRuntime::Open(
            db_path_,
            DatabaseOpenOptions{
                .buffer_pool_size = 64,
                .lru_k = 2,
                .enable_wal = true,
                .wal_path = wal_path_,
            });
        ASSERT_TRUE(open_exp.has_value()) << open_exp.error();
        auto runtime = std::move(open_exp.value());

        auto* catalog = runtime.GetCatalog();
        auto* exec_ctx = runtime.GetExecutorContext();
        ASSERT_NE(catalog, nullptr);
        ASSERT_NE(exec_ctx, nullptr);

        auto table_exp = catalog->CreateTable("student", MakeStudentSchema());
        ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
        catalog::TableInfo* table_info = table_exp.value();
        ASSERT_NE(table_info, nullptr);

        auto index_exp = catalog->CreateIndex(table_info->Oid(), "idx_student_id");
        ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
        ASSERT_NE(index_exp.value(), nullptr);

        ASSERT_EQ(
            InsertRows(
                exec_ctx,
                table_info,
                {
                    {type::Value::Int32(1), type::Value::VarChar("a")},
                    {type::Value::Int32(2), type::Value::VarChar("b")},
                    {type::Value::Int32(3), type::Value::VarChar("c")},
                }),
            3);

        table_oid = table_info->Oid();
        ASSERT_FALSE(table_info->IndexOids().empty());
        index_oid = table_info->IndexOids().front();

        ASSERT_NE(runtime.GetBufferPoolManager(), nullptr);
        ASSERT_TRUE(runtime.GetBufferPoolManager()->FlushAllPages().has_value());
    }

    {
        auto open_exp = DatabaseRuntime::Open(
            db_path_,
            DatabaseOpenOptions{
                .buffer_pool_size = 64,
                .lru_k = 2,
                .enable_wal = true,
                .wal_path = wal_path_,
            });
        ASSERT_TRUE(open_exp.has_value()) << open_exp.error();
        auto runtime = std::move(open_exp.value());

        auto* catalog = runtime.GetCatalog();
        auto* exec_ctx = runtime.GetExecutorContext();
        ASSERT_NE(catalog, nullptr);
        ASSERT_NE(exec_ctx, nullptr);

        auto* recovered_table = catalog->GetTable(table_oid);
        ASSERT_NE(recovered_table, nullptr);
        auto* recovered_index = catalog->GetIndex(table_oid, index_oid);
        ASSERT_NE(recovered_index, nullptr);

        EXPECT_EQ(CollectIdsBySeqScan(exec_ctx, recovered_table), (std::vector<int32_t>{1, 2, 3}));
        EXPECT_EQ(
            CollectIdsByIndexScan(exec_ctx, recovered_table, recovered_index, 2),
            (std::vector<int32_t>{2, 3}));
    }
}

TEST_F(DatabaseRuntimeTest, OpenFailsWhenWalRecoverFails)
{
    {
        storage::DiskManager dm(db_path_);
        storage::wal::WalManager wal(wal_path_);
        storage::wal::LogRecord record{};
        record.type = storage::wal::LogRecordType::PUT;
        record.page_id = 1234;
        record.payload_len = WAL_PAYLOAD_LEN;
        ASSERT_TRUE(wal.AppendLog(record));
        ASSERT_TRUE(wal.FlushLog());
    }

    auto open_exp = DatabaseRuntime::Open(
        db_path_,
        DatabaseOpenOptions{
            .buffer_pool_size = 16,
            .lru_k = 2,
            .enable_wal = true,
            .wal_path = wal_path_,
        });
    ASSERT_FALSE(open_exp.has_value());
}

TEST_F(DatabaseRuntimeTest, OpenFailsWhenCatalogLoadFails)
{
    page_id_t catalog_meta_page_id = INVALID_PAGE_ID;
    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        catalog::Catalog catalog(&bpm);
        auto table_exp = catalog.CreateTable("student", MakeStudentSchema());
        ASSERT_TRUE(table_exp.has_value()) << table_exp.error();

        catalog_meta_page_id = dm.CatalogMetaPageId();
        ASSERT_NE(catalog_meta_page_id, INVALID_PAGE_ID);

        auto page_exp = bpm.FetchPage(catalog_meta_page_id);
        ASSERT_TRUE(page_exp.has_value());
        storage::Page* page = page_exp.value();
        page->WLock();
        page->Header()->opaque[0] = static_cast<uint8_t>(0);
        page->Header()->opaque[1] = static_cast<uint8_t>(0);
        page->Header()->opaque[2] = static_cast<uint8_t>(0);
        page->Header()->opaque[3] = static_cast<uint8_t>(0);
        page->MarkDirty();
        page->WUnLock();
        ASSERT_TRUE(bpm.UnpinPage(catalog_meta_page_id, true));
        ASSERT_TRUE(bpm.FlushAllPages().has_value());
    }

    auto open_exp = DatabaseRuntime::Open(
        db_path_,
        DatabaseOpenOptions{
            .buffer_pool_size = 16,
            .lru_k = 2,
            .enable_wal = false,
            .wal_path = std::nullopt,
        });
    ASSERT_FALSE(open_exp.has_value());
}

TEST_F(DatabaseRuntimeTest, OpenBindsWalManagerToRecoveredAndNewTables)
{
    {
        auto open_exp = DatabaseRuntime::Open(
            db_path_,
            DatabaseOpenOptions{
                .buffer_pool_size = 16,
                .lru_k = 2,
                .enable_wal = true,
                .wal_path = wal_path_,
            });
        ASSERT_TRUE(open_exp.has_value()) << open_exp.error();
        auto runtime = std::move(open_exp.value());
        auto* catalog = runtime.GetCatalog();
        ASSERT_NE(catalog, nullptr);

        auto table_exp = catalog->CreateTable("t0", MakeStudentSchema());
        ASSERT_TRUE(table_exp.has_value()) << table_exp.error();
        ASSERT_NE(table_exp.value(), nullptr);
        ASSERT_NE(table_exp.value()->GetTableHeap(), nullptr);
        ASSERT_NE(table_exp.value()->GetTableHeap()->GetWalManager(), nullptr);
    }

    {
        auto open_exp = DatabaseRuntime::Open(
            db_path_,
            DatabaseOpenOptions{
                .buffer_pool_size = 16,
                .lru_k = 2,
                .enable_wal = true,
                .wal_path = wal_path_,
            });
        ASSERT_TRUE(open_exp.has_value()) << open_exp.error();
        auto runtime = std::move(open_exp.value());
        auto* catalog = runtime.GetCatalog();
        ASSERT_NE(catalog, nullptr);

        auto* recovered_table = catalog->GetTable("t0");
        ASSERT_NE(recovered_table, nullptr);
        ASSERT_NE(recovered_table->GetTableHeap(), nullptr);
        ASSERT_NE(recovered_table->GetTableHeap()->GetWalManager(), nullptr);

        auto new_table_exp = catalog->CreateTable("t1", MakeStudentSchema());
        ASSERT_TRUE(new_table_exp.has_value()) << new_table_exp.error();
        ASSERT_NE(new_table_exp.value(), nullptr);
        ASSERT_NE(new_table_exp.value()->GetTableHeap(), nullptr);
        ASSERT_NE(new_table_exp.value()->GetTableHeap()->GetWalManager(), nullptr);
    }
}

} // namespace HaruhiDB::runtime
