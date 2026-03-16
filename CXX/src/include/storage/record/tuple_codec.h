/**
 * CXX/src/include/storage/record/tuple_codec.h
 *
 * ========================= 设计目标 =========================
 *
 * TupleCodec 负责在“列值序列”和“存储层 Tuple 字节串”之间做编解码转换。
 *
 * 它建立在 Schema 之上，
 * 按照列定义把一组 Value 编码成 Tuple，
 * 或把 Tuple 解码回一组 Value。
 *
 * 核心职责：
 *
 * 1. 按 Schema 编码一行记录
 * 2. 按 Schema 解码一行记录
 * 3. 解码指定列
 * 4. 校验变长字段 slot 与 payload 的合法性
 *
 *
 * ========================= 为什么需要 TupleCodec =========================
 *
 * Schema 只描述：
 *
 * - 每列类型
 * - 每列 offset
 * - 每列是否变长
 *
 * Tuple 只保存：
 *
 * - 一整段原始字节
 *
 * 两者之间仍然需要一个中间层，
 * 把“逻辑列值”转成“物理字节布局”，
 * 或反过来解析。
 *
 *
 * ========================= TupleCodec 在系统中的位置 =========================
 *
 * Schema + Value[]
 *        │
 *        ▼
 *    TupleCodec
 *        │
 *        ▼
 *      Tuple
 *
 * Decode 时方向相反：
 *
 *      Tuple
 *        │
 *        ▼
 *    TupleCodec
 *        │
 *        ▼
 *   Value[]
 *
 *
 * ========================= 当前编码布局 =========================
 *
 * Tuple bytes
 *
 *   +-----------------------------+
 *   | inlined fields              |
 *   |   - fixed columns           |
 *   |   - varlen slots            |
 *   +-----------------------------+
 *   | payload of varlen columns   |
 *   +-----------------------------+
 *
 * 其中：
 *
 * - 固定长度列直接写入 inline 区
 * - 变长列在 inline 区写入 VarLenSlot
 * - VarLenSlot 记录 payload 的 offset 与 length
 */

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

    /**
     * TupleCodec 相关错误。
     */
    struct TupleCodecErr
    {
        std::string msg;
        TupleCodecErrCode err_code;
    };

    class TupleCodec
    {
    public:
        /**
         * 按 Schema 将一组 Value 编码为 Tuple。
         *
         * @param schema 目标 schema
         * @param values 输入列值
         * @return 成功时返回编码后的 Tuple
         */
        static std::expected<Tuple, TupleCodecErr> Encode(
            const catalog::Schema& schema,
            std::span<const type::Value> values);

        /**
         * 按 Schema 将 Tuple 解码为整行 Value。
         *
         * @param schema 目标 schema
         * @param tuple  输入 tuple
         * @return 成功时返回解码后的值序列
         */
        static std::expected<std::vector<type::Value>, TupleCodecErr> Decode(
            const catalog::Schema& schema,
            const Tuple& tuple);

        /**
         * 按 Schema 解码指定列。
         *
         * @param schema       目标 schema
         * @param tuple        输入 tuple
         * @param column_index 目标列下标
         * @return 成功时返回该列值
         */
        static std::expected<type::Value, TupleCodecErr> DecodeAt(
            const catalog::Schema& schema,
            const Tuple& tuple,
            size_t column_index);

    private:
        /**
         * 变长列在 inline 区中的槽位结构。
         *
         * offset 指向 payload 起点，
         * length 表示 payload 长度。
         */
        struct VarLenSlot
        {
            uint16_t offset;
            uint16_t length;
        };

        static_assert(sizeof(VarLenSlot) == catalog::Column::VARLEN_SLOT_SIZE);
    };

} // namespace record
} // namespace HaruhiDB