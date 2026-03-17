/**
 * CXX/src/include/storage/index/index_iterator.h
 *
 * ========================= 设计目标 =========================
 *
 * IndexIterator 用于在 B+Tree 叶子链上做顺序扫描。
 *
 * 它提供最小前向迭代能力，
 * 用于按叶子页顺序访问索引中的键值对。
 *
 * 核心职责：
 *
 * 1. 维护当前位置
 * 2. 在叶子页内前进
 * 3. 在叶子链上跨页前进
 * 4. 返回当前键值对
 * 5. 判断是否到达末尾
 *
 *
 * ========================= 为什么需要 IndexIterator =========================
 *
 * B+Tree 的查找入口通常只能定位到某个叶子页位置，
 * 但范围扫描、全索引扫描还需要继续沿叶子链前进。
 *
 * 调用方不应手动处理：
 *
 * - 当前叶子页号
 * - 当前槽位下标
 * - 页尾跳转
 * - FetchPage / UnpinPage
 *
 * 这些都由 IndexIterator 统一封装。
 *
 *
 * ========================= IndexIterator 在系统中的位置 =========================
 *
 * BPlusTree
 *   ├── Begin()
 *   └── Begin(key)
 *        │
 *        ▼
 *   IndexIterator
 *        │
 *        ├── operator*()
 *        ├── operator++()
 *        └── IsEnd()
 *
 *
 * ========================= 当前语义 =========================
 *
 * 1. 顺序遍历叶子页中的有序键值对
 * 2. 解引用返回 {key, RID} 拷贝
 * 3. 迭代器本身不拥有页面对象
 * 4. 页访问期间依赖 BufferPoolManager 完成 pin/unpin
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "common/config.h"
#include "storage/record/rid.h"

#include <cstdint>
#include <utility>

namespace HaruhiDB
{
namespace storage
{

    class IndexIterator
    {
    public:
        using MappingType = std::pair<int32_t, record::RID>;

        /**
         * 构造 end 迭代器。
         */
        IndexIterator();

        /**
         * 从指定叶子页位置构造迭代器。
         *
         * @param bpm          缓冲池管理器
         * @param leaf_page_id 起始叶子页号
         * @param index        起始下标
         */
        IndexIterator(
            buffer::BufferPoolManager* bpm,
            page_id_t leaf_page_id,
            uint16_t index);

        /**
         * 解引用当前键值对，返回拷贝。
         */
        MappingType operator*() const;

        /**
         * 前缀递增，移动到下一个有效键值对。
         */
        IndexIterator& operator++();

        /**
         * 比较两个迭代器是否相等。
         */
        bool operator==(const IndexIterator& other) const noexcept;

        /**
         * 比较两个迭代器是否不等。
         */
        bool operator!=(const IndexIterator& other) const noexcept;

        /**
         * 判断当前是否为 end 迭代器。
         */
        bool IsEnd() const noexcept
        {
            return at_end_;
        }

    private:
        /**
         * 从当前位置开始定位下一个有效键值对。
         *
         * @return 找到返回 true，否则返回 false
         */
        bool AdvanceToValid();

    private:
        buffer::BufferPoolManager* bpm_{nullptr};
        page_id_t leaf_page_id_{INVALID_PAGE_ID};
        uint16_t index_{0};
        bool at_end_{true};
    };

} // namespace storage
} // namespace HaruhiDB