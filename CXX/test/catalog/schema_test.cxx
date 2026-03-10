/**
 * CXX/test/catalog/schema_test.cxx
 */

#include "gtest/gtest.h"

#include "catalog/column.h"
#include "catalog/schema.h"
#include "type/type.h"

#include <array>
#include <string>
#include <vector>

using namespace HaruhiDB::catalog;
using namespace HaruhiDB::type;

TEST(SchemaTest, CreateSuccessAndComputedLayout)
{
    std::vector<Column> columns;
    columns.emplace_back("id", TypeId::INTEGER, false);
    columns.emplace_back("name", TypeId::VARCHAR, 32, true);
    columns.emplace_back("score", TypeId::DOUBLE, true);

    auto schema_result = Schema::Create(std::move(columns));
    ASSERT_TRUE(schema_result.has_value()) << schema_result.error();
    Schema schema = std::move(schema_result.value());

    ASSERT_EQ(schema.ColumnCount(), 3u);
    EXPECT_FALSE(schema.Empty());
    EXPECT_FALSE(schema.IsTupleInlined());

    EXPECT_EQ(schema.GetColumn(0).Offset(), 0u);
    EXPECT_EQ(schema.GetColumn(1).Offset(), schema.GetColumn(0).StorageSize());
    EXPECT_EQ(schema.GetColumn(2).Offset(), schema.GetColumn(0).StorageSize() + schema.GetColumn(1).StorageSize());

    EXPECT_EQ(schema.InlinedStorageSize(),
              schema.GetColumn(0).StorageSize() + schema.GetColumn(1).StorageSize() + schema.GetColumn(2).StorageSize());

    ASSERT_EQ(schema.UninlinedColumns().size(), 1u);
    EXPECT_EQ(schema.UninlinedColumns()[0], 1u);

    const auto names = schema.ColumnNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "id");
    EXPECT_EQ(names[1], "name");
    EXPECT_EQ(names[2], "score");
}

TEST(SchemaTest, CreateAllowsEmptySchema)
{
    auto schema_result = Schema::Create({});
    ASSERT_TRUE(schema_result.has_value()) << schema_result.error();
    Schema schema = std::move(schema_result.value());

    EXPECT_TRUE(schema.Empty());
    EXPECT_EQ(schema.ColumnCount(), 0u);
    EXPECT_TRUE(schema.IsTupleInlined());
    EXPECT_EQ(schema.InlinedStorageSize(), 0u);
    EXPECT_TRUE(schema.UninlinedColumns().empty());
}

TEST(SchemaTest, CreateRejectsDuplicateNamesCaseInsensitive)
{
    std::vector<Column> columns;
    columns.emplace_back("UserId", TypeId::INTEGER, false);
    columns.emplace_back("userid", TypeId::BIGINT, false);

    auto schema_result = Schema::Create(std::move(columns));
    ASSERT_FALSE(schema_result.has_value());
    EXPECT_NE(schema_result.error().find("duplicate column name"), std::string::npos);
}

TEST(SchemaTest, NameLookupIsCaseInsensitive)
{
    std::vector<Column> columns;
    columns.emplace_back("FirstName", TypeId::VARCHAR, 16, true);
    columns.emplace_back("Age", TypeId::INTEGER, false);
    auto schema_result = Schema::Create(std::move(columns));
    ASSERT_TRUE(schema_result.has_value()) << schema_result.error();
    Schema schema = std::move(schema_result.value());

    EXPECT_TRUE(schema.HasColumn("firstname"));
    EXPECT_TRUE(schema.HasColumn("FIRSTNAME"));
    EXPECT_TRUE(schema.HasColumn("age"));
    EXPECT_FALSE(schema.HasColumn("unknown"));

    const auto idx1 = schema.TryGetColumnIndex("FIRSTNAME");
    const auto idx2 = schema.TryGetColumnIndex("firstname");
    ASSERT_TRUE(idx1.has_value());
    ASSERT_TRUE(idx2.has_value());
    EXPECT_EQ(idx1.value(), 0u);
    EXPECT_EQ(idx1.value(), idx2.value());
    EXPECT_EQ(schema.GetColumnIndex("aGe"), 1u);

    EXPECT_THROW(schema.GetColumnIndex("missing"), std::out_of_range);

    const Column* by_name = schema.FindColumn("FIRSTNAME");
    ASSERT_NE(by_name, nullptr);
    EXPECT_EQ(by_name->Name(), schema.GetColumn(0).Name());

    EXPECT_EQ(schema.FindColumn("missing"), nullptr);
}

TEST(SchemaTest, GetColumnBounds)
{
    Schema schema({Column("a", TypeId::INTEGER), Column("b", TypeId::BOOLEAN)});

    EXPECT_NO_THROW(static_cast<void>(schema.GetColumn(0)));
    EXPECT_NO_THROW(static_cast<void>(schema.GetColumn(1)));
    EXPECT_THROW(schema.GetColumn(2), std::out_of_range);
}

TEST(SchemaTest, ProjectKeepsRequestedOrderAndRebuildsLayout)
{
    std::vector<Column> columns;
    columns.emplace_back("c0", TypeId::INTEGER, false);
    columns.emplace_back("c1", TypeId::VARCHAR, 8, true);
    columns.emplace_back("c2", TypeId::BOOLEAN, false);
    auto schema_result = Schema::Create(std::move(columns));
    ASSERT_TRUE(schema_result.has_value()) << schema_result.error();
    Schema schema = std::move(schema_result.value());

    const std::array<uint32_t, 2> indices{2u, 0u};
    Schema projected = schema.Project(indices);

    ASSERT_EQ(projected.ColumnCount(), 2u);
    EXPECT_EQ(projected.ColumnNames()[0], "c2");
    EXPECT_EQ(projected.ColumnNames()[1], "c0");
    EXPECT_TRUE(projected.IsTupleInlined());
    EXPECT_TRUE(projected.UninlinedColumns().empty());

    EXPECT_EQ(projected.GetColumn(0).Offset(), 0u);
    EXPECT_EQ(projected.GetColumn(1).Offset(), projected.GetColumn(0).StorageSize());

    EXPECT_EQ(projected.InlinedStorageSize(), projected.GetColumn(0).StorageSize() + projected.GetColumn(1).StorageSize());

    EXPECT_EQ(schema.ColumnCount(), 3u);
    EXPECT_TRUE(schema.HasColumn("c1"));
}

TEST(SchemaTest, ProjectOutOfRangeThrows)
{
    Schema schema({Column("a", TypeId::INTEGER), Column("b", TypeId::INTEGER)});
    const std::array<uint32_t, 2> bad_indices{0u, 99u};
    EXPECT_THROW(static_cast<void>(schema.Project(bad_indices)), std::out_of_range);
}
