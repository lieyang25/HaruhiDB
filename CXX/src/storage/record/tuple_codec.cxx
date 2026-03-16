/**
 * CXX/src/storage/record/tuple_codec.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 TupleCodec 的编解码逻辑。
 *
 * 主要完成：
 *
 * 1. 按 Schema 把 Value[] 编码为 Tuple
 * 2. 按 Schema 把 Tuple 解码为 Value[]
 * 3. 解码指定列
 * 4. 校验 inline 区与 varlen payload 的边界
 *
 *
 * ========================= 核心机制 =========================
 *
 * Encode:
 *   遍历每一列
 *     -> 检查类型与列数
 *     -> 固定列直接写 inline 区
 *     -> 变长列写 payload，再写 VarLenSlot
 *
 * Decode:
 *   遍历每一列
 *     -> 固定列按 offset 直接解析
 *     -> 变长列先读 VarLenSlot，再解析 payload
 *
 *
 * ========================= 当前约束 =========================
 *
 * 1. 当前 schema-style tuple layout 不支持 NULL bitmap
 * 2. 因此 Encode 不接受 NULL 值
 * 3. Decode 若解析出 NULL，会视为 tuple 损坏
 * 4. 变长列 payload 必须位于 inline 区之后
 */

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

    /**
     * @param schema 目标 schema
     * @param values 输入列值
     */
    std::expected<Tuple, TupleCodecErr> TupleCodec::Encode(
        const catalog::Schema& schema,
        std::span<const type::Value> values)
    {
        // step 1: 检查输入值数量是否与 schema 列数一致。
        const size_t column_count = schema.ColumnCount();
        if (values.size() != column_count) {
            return MakeErr(
                TupleCodecErrCode::ValueCountMismatch,
                "TupleCodec::Encode: values count does not match schema");
        }

        // step 2: 先分配 inline 区，payload 从 inline 区末尾开始追加。
        const uint32_t inlined_size = schema.InlinedStorageSize();
        std::vector<std::byte> bytes(inlined_size, std::byte{0});
        size_t payload_offset = inlined_size;

        // step 3: 按列顺序逐个编码。
        for (size_t i = 0; i < column_count; ++i) {
            const auto& column = schema.GetColumn(i);

            if (!CheckColumnBounds(column, inlined_size)) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Encode: schema column offset/storage is out of inlined bounds");
            }

            const auto& value = values[i];

            // step 3.1: 当前布局不支持 NULL。
            if (value.IsNull()) {
                return MakeErr(
                    TupleCodecErrCode::NullValueUnsupported,
                    "TupleCodec::Encode: NULL is not supported in schema-style tuple layout");
            }

            // step 3.2: 检查 Value 是否可转换到列类型。
            if (!value.CanCastTo(column.Type())) {
                return MakeErr(
                    TupleCodecErrCode::TypeMismatch,
                    "TupleCodec::Encode: value cannot cast to column type");
            }

            // step 3.3: 先序列化该列值。
            TupleCodecErr serialize_err{};
            auto serialized = SerializeOrEmpty(value, column.Type(), &serialize_err);
            if (!serialize_err.msg.empty()) {
                return std::unexpected(std::move(serialize_err));
            }

            const size_t field_offset = column.Offset();

            // step 3.4: 固定长度列直接写入 inline 区。
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

            // step 3.5: 变长列先检查长度，再把 payload 追加到 tuple 尾部。
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

        // step 4: 最终 tuple 总大小仍需落在 uint16 可表达范围内。
        if (bytes.size() > std::numeric_limits<uint16_t>::max()) {
            return MakeErr(
                TupleCodecErrCode::TupleTooLarge,
                "TupleCodec::Encode: encoded tuple exceeds uint16 size");
        }

        return Tuple(std::move(bytes));
    }

    /**
     * @param schema 目标 schema
     * @param tuple  输入 tuple
     */
    std::expected<std::vector<type::Value>, TupleCodecErr> TupleCodec::Decode(
        const catalog::Schema& schema,
        const Tuple& tuple)
    {
        // step 1: 读取 tuple 基础信息，并检查 inline 区是否完整存在。
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

        // step 2: 按列顺序逐列解码。
        for (size_t i = 0; i < schema.ColumnCount(); ++i) {
            const auto& column = schema.GetColumn(i);

            if (!CheckColumnBounds(column, inlined_size)) {
                return MakeErr(
                    TupleCodecErrCode::TupleCorrupted,
                    "TupleCodec::Decode: schema column offset/storage is out of inlined bounds");
            }

            const size_t field_offset = column.Offset();

            // step 2.1: 固定长度列直接从 inline 区读取并反序列化。
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

            // step 2.2: 变长列先读取 VarLenSlot。
            VarLenSlot slot{};
            std::memcpy(&slot, data + field_offset, sizeof(slot));

            if (slot.length > column.Length()) {
                return MakeErr(
                    TupleCodecErrCode::VarLenTooLong,
                    "TupleCodec::Decode: variable-length slot length exceeds column length");
            }

            const size_t payload_offset = slot.offset;
            const size_t payload_len = slot.length;

            // step 2.3: 检查 payload 指向的位置是否合法。
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

            // step 2.4: 解析 payload 得到最终值。
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

    /**
     * @param schema       目标 schema
     * @param tuple        输入 tuple
     * @param column_index 目标列下标
     */
    std::expected<type::Value, TupleCodecErr> TupleCodec::DecodeAt(
        const catalog::Schema& schema,
        const Tuple& tuple,
        size_t column_index)
    {
        // step 1: 先检查列下标是否越界。
        if (column_index >= schema.ColumnCount()) {
            return MakeErr(
                TupleCodecErrCode::ColumnIndexOutOfRange,
                "TupleCodec::DecodeAt: column index out of range");
        }

        // step 2: 复用整行解码逻辑。
        auto decoded = Decode(schema, tuple);
        if (!decoded.has_value()) {
            return std::unexpected(decoded.error());
        }

        // step 3: 取出目标列返回。
        return decoded->at(column_index);
    }

} // namespace record
} // namespace HaruhiDB