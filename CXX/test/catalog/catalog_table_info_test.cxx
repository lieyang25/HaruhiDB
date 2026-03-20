/**
 * CXX/test/catalog/catalog_table_info_test.cxx
 */

#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "storage/disk/disk_manager.h"
#include "storage/record/tuple.h"
#include "storage/record/tuple_codec.h"
#include "storage/wal/wal_manager.h"
#include "table/table_iterator.h"
#include "type/type.h"
#include "type/value.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HaruhiDB::catalog
{

namespace
{
    Schema MakeSimpleSchema()
    {
        auto schema_exp = Schema::Create({
            Column("id", type::TypeId::INTEGER, false),
            Column("name", type::TypeId::VARCHAR, 32, true),
        });
        EXPECT_TRUE(schema_exp.has_value());
        return std::move(schema_exp.value());
    }

    Schema MakeStringFirstSchema()
    {
        auto schema_exp = Schema::Create({
            Column("name", type::TypeId::VARCHAR, 32, false),
            Column("id", type::TypeId::INTEGER, false),
        });
        EXPECT_TRUE(schema_exp.has_value());
        return std::move(schema_exp.value());
    }

    record::Tuple MakeTupleFromString(const std::string& text)
    {
        std::vector<std::byte> bytes(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            bytes[i] = static_cast<std::byte>(text[i]);
        }
        return record::Tuple(std::move(bytes));
    }

    std::string TupleToString(const record::Tuple& tuple)
    {
        return std::string(
            reinterpret_cast<const char*>(tuple.Data()),
            reinterpret_cast<const char*>(tuple.Data()) + tuple.Size());
    }

    record::Tuple MakeStudentTuple(int32_t id, std::string name)
    {
        std::vector<type::Value> values{
            type::Value::Int32(id),
            type::Value::VarChar(std::move(name)),
        };
        auto tuple_exp = record::TupleCodec::Encode(
            MakeSimpleSchema(),
            values);
        EXPECT_TRUE(tuple_exp.has_value());
        if (!tuple_exp.has_value()) {
            return {};
        }
        return std::move(tuple_exp.value());
    }

    record::Tuple MakeStringFirstTuple(std::string name, int32_t id)
    {
        std::vector<type::Value> values{
            type::Value::VarChar(std::move(name)),
            type::Value::Int32(id),
        };
        auto tuple_exp = record::TupleCodec::Encode(
            MakeStringFirstSchema(),
            values);
        EXPECT_TRUE(tuple_exp.has_value());
        if (!tuple_exp.has_value()) {
            return {};
        }
        return std::move(tuple_exp.value());
    }
} // namespace

class CatalogTableInfoTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto filename =
            std::string("catalog_") + info->test_suite_name() + "_" + info->name() + ".db";
        db_path_ = std::filesystem::temp_directory_path() / filename;

        std::error_code ec;
        std::filesystem::remove(db_path_, ec);

        disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
        buffer_pool_manager_ =
            std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());
    }

    void TearDown() override
    {
        buffer_pool_manager_.reset();
        disk_manager_.reset();

        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::filesystem::path db_path_;
    std::unique_ptr<storage::DiskManager> disk_manager_;
    std::unique_ptr<buffer::BufferPoolManager> buffer_pool_manager_;
};

TEST_F(CatalogTableInfoTest, CreateAndLookupTable)
{
    Catalog catalog(buffer_pool_manager_.get());
    Schema schema = MakeSimpleSchema();

    auto created = catalog.CreateTable("student", schema);
    ASSERT_TRUE(created.has_value());
    ASSERT_NE(created.value(), nullptr);

    TableInfo* table_info = created.value();
    EXPECT_EQ(table_info->Name(), "student");
    EXPECT_EQ(table_info->Oid(), 0u);
    EXPECT_EQ(table_info->GetSchema().ColumnCount(), schema.ColumnCount());
    ASSERT_NE(table_info->GetTableHeap(), nullptr);
    EXPECT_NE(table_info->GetTableHeap()->FirstPageId(), INVALID_PAGE_ID);

    EXPECT_TRUE(catalog.HasTable("student"));
    EXPECT_TRUE(catalog.HasTable("STUDENT"));
    EXPECT_EQ(catalog.TableCount(), 1u);
    EXPECT_EQ(catalog.NextTableOid(), 1u);

    TableInfo* by_name = catalog.GetTable("Student");
    ASSERT_NE(by_name, nullptr);
    EXPECT_EQ(by_name, table_info);

    TableInfo* by_oid = catalog.GetTable(table_info->Oid());
    ASSERT_NE(by_oid, nullptr);
    EXPECT_EQ(by_oid, table_info);
}

TEST_F(CatalogTableInfoTest, DuplicateTableNameIsRejectedCaseInsensitive)
{
    Catalog catalog(buffer_pool_manager_.get());
    Schema schema = MakeSimpleSchema();

    auto first = catalog.CreateTable("student", schema);
    ASSERT_TRUE(first.has_value());

    auto second = catalog.CreateTable("STUDENT", schema);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(catalog.TableCount(), 1u);
    EXPECT_EQ(catalog.NextTableOid(), 1u);

    auto third = catalog.CreateTable("teacher", schema);
    ASSERT_TRUE(third.has_value());
    ASSERT_NE(third.value(), nullptr);
    EXPECT_EQ(third.value()->Oid(), 1u);
}

TEST_F(CatalogTableInfoTest, TableInfoIndexOidDeduplicates)
{
    Catalog catalog(buffer_pool_manager_.get());
    Schema schema = MakeSimpleSchema();

    auto created = catalog.CreateTable("course", schema);
    ASSERT_TRUE(created.has_value());
    TableInfo* table_info = created.value();
    ASSERT_NE(table_info, nullptr);

    table_info->AddIndexOid(7);
    table_info->AddIndexOid(7);
    table_info->AddIndexOid(9);

    const auto& index_oids = table_info->IndexOids();
    ASSERT_EQ(index_oids.size(), 2u);
    EXPECT_EQ(index_oids[0], 7u);
    EXPECT_EQ(index_oids[1], 9u);
}

TEST_F(CatalogTableInfoTest, CatalogCreateIndexAndLookup)
{
    Catalog catalog(buffer_pool_manager_.get());
    Schema schema = MakeSimpleSchema();

    auto created = catalog.CreateTable("score", schema);
    ASSERT_TRUE(created.has_value());
    TableInfo* table_info = created.value();
    ASSERT_NE(table_info, nullptr);

    auto index_exp = catalog.CreateIndex("score", "idx_score_id");
    ASSERT_TRUE(index_exp.has_value());
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    EXPECT_EQ(catalog.NextIndexOid(), 1u);
    ASSERT_EQ(table_info->IndexOids().size(), 1u);
    EXPECT_EQ(table_info->IndexOids()[0], 0u);

    ASSERT_TRUE(index->Insert(10, record::RID(100, 10)));
    ASSERT_TRUE(index->Insert(20, record::RID(100, 20)));

    record::RID out;
    ASSERT_TRUE(index->GetValue(10, &out));
    EXPECT_EQ(out, record::RID(100, 10));
    ASSERT_TRUE(index->GetValue(20, &out));
    EXPECT_EQ(out, record::RID(100, 20));
}

TEST_F(CatalogTableInfoTest, CatalogCreateIndexBackfillsExistingTableRows)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto created = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(created.has_value());
    TableInfo* table_info = created.value();
    ASSERT_NE(table_info, nullptr);
    ASSERT_NE(table_info->GetTableHeap(), nullptr);

    record::RID rid1;
    record::RID rid2;
    ASSERT_TRUE(table_info->GetTableHeap()->InsertTuple(MakeStudentTuple(1, "a"), &rid1));
    ASSERT_TRUE(table_info->GetTableHeap()->InsertTuple(MakeStudentTuple(2, "b"), &rid2));

    auto index_exp = catalog.CreateIndex("student", "idx_student_id");
    ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
    storage::BPlusTree* index = index_exp.value();
    ASSERT_NE(index, nullptr);

    record::RID out;
    ASSERT_TRUE(index->GetValue(1, &out));
    EXPECT_EQ(out, rid1);
    ASSERT_TRUE(index->GetValue(2, &out));
    EXPECT_EQ(out, rid2);
}

TEST_F(CatalogTableInfoTest, CatalogCreateIndexBackfillFailureOnDuplicateKeyRollsBack)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto created = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(created.has_value());
    TableInfo* table_info = created.value();
    ASSERT_NE(table_info, nullptr);
    ASSERT_NE(table_info->GetTableHeap(), nullptr);

    record::RID rid1;
    record::RID rid2;
    ASSERT_TRUE(table_info->GetTableHeap()->InsertTuple(MakeStudentTuple(1, "a"), &rid1));
    ASSERT_TRUE(table_info->GetTableHeap()->InsertTuple(MakeStudentTuple(1, "b"), &rid2));

    const index_oid_t old_next_index_oid = catalog.NextIndexOid();
    auto failed_index = catalog.CreateIndex("student", "idx_student_id");
    ASSERT_FALSE(failed_index.has_value());
    EXPECT_NE(failed_index.error().find("backfill"), std::string::npos);
    EXPECT_EQ(catalog.NextIndexOid(), old_next_index_oid);
    EXPECT_TRUE(table_info->IndexOids().empty());
    EXPECT_TRUE(table_info->IndexEntries().empty());

    auto second_table = catalog.CreateTable("student2", MakeSimpleSchema());
    ASSERT_TRUE(second_table.has_value());
    ASSERT_NE(second_table.value(), nullptr);
    record::RID rid3;
    ASSERT_TRUE(second_table.value()->GetTableHeap()->InsertTuple(MakeStudentTuple(3, "c"), &rid3));

    auto retry_index = catalog.CreateIndex("student2", "idx_student2_id");
    ASSERT_TRUE(retry_index.has_value()) << retry_index.error();
    ASSERT_EQ(second_table.value()->IndexOids().size(), 1u);
    EXPECT_EQ(second_table.value()->IndexOids()[0], old_next_index_oid);
}

TEST_F(CatalogTableInfoTest, CatalogCreateIndexRejectsNonIntegerFirstColumn)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto created = catalog.CreateTable("student", MakeStringFirstSchema());
    ASSERT_TRUE(created.has_value());
    TableInfo* table_info = created.value();
    ASSERT_NE(table_info, nullptr);
    ASSERT_NE(table_info->GetTableHeap(), nullptr);

    record::RID rid;
    ASSERT_TRUE(table_info->GetTableHeap()->InsertTuple(MakeStringFirstTuple("alice", 1), &rid));

    const index_oid_t old_next_index_oid = catalog.NextIndexOid();
    auto failed_index = catalog.CreateIndex("student", "idx_student_name");
    ASSERT_FALSE(failed_index.has_value());
    EXPECT_NE(failed_index.error().find("must be INTEGER"), std::string::npos);
    EXPECT_EQ(catalog.NextIndexOid(), old_next_index_oid);
    EXPECT_TRUE(table_info->IndexOids().empty());
    EXPECT_TRUE(table_info->IndexEntries().empty());
}

TEST_F(CatalogTableInfoTest, DropIndexRemovesIndexAndRecoveryDoesNotBringItBack)
{
    table_oid_t table_oid = 0;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto created = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(created.has_value());
        ASSERT_NE(created.value(), nullptr);
        table_oid = created.value()->Oid();

        auto index_exp = catalog.CreateIndex("student", "idx_student_id");
        ASSERT_TRUE(index_exp.has_value()) << index_exp.error();
        ASSERT_EQ(created.value()->IndexEntries().size(), 1u);

        auto dropped = catalog.DropIndex("student", "idx_student_id");
        ASSERT_TRUE(dropped.has_value()) << dropped.error();
        EXPECT_TRUE(created.value()->IndexEntries().empty());
        EXPECT_TRUE(created.value()->IndexOids().empty());
        EXPECT_EQ(catalog.GetIndex(table_oid, 0), nullptr);

        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    auto* recovered_table = recovered_catalog.GetTable("student");
    ASSERT_NE(recovered_table, nullptr);
    EXPECT_TRUE(recovered_table->IndexEntries().empty());
}

TEST_F(CatalogTableInfoTest, DropTableCascadesIndexesAndRecoveryDoesNotBringItBack)
{
    {
        Catalog catalog(buffer_pool_manager_.get());
        auto created = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(created.has_value());
        ASSERT_NE(created.value(), nullptr);

        ASSERT_TRUE(catalog.CreateIndex("student", "idx_student_a").has_value());
        ASSERT_TRUE(catalog.CreateIndex("student", "idx_student_b").has_value());
        ASSERT_EQ(created.value()->IndexEntries().size(), 2u);

        auto dropped = catalog.DropTable("student");
        ASSERT_TRUE(dropped.has_value()) << dropped.error();
        EXPECT_FALSE(catalog.HasTable("student"));
        EXPECT_EQ(catalog.GetTable("student"), nullptr);
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    EXPECT_EQ(recovered_catalog.GetTable("student"), nullptr);
}

TEST_F(CatalogTableInfoTest, DropStrictlyRejectsMissingObjects)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto created = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(created.has_value());
    ASSERT_NE(created.value(), nullptr);

    auto drop_missing_index = catalog.DropIndex("student", "idx_missing");
    ASSERT_FALSE(drop_missing_index.has_value());
    EXPECT_NE(drop_missing_index.error().find("index not found"), std::string::npos);

    auto drop_missing_table = catalog.DropTable("student_missing");
    ASSERT_FALSE(drop_missing_table.has_value());
    EXPECT_NE(drop_missing_table.error().find("table not found"), std::string::npos);
}

TEST_F(CatalogTableInfoTest, DropIndexOnPinnedPageUsesPendingReclaimThenRetry)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto created = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(created.has_value());
    ASSERT_NE(created.value(), nullptr);

    ASSERT_TRUE(catalog.CreateIndex("student", "idx_student_id").has_value());
    auto header_page_id = created.value()->GetIndexHeaderPageId(0);
    ASSERT_TRUE(header_page_id.has_value());
    ASSERT_NE(header_page_id.value(), INVALID_PAGE_ID);

    auto pinned_page = buffer_pool_manager_->FetchPage(header_page_id.value());
    ASSERT_TRUE(pinned_page.has_value());

    auto dropped = catalog.DropIndex("student", "idx_student_id");
    ASSERT_TRUE(dropped.has_value()) << dropped.error();
    EXPECT_TRUE(created.value()->IndexEntries().empty());

    ASSERT_TRUE(buffer_pool_manager_->UnpinPage(header_page_id.value(), false));

    auto retry_index = catalog.CreateIndex("student", "idx_student_retry");
    ASSERT_TRUE(retry_index.has_value()) << retry_index.error();
    ASSERT_EQ(created.value()->IndexEntries().size(), 1u);

    // 触发重试后，已释放的索引 header 页应被优先复用于新页分配。
    EXPECT_EQ(created.value()->IndexEntries()[0].header_page_id, header_page_id.value());
}

TEST_F(CatalogTableInfoTest, CatalogAutoDiscoversTableAndSchemaAfterRestart)
{
    page_id_t first_page_id = INVALID_PAGE_ID;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto created = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(created.has_value());
        ASSERT_NE(created.value(), nullptr);

        first_page_id = created.value()->GetTableHeap()->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    const TableInfo* recovered = recovered_catalog.GetTable("student");
    ASSERT_NE(recovered, nullptr);
    ASSERT_NE(recovered->GetTableHeap(), nullptr);
    EXPECT_EQ(recovered->GetTableHeap()->FirstPageId(), first_page_id);
    EXPECT_EQ(recovered->GetSchema().ColumnCount(), 2u);
    EXPECT_EQ(recovered->GetSchema().GetColumn(0).Name(), "id");
    EXPECT_EQ(recovered->GetSchema().GetColumn(0).Type(), type::TypeId::INTEGER);
    EXPECT_EQ(recovered->GetSchema().GetColumn(1).Name(), "name");
    EXPECT_EQ(recovered->GetSchema().GetColumn(1).Type(), type::TypeId::VARCHAR);
    EXPECT_EQ(recovered->GetSchema().GetColumn(1).Length(), 32u);
}

TEST_F(CatalogTableInfoTest, CatalogAutoRecoversIndexFromHeaderPageId)
{
    page_id_t index_header_page_id = INVALID_PAGE_ID;
    constexpr index_oid_t kIndexOid = 0;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto created = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(created.has_value());

        auto index_exp = catalog.CreateIndex("student", "idx_student_id");
        ASSERT_TRUE(index_exp.has_value());
        storage::BPlusTree* index = index_exp.value();
        ASSERT_NE(index, nullptr);

        ASSERT_TRUE(index->Insert(1, record::RID(77, 1)));
        ASSERT_TRUE(index->Insert(2, record::RID(77, 2)));
        ASSERT_TRUE(index->Insert(3, record::RID(77, 3)));

        auto header_page_id = created.value()->GetIndexHeaderPageId(kIndexOid);
        ASSERT_TRUE(header_page_id.has_value());
        index_header_page_id = header_page_id.value();
        ASSERT_NE(index_header_page_id, INVALID_PAGE_ID);
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    const TableInfo* recovered_table = recovered_catalog.GetTable("student");
    ASSERT_NE(recovered_table, nullptr);
    auto recovered_header = recovered_table->GetIndexHeaderPageId(kIndexOid);
    ASSERT_TRUE(recovered_header.has_value());
    EXPECT_EQ(recovered_header.value(), index_header_page_id);

    storage::BPlusTree* recovered_index = recovered_catalog.GetIndex(recovered_table->Oid(), kIndexOid);
    ASSERT_NE(recovered_index, nullptr);
    record::RID out;
    ASSERT_TRUE(recovered_index->GetValue(1, &out));
    EXPECT_EQ(out, record::RID(77, 1));
    ASSERT_TRUE(recovered_index->GetValue(2, &out));
    EXPECT_EQ(out, record::RID(77, 2));
    ASSERT_TRUE(recovered_index->GetValue(3, &out));
    EXPECT_EQ(out, record::RID(77, 3));
}

TEST_F(CatalogTableInfoTest, WalRecoverThenCatalogLoadKeepsEntrypointsConsistent)
{
    auto wal_path = db_path_;
    wal_path.replace_extension(".wal");
    std::error_code ec;
    std::filesystem::remove(wal_path, ec);

    page_id_t first_page_id = INVALID_PAGE_ID;
    page_id_t index_header_page_id = INVALID_PAGE_ID;
    record::RID tuple_rid;
    constexpr int32_t kIndexKey = 42;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto created = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(created.has_value());
        TableInfo* table = created.value();
        ASSERT_NE(table, nullptr);

        auto index_exp = catalog.CreateIndex("student", "idx_student_id");
        ASSERT_TRUE(index_exp.has_value());
        storage::BPlusTree* index = index_exp.value();
        ASSERT_NE(index, nullptr);

        storage::wal::WalManager wal(wal_path);
        table->GetTableHeap()->SetWalManager(&wal);
        ASSERT_TRUE(table->GetTableHeap()->InsertTuple(MakeTupleFromString("wal_catalog_row"), &tuple_rid));
        ASSERT_TRUE(index->Insert(kIndexKey, tuple_rid));

        first_page_id = table->GetTableHeap()->FirstPageId();
        auto header_page_id = table->GetIndexHeaderPageId(0);
        ASSERT_TRUE(header_page_id.has_value());
        index_header_page_id = header_page_id.value();
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    {
        storage::wal::WalManager wal(wal_path);
        ASSERT_TRUE(wal.Recover(buffer_pool_manager_.get()));
    }

    Catalog recovered_catalog(buffer_pool_manager_.get());
    TableInfo* recovered_table = recovered_catalog.GetTable("student");
    ASSERT_NE(recovered_table, nullptr);
    ASSERT_NE(recovered_table->GetTableHeap(), nullptr);
    EXPECT_EQ(recovered_table->GetTableHeap()->FirstPageId(), first_page_id);
    auto recovered_header_page_id = recovered_table->GetIndexHeaderPageId(0);
    ASSERT_TRUE(recovered_header_page_id.has_value());
    EXPECT_EQ(recovered_header_page_id.value(), index_header_page_id);

    storage::BPlusTree* recovered_index = recovered_catalog.GetIndex(recovered_table->Oid(), 0);
    ASSERT_NE(recovered_index, nullptr);
    record::RID recovered_rid;
    ASSERT_TRUE(recovered_index->GetValue(kIndexKey, &recovered_rid));
    EXPECT_EQ(recovered_rid, tuple_rid);

    record::Tuple out_tuple;
    ASSERT_TRUE(recovered_table->GetTableHeap()->GetTuple(tuple_rid, &out_tuple));
    std::string recovered_payload(
        reinterpret_cast<const char*>(out_tuple.Data()),
        reinterpret_cast<const char*>(out_tuple.Data()) + out_tuple.Size());
    EXPECT_EQ(recovered_payload, "wal_catalog_row");
}

TEST_F(CatalogTableInfoTest, ConstructorRejectsNullBufferPoolManager)
{
    EXPECT_THROW((Catalog(nullptr)), std::invalid_argument);
}

TEST_F(CatalogTableInfoTest, TableNameLeadingTrailingSpacesAreRejected)
{
    Catalog catalog(buffer_pool_manager_.get());
    Schema schema = MakeSimpleSchema();

    auto leading = catalog.CreateTable(" student", schema);
    ASSERT_FALSE(leading.has_value());
    EXPECT_NE(leading.error().find("leading/trailing spaces not allowed"), std::string::npos);

    auto trailing = catalog.CreateTable("student ", schema);
    ASSERT_FALSE(trailing.has_value());
    EXPECT_NE(trailing.error().find("leading/trailing spaces not allowed"), std::string::npos);

    auto normal = catalog.CreateTable("student", schema);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value()->Oid(), 0u);
}

TEST_F(CatalogTableInfoTest, RestartThenCreateTableAndIndexKeepsOidContinuous)
{
    table_oid_t next_table_oid_before_restart = 0;
    index_oid_t next_index_oid_before_restart = 0;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto t0 = catalog.CreateTable("t0", MakeSimpleSchema());
        auto t1 = catalog.CreateTable("t1", MakeSimpleSchema());
        ASSERT_TRUE(t0.has_value());
        ASSERT_TRUE(t1.has_value());

        ASSERT_TRUE(catalog.CreateIndex(t0.value()->Oid(), "idx_t0_a").has_value());
        ASSERT_TRUE(catalog.CreateIndex(t0.value()->Oid(), "idx_t0_b").has_value());
        ASSERT_TRUE(catalog.CreateIndex(t1.value()->Oid(), "idx_t1_a").has_value());

        next_table_oid_before_restart = catalog.NextTableOid();
        next_index_oid_before_restart = catalog.NextIndexOid();
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    EXPECT_EQ(recovered_catalog.NextTableOid(), next_table_oid_before_restart);
    EXPECT_EQ(recovered_catalog.NextIndexOid(), next_index_oid_before_restart);

    auto t2 = recovered_catalog.CreateTable("t2", MakeSimpleSchema());
    ASSERT_TRUE(t2.has_value());
    ASSERT_NE(t2.value(), nullptr);
    EXPECT_EQ(t2.value()->Oid(), next_table_oid_before_restart);

    auto idx = recovered_catalog.CreateIndex(t2.value()->Oid(), "idx_t2_a");
    ASSERT_TRUE(idx.has_value());
    ASSERT_EQ(t2.value()->IndexOids().size(), 1u);
    EXPECT_EQ(t2.value()->IndexOids()[0], next_index_oid_before_restart);
}

TEST_F(CatalogTableInfoTest, RecoversMultiTableMultiIndexCatalog)
{
    struct ExpectedIndexPoint
    {
        table_oid_t table_oid{0};
        index_oid_t index_oid{0};
        int32_t key{0};
        record::RID rid;
    };

    std::vector<ExpectedIndexPoint> expected_points;

    {
        Catalog catalog(buffer_pool_manager_.get());

        for (int t = 0; t < 3; ++t) {
            const std::string table_name = "table_" + std::to_string(t);
            auto table = catalog.CreateTable(table_name, MakeSimpleSchema());
            ASSERT_TRUE(table.has_value());
            ASSERT_NE(table.value(), nullptr);

            for (int i = 0; i < 2; ++i) {
                const std::string index_name = "idx_" + std::to_string(t) + "_" + std::to_string(i);
                auto index = catalog.CreateIndex(table.value()->Oid(), index_name);
                ASSERT_TRUE(index.has_value());
                ASSERT_NE(index.value(), nullptr);
                ASSERT_FALSE(table.value()->IndexOids().empty());

                const index_oid_t index_oid = table.value()->IndexOids().back();
                const int32_t key = t * 100 + i;
                const record::RID rid(static_cast<page_id_t>(7000 + key), static_cast<slot_id_t>(i + 1));
                ASSERT_TRUE(index.value()->Insert(key, rid));

                expected_points.push_back(ExpectedIndexPoint{
                    .table_oid = table.value()->Oid(),
                    .index_oid = index_oid,
                    .key = key,
                    .rid = rid,
                });
            }
        }

        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(16, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    for (const auto& point : expected_points) {
        auto* table = recovered_catalog.GetTable(point.table_oid);
        ASSERT_NE(table, nullptr);

        auto* index = recovered_catalog.GetIndex(point.table_oid, point.index_oid);
        ASSERT_NE(index, nullptr);

        record::RID out;
        ASSERT_TRUE(index->GetValue(point.key, &out));
        EXPECT_EQ(out, point.rid);
    }
}

TEST_F(CatalogTableInfoTest, RestartThenSeqScanReadsBackInsertedTuples)
{
    const std::vector<std::string> inserted = {"alpha", "beta", "gamma"};

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto table = catalog.CreateTable("scan_table", MakeSimpleSchema());
        ASSERT_TRUE(table.has_value());
        ASSERT_NE(table.value(), nullptr);

        for (const auto& payload : inserted) {
            record::RID rid;
            ASSERT_TRUE(table.value()->GetTableHeap()->InsertTuple(MakeTupleFromString(payload), &rid));
        }
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(16, disk_manager_.get());

    Catalog recovered_catalog(buffer_pool_manager_.get());
    auto* recovered_table = recovered_catalog.GetTable("scan_table");
    ASSERT_NE(recovered_table, nullptr);

    std::vector<std::string> scanned;
    for (auto it = recovered_table->GetTableHeap()->Begin(); it != recovered_table->GetTableHeap()->End(); ++it) {
        scanned.push_back(TupleToString(*it));
    }
    EXPECT_EQ(scanned, inserted);
}

TEST_F(CatalogTableInfoTest, CorruptedCatalogMetaPageFailsToLoad)
{
    page_id_t catalog_meta_page_id = INVALID_PAGE_ID;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto table = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(table.has_value());
        catalog_meta_page_id = disk_manager_->CatalogMetaPageId();
        ASSERT_NE(catalog_meta_page_id, INVALID_PAGE_ID);

        auto page_exp = buffer_pool_manager_->FetchPage(catalog_meta_page_id);
        ASSERT_TRUE(page_exp.has_value());
        storage::Page* page = page_exp.value();
        page->WLock();
        page->Header()->opaque[0] = static_cast<uint8_t>(0);
        page->Header()->opaque[1] = static_cast<uint8_t>(0);
        page->Header()->opaque[2] = static_cast<uint8_t>(0);
        page->Header()->opaque[3] = static_cast<uint8_t>(0);
        page->MarkDirty();
        page->WUnLock();
        ASSERT_TRUE(buffer_pool_manager_->UnpinPage(catalog_meta_page_id, true));
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    EXPECT_THROW((Catalog(buffer_pool_manager_.get())), std::runtime_error);
}

TEST_F(CatalogTableInfoTest, CorruptedCatalogNextOidFailsToLoad)
{
    page_id_t catalog_meta_page_id = INVALID_PAGE_ID;

    {
        Catalog catalog(buffer_pool_manager_.get());
        auto table = catalog.CreateTable("student", MakeSimpleSchema());
        ASSERT_TRUE(table.has_value());
        ASSERT_TRUE(catalog.CreateIndex("student", "idx_student").has_value());
        catalog_meta_page_id = disk_manager_->CatalogMetaPageId();
        ASSERT_NE(catalog_meta_page_id, INVALID_PAGE_ID);

        auto page_exp = buffer_pool_manager_->FetchPage(catalog_meta_page_id);
        ASSERT_TRUE(page_exp.has_value());
        storage::Page* page = page_exp.value();
        page->WLock();

        uint32_t payload_size = 0;
        std::memcpy(&payload_size, page->Header()->opaque + 12, sizeof(payload_size));
        ASSERT_GE(payload_size, 16u);

        uint32_t bad_next_index_oid = 0;
        std::memcpy(page->RawData() + HEADER_SIZE + 12, &bad_next_index_oid, sizeof(bad_next_index_oid));
        page->MarkDirty();
        page->WUnLock();
        ASSERT_TRUE(buffer_pool_manager_->UnpinPage(catalog_meta_page_id, true));
        ASSERT_TRUE(buffer_pool_manager_->FlushAllPages().has_value());
    }

    buffer_pool_manager_.reset();
    disk_manager_.reset();
    disk_manager_ = std::make_unique<storage::DiskManager>(db_path_);
    buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(8, disk_manager_.get());

    EXPECT_THROW((Catalog(buffer_pool_manager_.get())), std::runtime_error);
}

TEST_F(CatalogTableInfoTest, CreateIndexRollbackOnPersistFailureDoesNotLeaveOidHole)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto table = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(table.has_value());
    ASSERT_NE(table.value(), nullptr);

    const index_oid_t old_next_index_oid = catalog.NextIndexOid();
    catalog.FailNextPersistsForTest(1);

    auto failed = catalog.CreateIndex(table.value()->Oid(), "idx_fail_once");
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(catalog.NextIndexOid(), old_next_index_oid);
    EXPECT_TRUE(table.value()->IndexEntries().empty());
    EXPECT_TRUE(table.value()->IndexOids().empty());

    auto retry = catalog.CreateIndex(table.value()->Oid(), "idx_retry");
    ASSERT_TRUE(retry.has_value());
    ASSERT_EQ(table.value()->IndexOids().size(), 1u);
    EXPECT_EQ(table.value()->IndexOids()[0], old_next_index_oid);
}

TEST_F(CatalogTableInfoTest, LoadIndexRollbackOnPersistFailureDoesNotLeaveOidHole)
{
    Catalog catalog(buffer_pool_manager_.get());
    auto table = catalog.CreateTable("student", MakeSimpleSchema());
    ASSERT_TRUE(table.has_value());
    ASSERT_NE(table.value(), nullptr);

    auto base_index = catalog.CreateIndex(table.value()->Oid(), "idx_base");
    ASSERT_TRUE(base_index.has_value());
    auto base_header = table.value()->GetIndexHeaderPageId(0);
    ASSERT_TRUE(base_header.has_value());

    const index_oid_t old_next_index_oid = catalog.NextIndexOid();
    catalog.FailNextPersistsForTest(1);

    auto failed_load = catalog.LoadIndex(
        table.value()->Oid(),
        old_next_index_oid,
        "idx_alias_fail",
        base_header.value());
    ASSERT_FALSE(failed_load.has_value());
    EXPECT_EQ(catalog.NextIndexOid(), old_next_index_oid);
    ASSERT_EQ(table.value()->IndexEntries().size(), 1u);
    ASSERT_EQ(table.value()->IndexOids().size(), 1u);
    EXPECT_EQ(table.value()->IndexOids()[0], 0u);

    auto retry_load = catalog.LoadIndex(
        table.value()->Oid(),
        old_next_index_oid,
        "idx_alias_ok",
        base_header.value());
    ASSERT_TRUE(retry_load.has_value());
    EXPECT_EQ(catalog.NextIndexOid(), old_next_index_oid + 1);
}

} // namespace HaruhiDB::catalog
