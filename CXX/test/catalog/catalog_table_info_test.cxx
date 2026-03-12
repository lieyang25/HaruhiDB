/**
 * CXX/test/catalog/catalog_table_info_test.cxx
 */

#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "storage/disk/disk_manager.h"
#include "type/type.h"

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

} // namespace HaruhiDB::catalog
