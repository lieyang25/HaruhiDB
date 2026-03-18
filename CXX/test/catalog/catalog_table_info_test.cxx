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
#include "storage/wal/wal_manager.h"
#include "type/type.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
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

    record::Tuple MakeTupleFromString(const std::string& text)
    {
        std::vector<std::byte> bytes(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            bytes[i] = static_cast<std::byte>(text[i]);
        }
        return record::Tuple(std::move(bytes));
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

} // namespace HaruhiDB::catalog
