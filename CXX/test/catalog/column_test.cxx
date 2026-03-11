/**
 * CXX/test/catalog/column_test.cxx
 */

#include "gtest/gtest.h"

#include "catalog/column.h"
#include "type/type.h"
#include "type/value.h"

#include <string>
#include <vector>

using namespace HaruhiDB::catalog;
using namespace HaruhiDB::type;

namespace
{
    std::vector<TypeId> FixedLengthTypes()
    {
        return {
            TypeId::BOOLEAN, TypeId::TINYINT, TypeId::SMALLINT, TypeId::INTEGER, TypeId::BIGINT,
            TypeId::FLOAT,   TypeId::DOUBLE,  TypeId::DECIMAL,
        };
    }
} // namespace

TEST(ColumnTest, FixedLengthConstructorSetsProperties)
{
    for (TypeId type : FixedLengthTypes()) {
        Column column("fixed_col", type, false);
        EXPECT_EQ(column.Name(), "fixed_col");
        EXPECT_EQ(column.Type(), type);
        EXPECT_FALSE(column.Nullable());
        EXPECT_TRUE(column.IsInlined());
        EXPECT_FALSE(column.IsVarlen());
        EXPECT_EQ(column.Length(), static_cast<uint32_t>(TypeUtil::FixedLengthSize(type)));
        EXPECT_EQ(column.StorageSize(), column.Length());
        EXPECT_TRUE(column.Validate().has_value());

        const std::string text = column.ToString();
        EXPECT_NE(text.find("fixed_col"), std::string::npos);
        EXPECT_NE(text.find(std::string(TypeUtil::TypeName(type))), std::string::npos);
        EXPECT_NE(text.find("NOT NULL"), std::string::npos);
    }
}

TEST(ColumnTest, FirstConstructorRejectsVariableLengthOrInvalidType)
{
    EXPECT_THROW(Column("v", TypeId::VARCHAR), std::invalid_argument);
    EXPECT_THROW(Column("bad", TypeId::INVALID), std::invalid_argument);
}

TEST(ColumnTest, ExplicitLengthConstructorForVariableLengthType)
{
    Column varchar_col("name", TypeId::VARCHAR, 64, true);
    EXPECT_EQ(varchar_col.Name(), "name");
    EXPECT_EQ(varchar_col.Type(), TypeId::VARCHAR);
    EXPECT_EQ(varchar_col.Length(), 64u);
    EXPECT_TRUE(varchar_col.Nullable());
    EXPECT_FALSE(varchar_col.IsInlined());
    EXPECT_TRUE(varchar_col.IsVarlen());
    EXPECT_EQ(varchar_col.StorageSize(), Column::VARLEN_SLOT_SIZE);
    EXPECT_TRUE(varchar_col.Validate().has_value());
}

TEST(ColumnTest, ExplicitLengthConstructorChecksFixedLengthSize)
{
    EXPECT_NO_THROW({
        Column int_col("id", TypeId::INTEGER, 4, false);
        EXPECT_TRUE(int_col.IsInlined());
        EXPECT_EQ(int_col.StorageSize(), 4u);
    });

    EXPECT_THROW(Column("bad_int", TypeId::INTEGER, 8, false), std::invalid_argument);
    EXPECT_THROW(Column("empty_text", TypeId::VARCHAR, 0, true), std::invalid_argument);
}

TEST(ColumnTest, DefaultValueCompatibilityAndNullability)
{
    EXPECT_NO_THROW({
        Column numeric("age", TypeId::INTEGER, false, Value::Int16(18));
        EXPECT_TRUE(numeric.HasDefaultValue());
        EXPECT_TRUE(numeric.Validate().has_value());
    });

    EXPECT_THROW(Column("age_bad", TypeId::INTEGER, false, Value::VarChar("18")), std::invalid_argument);
    EXPECT_THROW(Column("not_null_with_null_default", TypeId::INTEGER, false, Value::Null()), std::invalid_argument);
}

TEST(ColumnTest, VarCharDefaultValueLengthAndCastingRules)
{
    EXPECT_NO_THROW({
        Column from_number("code", TypeId::VARCHAR, 4, false, Value::Int32(7));
        EXPECT_TRUE(from_number.HasDefaultValue());
        EXPECT_TRUE(from_number.Validate().has_value());
        EXPECT_NE(from_number.ToString().find("DEFAULT"), std::string::npos);
    });

    EXPECT_NO_THROW(Column("short_ok", TypeId::VARCHAR, 4, true, Value::VarChar("abcd")));
    EXPECT_THROW(Column("too_long", TypeId::VARCHAR, 3, true, Value::VarChar("abcd")), std::invalid_argument);
}

TEST(ColumnTest, SetOffsetOnlyChangesOffset)
{
    Column c("offset_col", TypeId::BIGINT);
    const auto storage_size = c.StorageSize();

    EXPECT_EQ(c.Offset(), 0u);
    c.SetOffset(128u);
    EXPECT_EQ(c.Offset(), 128u);
    EXPECT_EQ(c.StorageSize(), storage_size);
    EXPECT_EQ(c.Length(), static_cast<uint32_t>(TypeUtil::FixedLengthSize(TypeId::BIGINT)));
}
