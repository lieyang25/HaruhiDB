#include "storage/record/tuple_codec.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace record
{
    namespace
    {
        std::unexpected<TupleCodecErr> MakeErr(TupleCodecErrCode code, std::string msg)
        {
            return std::unexpected(TupleCodecErr{
                .msg = std::move(msg),
                .err_code = code,
            });
        }

        bool CheckColumnBounds(const catalog::Column& column, uint32_t inlined_size)
        {
            const uint64_t begin = column.Offset();
            const uint64_t end = begin + column.StorageSize();
            return end <= inlined_size;
        }

        std::vector<std::byte> SerializeOrEmpty(
            const type::Value& value,
            type::TypeId type_id,
            TupleCodecErr* err_out)
        {
            try {
                return value.Serialize(type_id);
            } catch (const std::exception& e) {
                if (err_out != nullptr) {
                    *err_out = TupleCodecErr{
                        .msg = std::string("TupleCodec::Encode: serialize failed: ") + e.what(),
                        .err_code = TupleCodecErrCode::UnsupportedType,
                    };
                }
                return {};
            }
        }
    } // namespace

    std::expected<Tuple, TupleCodecErr> TupleCodec::Encode(
        const catalog::Schema& schema,
        std::span<const type::Value> values)
    {
        const size_t column_count = schema.ColumnCount();
        if (values.size() != column_count) {
            return MakeErr(
                TupleCodecErrCode::ValueCountMismatch,
                "TupleCodec::Encode: values count does not match schema");
        }

        const uint32_t inlined_size = schema.InlinedStorageSize();
        std::vector<std::byte> bytes(inlined_size, std::byte{0});
        size_t payload_offset = inlined_size;

        for (size_t i = 0; i < column_count; ++i) {
            const auto& column = schema.GetColumn(i);
            if (!CheckColumnBounds(column, inlined_size)) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Encode: schema column offset/storage is out of inlined bounds");
            }

            const auto& value = values[i];
            if (value.IsNull()) {
                // Column/Schema tuple layout does not define per-field null bitmap or sentinel.
                return MakeErr(
                    TupleCodecErrCode::NullValueUnsupported,
                    "TupleCodec::Encode: NULL is not supported in schema-style tuple layout");
            }
            if (!value.CanCastTo(column.Type())) {
                return MakeErr(
                    TupleCodecErrCode::TypeMismatch,
                    "TupleCodec::Encode: value cannot cast to column type");
            }

            TupleCodecErr serialize_err{};
            auto serialized = SerializeOrEmpty(value, column.Type(), &serialize_err);
            if (!serialize_err.msg.empty()) {
                return std::unexpected(std::move(serialize_err));
            }

            const size_t field_offset = column.Offset();
            if (column.IsInlined()) {
                const size_t fixed_len = column.StorageSize();
                if (serialized.size() != fixed_len) {
                    return MakeErr(
                        TupleCodecErrCode::TupleCorrupted,
                        "TupleCodec::Encode: fixed column serialized size mismatch");
                }
                std::memcpy(bytes.data() + field_offset, serialized.data(), fixed_len);
                continue;
            }

            if (serialized.size() > column.Length()) {
                return MakeErr(
                    TupleCodecErrCode::VarLenTooLong,
                    "TupleCodec::Encode: variable-length value exceeds column length");
            }
            if (serialized.size() > std::numeric_limits<uint16_t>::max()) {
                return MakeErr(
                    TupleCodecErrCode::TupleTooLarge,
                    "TupleCodec::Encode: variable-length payload exceeds uint16 range");
            }
            if (payload_offset > std::numeric_limits<uint16_t>::max() ||
                payload_offset + serialized.size() > std::numeric_limits<uint16_t>::max()) {
                return MakeErr(
                    TupleCodecErrCode::TupleTooLarge,
                    "TupleCodec::Encode: tuple payload offset exceeds uint16 range");
            }

            bytes.insert(bytes.end(), serialized.begin(), serialized.end());
            VarLenSlot slot{
                .offset = static_cast<uint16_t>(payload_offset),
                .length = static_cast<uint16_t>(serialized.size()),
            };
            std::memcpy(bytes.data() + field_offset, &slot, sizeof(slot));
            payload_offset += serialized.size();
        }

        if (bytes.size() > std::numeric_limits<uint16_t>::max()) {
            return MakeErr(
                TupleCodecErrCode::TupleTooLarge,
                "TupleCodec::Encode: encoded tuple exceeds uint16 size");
        }
        return Tuple(std::move(bytes));
    }

    std::expected<std::vector<type::Value>, TupleCodecErr> TupleCodec::Decode(
        const catalog::Schema& schema,
        const Tuple& tuple)
    {
        const auto* data = tuple.Data();
        const size_t tuple_size = tuple.Size();
        const uint32_t inlined_size = schema.InlinedStorageSize();

        if (data == nullptr && tuple_size != 0) {
            return MakeErr(
                TupleCodecErrCode::TupleCorrupted,
                "TupleCodec::Decode: tuple data pointer is null");
        }
        if (tuple_size < inlined_size) {
            return MakeErr(
                TupleCodecErrCode::TupleTooShort,
                "TupleCodec::Decode: tuple bytes shorter than schema inlined storage size");
        }

        std::vector<type::Value> values;
        values.reserve(schema.ColumnCount());

        for (size_t i = 0; i < schema.ColumnCount(); ++i) {
            const auto& column = schema.GetColumn(i);
            if (!CheckColumnBounds(column, inlined_size)) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Decode: schema column offset/storage is out of inlined bounds");
            }

            const size_t field_offset = column.Offset();
            if (column.IsInlined()) {
                const size_t fixed_len = column.StorageSize();
                auto parsed = type::Value::TryDeserialize(column.Type(), data + field_offset, fixed_len);
                if (!parsed.has_value()) {
                    return MakeErr(
                        TupleCodecErrCode::TupleCorrupted,
                        "TupleCodec::Decode: failed to parse fixed-length value");
                }
                if (parsed->IsNull()) {
                    return MakeErr(
                        TupleCodecErrCode::TupleCorrupted,
                        "TupleCodec::Decode: fixed-length field decoded as NULL");
                }
                values.push_back(std::move(parsed.value()));
                continue;
            }

            VarLenSlot slot{};
            std::memcpy(&slot, data + field_offset, sizeof(slot));
            if (slot.length > column.Length()) {
                return MakeErr(
                    TupleCodecErrCode::VarLenTooLong,
                    "TupleCodec::Decode: variable-length slot length exceeds column length");
            }

            const size_t payload_offset = slot.offset;
            const size_t payload_len = slot.length;
            if (payload_len > 0 && payload_offset < inlined_size) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Decode: variable-length slot points into inlined region");
            }
            if (payload_offset + payload_len > tuple_size) {
                return MakeErr(
                    TupleCodecErrCode::TupleTooShort,
                    "TupleCodec::Decode: variable-length payload overflows tuple bytes");
            }

            const std::byte* payload_ptr = payload_len == 0 ? nullptr : data + payload_offset;
            auto parsed = type::Value::TryDeserialize(column.Type(), payload_ptr, payload_len);
            if (!parsed.has_value()) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Decode: failed to parse variable-length value");
            }
            if (parsed->IsNull()) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Decode: variable-length field decoded as NULL");
            }
            values.push_back(std::move(parsed.value()));
        }
        return values;
    }

    std::expected<type::Value, TupleCodecErr> TupleCodec::DecodeAt(
        const catalog::Schema& schema,
        const Tuple& tuple,
        size_t column_index)
    {
        if (column_index >= schema.ColumnCount()) {
            return MakeErr(
                TupleCodecErrCode::ColumnIndexOutOfRange,
                "TupleCodec::DecodeAt: column index out of range");
        }
        auto decoded = Decode(schema, tuple);
        if (!decoded.has_value()) {
            return std::unexpected(decoded.error());
        }
        return decoded->at(column_index);
    }

} // namespace record
} // namespace HaruhiDB
