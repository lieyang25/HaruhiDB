/**
 * CXX/src/include/storage/record/tuple.h
 *
 * ========================= 设计目标 =========================
 *
 * Tuple 表示一条记录在存储层中的原始字节数据。
 *
 * 当前阶段中，
 * 它不解释字段语义，不负责 schema 级解码，
 * 只负责持有一段完整的 tuple bytes。
 *
 *
 * ========================= 为什么需要 Tuple =========================
 *
 * TablePage、TableHeap、BufferPoolManager 处理的核心对象之一就是记录。
 *
 * 但在存储层里，
 * 一条记录首先表现为一段连续字节，而不是带类型的列对象。
 *
 * 因此需要 Tuple 来统一表示：
 *
 * - 从页面中读取出的记录内容
 * - 待写入页面的记录内容
 * - 在页间迁移、更新时传递的记录内容
 *
 *
 * ========================= Tuple 在系统中的位置 =========================
 *
 * TableHeap
 *   ├── InsertTuple(Tuple)
 *   ├── GetTuple(...) -> Tuple
 *   └── UpdateTuple(..., Tuple)
 *
 * TablePage
 *   ├── InsertTuple(Tuple)
 *   ├── GetTuple(...) -> Tuple
 *   └── UpdateTuple(..., Tuple)
 *
 * Tuple
 *   └── raw bytes
 *
 *
 * ========================= 当前实现语义 =========================
 *
 * 当前 Tuple 采用“自拥有字节数据”设计：
 *
 * - 从 span 构造时会拷贝数据
 * - 内部使用 vector<byte> 持有内容
 *
 * 这样可以避免页面解除 pin 或解锁后出现悬垂引用问题。
 */

#pragma once

#include "common/config.h"
#include "storage/record/rid.h"

#include <span>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace record
{
    class Tuple
    {
    public:
        Tuple() = default;

        /**
         * 从一段连续字节拷贝构造 Tuple。
         *
         * @param data 记录原始字节视图
         * @note 该构造会复制数据，而不是仅保存外部引用
         */
        explicit Tuple(std::span<const std::byte> data)
            : data_(data.begin(), data.end())
        {
        }

        /**
         * 从已有字节容器移动构造 Tuple。
         *
         * @param data 记录原始字节
         */
        explicit Tuple(std::vector<std::byte> data)
            : data_(std::move(data))
        {
        }

        /**
         * 返回记录字节长度。
         */
        uint16_t Size() const noexcept
        {
            return static_cast<uint16_t>(data_.size());
        }

        /**
         * 返回可写字节首地址。
         */
        std::byte* Data() noexcept
        {
            return data_.data();
        }

        /**
         * 返回只读字节首地址。
         */
        const std::byte* Data() const noexcept
        {
            return data_.data();
        }

    private:
        /// Tuple 自持有的原始字节数据
        std::vector<std::byte> data_;
    };

} // namespace record
} // namespace HaruhiDB