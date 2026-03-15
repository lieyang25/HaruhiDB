#pragma once

#include "catalog/schema.h"
#include "storage/record/tuple.h"
#include "type/value.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace HaruhiDB
{
namespace record
{

enum class TupleCodecErrCode : int {
    ValueCountMismatch = 1,
    NullValueUnsupported,
    TypeMismatch,
    UnsupportedType,
    VarLenTooLong,
    TupleTooLarge,
    TupleTooShort,
    TupleCorrupted,
    ColumnIndexOutOfRange
};

struct TupleCodecErr {
    std::string msg;
    TupleCodecErrCode err_code;
};

class TupleCodec
{
public:
    static std::expected<Tuple, TupleCodecErr> Encode(
        const catalog::Schema& schema,
        std::span<const type::Value> values);

    static std::expected<std::vector<type::Value>, TupleCodecErr> Decode(
        const catalog::Schema& schema,
        const Tuple& tuple);

    static std::expected<type::Value, TupleCodecErr> DecodeAt(
        const catalog::Schema& schema,
        const Tuple& tuple,
        size_t column_index);

private:
    struct VarLenSlot {
        uint16_t offset;
        uint16_t length;
    };

    static_assert(sizeof(VarLenSlot) == catalog::Column::VARLEN_SLOT_SIZE);
};

} // namespace record
} // namespace HaruhiDB

