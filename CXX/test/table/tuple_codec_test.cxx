/**
 * CXX/test/table/tuple_codec_test.cxx
 */

#include "gtest/gtest.h"

#include "catalog/column.h"
#include "catalog/schema.h"
#include "storage/record/tuple_codec.h"
#include "type/type.h"
#include "type/value.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace HaruhiDB::record
{

namespace
{
    catalog::Schema MakeCodecSchema()
    {
        auto schema_exp = catalog::Schema::Create({
            catalog::Column("id", type::TypeId::INTEGER, false),
            catalog::Column("name", type::TypeId::VARCHAR, 8, true),
            catalog::Column("score", type::TypeId::DOUBLE, true),
        });
        EXPECT_TRUE(schema_exp.has_value());
        return std::move(schema_exp.value());
    }
} // namespace

TEST(TupleCodecTest, EncodeDecodeRoundtrip)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int16(7),
        type::Value::VarChar("haruhi"),
        type::Value::Double(9.25),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_TRUE(tuple_exp.has_value()) << tuple_exp.error().msg;

    auto decoded_exp = TupleCodec::Decode(schema, tuple_exp.value());
    ASSERT_TRUE(decoded_exp.has_value()) << decoded_exp.error().msg;
    ASSERT_EQ(decoded_exp->size(), values.size());

    EXPECT_EQ((*decoded_exp)[0], type::Value::Int32(7));
    EXPECT_EQ((*decoded_exp)[1], type::Value::VarChar("haruhi"));
    EXPECT_EQ((*decoded_exp)[2], type::Value::Double(9.25));

    const Tuple& tuple = tuple_exp.value();
    const uint32_t inline_size = schema.InlinedStorageSize();
    EXPECT_EQ(tuple.Size(), inline_size + 6u);

    struct VarLenSlot {
        uint16_t offset;
        uint16_t length;
    };
    VarLenSlot slot{};
    const auto& name_col = schema.GetColumn(1);
    std::memcpy(&slot, tuple.Data() + name_col.Offset(), sizeof(slot));
    EXPECT_EQ(slot.offset, inline_size);
    EXPECT_EQ(slot.length, 6u);
}

TEST(TupleCodecTest, RejectNullValue)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(10),
        type::Value::Null(),
        type::Value::Null(),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_FALSE(tuple_exp.has_value());
    EXPECT_EQ(tuple_exp.error().err_code, TupleCodecErrCode::NullValueUnsupported);
}

TEST(TupleCodecTest, RejectValueCountMismatch)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(1),
        type::Value::VarChar("x"),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_FALSE(tuple_exp.has_value());
    EXPECT_EQ(tuple_exp.error().err_code, TupleCodecErrCode::ValueCountMismatch);
}

TEST(TupleCodecTest, RejectVarCharTooLong)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(1),
        type::Value::VarChar("too_long_text"),
        type::Value::Double(2.0),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_FALSE(tuple_exp.has_value());
    EXPECT_EQ(tuple_exp.error().err_code, TupleCodecErrCode::VarLenTooLong);
}

TEST(TupleCodecTest, DecodeRejectsTruncatedTuple)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(11),
        type::Value::VarChar("abc"),
        type::Value::Double(6.0),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_TRUE(tuple_exp.has_value()) << tuple_exp.error().msg;
    const Tuple& tuple = tuple_exp.value();
    ASSERT_GT(tuple.Size(), 1u);

    std::vector<std::byte> truncated(tuple.Data(), tuple.Data() + tuple.Size() - 1);
    Tuple bad_tuple(std::move(truncated));

    auto decoded_exp = TupleCodec::Decode(schema, bad_tuple);
    ASSERT_FALSE(decoded_exp.has_value());
    EXPECT_TRUE(
        decoded_exp.error().err_code == TupleCodecErrCode::TupleTooShort ||
        decoded_exp.error().err_code == TupleCodecErrCode::TupleCorrupted);
}

TEST(TupleCodecTest, DecodeRejectsBrokenVarLenSlot)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(12),
        type::Value::VarChar("abc"),
        type::Value::Double(1.5),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_TRUE(tuple_exp.has_value()) << tuple_exp.error().msg;

    Tuple tampered = tuple_exp.value();
    struct VarLenSlot {
        uint16_t offset;
        uint16_t length;
    };
    const auto& name_col = schema.GetColumn(1);
    VarLenSlot broken_slot{
        .offset = 65000,
        .length = 3,
    };
    std::memcpy(tampered.Data() + name_col.Offset(), &broken_slot, sizeof(broken_slot));

    auto decoded_exp = TupleCodec::Decode(schema, tampered);
    ASSERT_FALSE(decoded_exp.has_value());
    EXPECT_TRUE(
        decoded_exp.error().err_code == TupleCodecErrCode::TupleTooShort ||
        decoded_exp.error().err_code == TupleCodecErrCode::TupleCorrupted);
}

TEST(TupleCodecTest, DecodeAtWorks)
{
    const auto schema = MakeCodecSchema();
    const std::vector<type::Value> values{
        type::Value::Int32(99),
        type::Value::VarChar("mio"),
        type::Value::Double(3.14),
    };

    auto tuple_exp = TupleCodec::Encode(schema, values);
    ASSERT_TRUE(tuple_exp.has_value()) << tuple_exp.error().msg;

    auto value_exp = TupleCodec::DecodeAt(schema, tuple_exp.value(), 1);
    ASSERT_TRUE(value_exp.has_value()) << value_exp.error().msg;
    EXPECT_EQ(value_exp.value(), type::Value::VarChar("mio"));
}

} // namespace HaruhiDB::record
