/**
 * CXX/test/type/type_test.cxx
 */
#include <gtest/gtest.h>

#include "type/type.h"

using namespace HaruhiDB::type;

/* ---------------------------------------------------------
 * IsValid
 * --------------------------------------------------------- */

TEST(TypeUtilTest, IsValid) {
    EXPECT_FALSE(TypeUtil::IsValid(TypeId::INVALID));

    EXPECT_TRUE(TypeUtil::IsValid(TypeId::BOOLEAN));
    EXPECT_TRUE(TypeUtil::IsValid(TypeId::INTEGER));
    EXPECT_TRUE(TypeUtil::IsValid(TypeId::VARCHAR));
}

/* ---------------------------------------------------------
 * Variable length
 * --------------------------------------------------------- */

TEST(TypeUtilTest, IsVariableLength) {
    EXPECT_TRUE(TypeUtil::IsVariableLength(TypeId::VARCHAR));

    EXPECT_FALSE(TypeUtil::IsVariableLength(TypeId::INTEGER));
    EXPECT_FALSE(TypeUtil::IsVariableLength(TypeId::DOUBLE));
}

/* ---------------------------------------------------------
 * Integral
 * --------------------------------------------------------- */

TEST(TypeUtilTest, IsIntegral) {
    EXPECT_TRUE(TypeUtil::IsIntegral(TypeId::BOOLEAN));
    EXPECT_TRUE(TypeUtil::IsIntegral(TypeId::TINYINT));
    EXPECT_TRUE(TypeUtil::IsIntegral(TypeId::SMALLINT));
    EXPECT_TRUE(TypeUtil::IsIntegral(TypeId::INTEGER));
    EXPECT_TRUE(TypeUtil::IsIntegral(TypeId::BIGINT));

    EXPECT_FALSE(TypeUtil::IsIntegral(TypeId::FLOAT));
    EXPECT_FALSE(TypeUtil::IsIntegral(TypeId::VARCHAR));
}

/* ---------------------------------------------------------
 * Floating point
 * --------------------------------------------------------- */

TEST(TypeUtilTest, IsFloatingPoint) {
    EXPECT_TRUE(TypeUtil::IsFloatingPoint(TypeId::FLOAT));
    EXPECT_TRUE(TypeUtil::IsFloatingPoint(TypeId::DOUBLE));
    EXPECT_TRUE(TypeUtil::IsFloatingPoint(TypeId::DECIMAL));

    EXPECT_FALSE(TypeUtil::IsFloatingPoint(TypeId::INTEGER));
    EXPECT_FALSE(TypeUtil::IsFloatingPoint(TypeId::VARCHAR));
}

/* ---------------------------------------------------------
 * Numeric
 * --------------------------------------------------------- */

TEST(TypeUtilTest, IsNumeric) {
    EXPECT_TRUE(TypeUtil::IsNumeric(TypeId::INTEGER));
    EXPECT_TRUE(TypeUtil::IsNumeric(TypeId::BIGINT));
    EXPECT_TRUE(TypeUtil::IsNumeric(TypeId::FLOAT));
    EXPECT_TRUE(TypeUtil::IsNumeric(TypeId::DOUBLE));

    EXPECT_FALSE(TypeUtil::IsNumeric(TypeId::VARCHAR));
}

/* ---------------------------------------------------------
 * FixedLengthSize
 * --------------------------------------------------------- */

TEST(TypeUtilTest, FixedLengthSize) {
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::BOOLEAN), 1);
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::TINYINT), 1);
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::SMALLINT), 2);
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::INTEGER), 4);
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::BIGINT), 8);

    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::FLOAT), 4);
    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::DOUBLE), 8);

    EXPECT_EQ(TypeUtil::FixedLengthSize(TypeId::VARCHAR),
              TypeUtil::VARIABLE_LENGTH);
}

/* ---------------------------------------------------------
 * TypeName
 * --------------------------------------------------------- */

TEST(TypeUtilTest, TypeName) {
    EXPECT_EQ(TypeUtil::TypeName(TypeId::BOOLEAN), "BOOLEAN");
    EXPECT_EQ(TypeUtil::TypeName(TypeId::INTEGER), "INTEGER");
    EXPECT_EQ(TypeUtil::TypeName(TypeId::DOUBLE), "DOUBLE");
    EXPECT_EQ(TypeUtil::TypeName(TypeId::VARCHAR), "VARCHAR");
}

/* ---------------------------------------------------------
 * ParseType basic
 * --------------------------------------------------------- */

TEST(TypeUtilTest, ParseTypeBasic) {
    EXPECT_EQ(TypeUtil::ParseType("boolean"), TypeId::BOOLEAN);
    EXPECT_EQ(TypeUtil::ParseType("tinyint"), TypeId::TINYINT);
    EXPECT_EQ(TypeUtil::ParseType("smallint"), TypeId::SMALLINT);
    EXPECT_EQ(TypeUtil::ParseType("integer"), TypeId::INTEGER);
    EXPECT_EQ(TypeUtil::ParseType("bigint"), TypeId::BIGINT);

    EXPECT_EQ(TypeUtil::ParseType("float"), TypeId::FLOAT);
    EXPECT_EQ(TypeUtil::ParseType("double"), TypeId::DOUBLE);

    EXPECT_EQ(TypeUtil::ParseType("varchar"), TypeId::VARCHAR);
}

/* ---------------------------------------------------------
 * ParseType aliases
 * --------------------------------------------------------- */

TEST(TypeUtilTest, ParseTypeAliases) {
    EXPECT_EQ(TypeUtil::ParseType("bool"), TypeId::BOOLEAN);

    EXPECT_EQ(TypeUtil::ParseType("int8"), TypeId::TINYINT);
    EXPECT_EQ(TypeUtil::ParseType("int16"), TypeId::SMALLINT);
    EXPECT_EQ(TypeUtil::ParseType("int32"), TypeId::INTEGER);
    EXPECT_EQ(TypeUtil::ParseType("int64"), TypeId::BIGINT);

    EXPECT_EQ(TypeUtil::ParseType("string"), TypeId::VARCHAR);
    EXPECT_EQ(TypeUtil::ParseType("text"), TypeId::VARCHAR);
}

/* ---------------------------------------------------------
 * ParseType case-insensitive
 * --------------------------------------------------------- */

TEST(TypeUtilTest, ParseTypeCaseInsensitive) {
    EXPECT_EQ(TypeUtil::ParseType("INTEGER"), TypeId::INTEGER);
    EXPECT_EQ(TypeUtil::ParseType("IntEgEr"), TypeId::INTEGER);
    EXPECT_EQ(TypeUtil::ParseType("VaRcHaR"), TypeId::VARCHAR);
}

/* ---------------------------------------------------------
 * ParseType invalid
 * --------------------------------------------------------- */

TEST(TypeUtilTest, ParseTypeInvalid) {
    EXPECT_EQ(TypeUtil::ParseType("unknown"), std::nullopt);
    EXPECT_EQ(TypeUtil::ParseType(""), std::nullopt);
    EXPECT_EQ(TypeUtil::ParseType("123"), std::nullopt);
}