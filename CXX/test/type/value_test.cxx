/**
 * CXX/test/type/value.cxx
 */

#include "gtest/gtest.h"

#include "type/value.h"
#include "type/type.h"

#include <vector>
#include <cstring>
#include <optional>
#include <array>

using namespace HaruhiDB::type;

TEST(ValueTest, NullAndType) {
    Value n = Value::Null();
    EXPECT_TRUE(n.IsNull());
    EXPECT_EQ(n.Type(), TypeId::INVALID);

    Value b = Value::Boolean(true);
    EXPECT_FALSE(b.IsNull());
    EXPECT_EQ(b.Type(), TypeId::BOOLEAN);
}

TEST(ValueTest, ConstructorsAndTryAsAndToString) {
    Value i32 = Value::Int32(123);
    EXPECT_FALSE(i32.IsNull());
    EXPECT_EQ(i32.Type(), TypeId::INTEGER);
    auto p32 = i32.TryAs<int32_t>();
    ASSERT_NE(p32, nullptr);
    EXPECT_EQ(*p32, 123);

    // ToString for integer should be decimal representation
    std::string s = i32.ToString();
    EXPECT_EQ(s, std::to_string(123));

    Value s1 = Value::VarChar(std::string("hello"));
    EXPECT_EQ(s1.Type(), TypeId::VARCHAR);
    auto sp = s1.TryAs<std::string>();
    ASSERT_NE(sp, nullptr);
    EXPECT_EQ(*sp, "hello");
    EXPECT_EQ(s1.ToString(), "hello");
}

TEST(ValueTest, SerializeDeserializeRoundtrip_Integer) {
    Value v = Value::Int32(2024);
    auto buf = v.Serialize(TypeId::INTEGER);
    ASSERT_FALSE(buf.empty());
    Value v2 = Value::Deserialize(TypeId::INTEGER, buf.data(), buf.size());
    EXPECT_EQ(v, v2);

    Value v64 = Value::Int64(1LL << 40);
    auto b64 = v64.Serialize(TypeId::BIGINT);
    ASSERT_FALSE(b64.empty());
    Value v64r = Value::Deserialize(TypeId::BIGINT, b64.data(), b64.size());
    EXPECT_EQ(v64, v64r);
}

TEST(ValueTest, SerializeDeserializeRoundtrip_FloatDouble) {
    Value f = Value::Float(3.1415f);
    auto bf = f.Serialize(TypeId::FLOAT);
    ASSERT_FALSE(bf.empty());
    Value fr = Value::Deserialize(TypeId::FLOAT, bf.data(), bf.size());
    // compare floats by AsLongDouble to avoid textual formatting issues
    auto of = f.AsLongDouble();
    auto orv = fr.AsLongDouble();
    ASSERT_TRUE(of.has_value());
    ASSERT_TRUE(orv.has_value());
    EXPECT_NEAR(static_cast<double>(*of), static_cast<double>(*orv), 1e-6);

    Value d = Value::Double(2.718281828);
    auto bd = d.Serialize(TypeId::DOUBLE);
    ASSERT_FALSE(bd.empty());
    Value dr = Value::Deserialize(TypeId::DOUBLE, bd.data(), bd.size());
    auto od = d.AsLongDouble();
    auto odr = dr.AsLongDouble();
    ASSERT_TRUE(od.has_value());
    ASSERT_TRUE(odr.has_value());
    EXPECT_NEAR((double)*od, (double)*odr, 1e-12);
}

TEST(ValueTest, SerializeDeserializeRoundtrip_VarChar) {
    std::string text = "HaruhiDB test string";
    Value s = Value::VarChar(text);
    auto bs = s.Serialize(TypeId::VARCHAR);
    ASSERT_FALSE(bs.empty());
    Value sr = Value::Deserialize(TypeId::VARCHAR, bs.data(), bs.size());
    EXPECT_EQ(s, sr);
    // also check ToString remains the same
    EXPECT_EQ(sr.ToString(), text);
}

TEST(ValueTest, TryDeserializeSeparatesNullFromCorruption) {
    auto null_value = Value::TryDeserialize(TypeId::INTEGER, nullptr, 0);
    ASSERT_TRUE(null_value.has_value());
    EXPECT_TRUE(null_value.value().IsNull());

    auto null_ptr_err = Value::TryDeserialize(TypeId::INTEGER, nullptr, sizeof(int32_t));
    ASSERT_FALSE(null_ptr_err.has_value());
    EXPECT_EQ(null_ptr_err.error().err_code, ValueDeserializeErrCode::NullPointerWithNonZeroLength);

    std::array<std::byte, 1> short_buf{std::byte{0x01}};
    auto short_len_err = Value::TryDeserialize(TypeId::INTEGER, short_buf.data(), short_buf.size());
    ASSERT_FALSE(short_len_err.has_value());
    EXPECT_EQ(short_len_err.error().err_code, ValueDeserializeErrCode::BufferTooShort);
}

TEST(ValueTest, DeserializeThrowsWhenInputIsCorrupted) {
    EXPECT_THROW(Value::Deserialize(TypeId::INTEGER, nullptr, sizeof(int32_t)), std::invalid_argument);
}

TEST(ValueTest, DecimalIsExplicitlyUnsupportedForNow) {
    Value v = Value::Double(12.34);
    EXPECT_FALSE(v.CanCastTo(TypeId::DECIMAL));
    EXPECT_THROW(v.Serialize(TypeId::DECIMAL), std::invalid_argument);

    std::array<std::byte, sizeof(double)> buf{};
    auto parsed = Value::TryDeserialize(TypeId::DECIMAL, buf.data(), buf.size());
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().err_code, ValueDeserializeErrCode::InvalidType);
}

TEST(ValueTest, CompareOrdering) {
    Value a = Value::Int32(1);
    Value b = Value::Int32(2);
    auto r = a.Compare(b);
    EXPECT_EQ(r, std::partial_ordering::less);
    EXPECT_EQ(b.Compare(a), std::partial_ordering::greater);
    EXPECT_EQ(a.Compare(a), std::partial_ordering::equivalent);

    Value sa = Value::VarChar(std::string("abc"));
    Value sb = Value::VarChar(std::string("abd"));
    EXPECT_EQ(sa.Compare(sb), std::partial_ordering::less);
    EXPECT_EQ(sb.Compare(sa), std::partial_ordering::greater);
    EXPECT_EQ(sa.Compare(sa), std::partial_ordering::equivalent);
}

TEST(ValueTest, CanCastToAndAsLongDouble) {
    Value i = Value::Int32(42);
    // integers usually castable to larger numeric kinds
    EXPECT_TRUE(i.CanCastTo(TypeId::INTEGER));
    EXPECT_TRUE(i.CanCastTo(TypeId::BIGINT));
    EXPECT_TRUE(i.CanCastTo(TypeId::FLOAT));
    EXPECT_TRUE(i.CanCastTo(TypeId::DOUBLE));

    auto ld = i.AsLongDouble();
    ASSERT_TRUE(ld.has_value());
    EXPECT_DOUBLE_EQ(static_cast<double>(*ld), 42.0);

    Value str = Value::VarChar(std::string("not_numeric"));
    EXPECT_TRUE(str.CanCastTo(TypeId::VARCHAR));
    EXPECT_FALSE(str.AsLongDouble().has_value());
}

TEST(ValueTest, EqualityAndNullEquality) {
    Value n1 = Value::Null();
    Value n2 = Value::Null();
    EXPECT_EQ(n1, n2);

    Value x = Value::Int32(100);
    Value y = Value::Int32(100);
    Value z = Value::Int32(101);
    EXPECT_EQ(x, y);
    EXPECT_FALSE(x == z);
}

TEST(ValueTest, NumericCrossTypeCompare) {

    Value i32 = Value::Int32(10);
    Value i64 = Value::Int64(20);
    Value f   = Value::Float(10.0f);
    Value d   = Value::Double(30.0);

    EXPECT_EQ(i32.Compare(i64), std::partial_ordering::less);
    EXPECT_EQ(i64.Compare(i32), std::partial_ordering::greater);

    EXPECT_EQ(i32.Compare(f), std::partial_ordering::equivalent);
    EXPECT_EQ(f.Compare(i32), std::partial_ordering::equivalent);

    EXPECT_EQ(d.Compare(i64), std::partial_ordering::greater);
}

TEST(ValueTest, SerializeRoundTripAllTypes) {

    std::vector<Value> values = {
        Value::Boolean(true),
        Value::Int8(1),
        Value::Int16(2),
        Value::Int32(3),
        Value::Int64(4),
        Value::Float(5.5f),
        Value::Double(6.6),
        Value::VarChar("haruhi")
    };

    for (const auto &v : values) {

        auto type = v.Type();

        auto buf = v.Serialize(type);

        Value r = Value::Deserialize(type, buf.data(), buf.size());

        EXPECT_EQ(v, r);
    }
}

TEST(ValueTest, AsLongDoubleAllNumeric) {

    std::vector<Value> nums = {
        Value::Int8(1),
        Value::Int16(2),
        Value::Int32(3),
        Value::Int64(4),
        Value::Float(5.5f),
        Value::Double(6.6)
    };

    for (auto &v : nums) {

        auto r = v.AsLongDouble();

        ASSERT_TRUE(r.has_value());
    }
}

TEST(ValueTest, NullBehavior) {

    Value n = Value::Null();
    Value i = Value::Int32(10);

    EXPECT_TRUE(n.IsNull());

    EXPECT_EQ(n.Compare(i), std::partial_ordering::unordered);
    EXPECT_EQ(i.Compare(n), std::partial_ordering::unordered);
}

TEST(ValueTest, InvalidCrossTypeCompare) {

    Value str = Value::VarChar("123");
    Value num = Value::Int32(123);

    EXPECT_EQ(str.Compare(num), std::partial_ordering::unordered);
}
