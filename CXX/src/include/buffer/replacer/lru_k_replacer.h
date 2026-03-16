/**
 * CXX/src/include/buffer/replacer/lru_k_replacer.h
 *
 * ========================= 设计目标 =========================
 *
 * LruKReplacer 实现 BufferPoolManager 使用的 LRU-K 替换策略。
 *
 * 当缓冲池没有空闲 frame 且需要装入新页时，
 * 该组件负责从“允许淘汰”的 frame 中选择 victim。
 *
 * 核心职责：
 *
 * 1. 记录 frame 的访问历史
 * 2. 维护 frame 是否可淘汰
 * 3. 按 LRU-K 规则选择 victim
 * 4. 统计当前可淘汰 frame 数量
 *
 *
 * ========================= 为什么需要 LruKReplacer =========================
 *
 * 普通 LRU 只看最近一次访问，
 * 容易把短期突发访问误判为长期热点。
 *
 * LRU-K 通过记录最近 K 次访问，
 * 使用第 K 次最近访问来估计长期访问行为，
 * 从而让淘汰决策更稳定。
 *
 *
 * ========================= LruKReplacer 在系统中的位置 =========================
 *
 * BufferPoolManager
 *   ├── page table
 *   ├── free list
 *   └── LruKReplacer
 *
 * LruKReplacer
 *   ├── RecordAccess(frame_id)
 *   ├── SetEvictable(frame_id, bool)
 *   ├── Victim(frame_id)
 *   └── Remove(frame_id)
 *
 *
 * ========================= 选择规则 =========================
 *
 * 1. 只在 evictable frame 中选择 victim
 * 2. 访问次数少于 K 的 frame 视为 backward K-distance 为无穷大
 * 3. 在可淘汰候选中优先选择 backward K-distance 更大的 frame
 * 4. 若同为无穷大，则选择更早进入历史的 frame
 */

#pragma once

#include "common/config.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace HaruhiDB
{
namespace replacer
{

    class LruKReplacer
    {
    public:
        /**
         * @param pool_size 缓冲池 frame 总数
         * @param k         LRU-K 中的 K
         */
        explicit LruKReplacer(size_t pool_size, size_t k = 2);

        ~LruKReplacer() = default;

        /**
         * 记录一次 frame 访问。
         *
         * @param frame_id 被访问的 frame
         */
        void RecordAccess(frame_id_t frame_id);

        /**
         * 设置 frame 是否允许被淘汰。
         *
         * @param frame_id 目标 frame
         * @param evictable 是否允许淘汰
         */
        void SetEvictable(frame_id_t frame_id, bool evictable);

        /**
         * 选择一个可淘汰的 victim frame。
         *
         * @param frame_id 输出参数，返回 victim frame
         * @return 成功返回 true，否则返回 false
         */
        bool Victim(frame_id_t& frame_id);

        /**
         * 从 replacer 中移除一个 frame。
         *
         * @param frame_id 要移除的 frame
         * @note 通常用于页面被删除的场景
         */
        void Remove(frame_id_t frame_id);

        /**
         * 返回当前可淘汰 frame 数量。
         */
        size_t Size() const;

    private:
        /**
         * 单个 frame 在 replacer 中的元信息。
         */
        struct FrameInfo
        {
            /// 最近 K 次访问时间戳
            std::deque<seq_t> history;

            /// 是否允许被淘汰
            bool evictable{false};
        };

    private:
        /// 保护内部状态的互斥锁
        mutable std::mutex latch_;

        /// 缓冲池 frame 总数
        const size_t pool_size_;

        /// LRU-K 参数 K
        const size_t k_;

        /// 全局递增访问时间戳
        uint64_t current_timestamp_{0};

        /// 所有 frame 的元信息
        std::vector<FrameInfo> frames_;

        /// 当前可淘汰 frame 数量
        size_t evictable_size_{0};

        /**
         * 按 LRU-K 规则选择 victim。
         *
         * @return 选中的 frame_id；若失败则返回 max frame_id
         */
        frame_id_t PickVictimInternal();
    };

} // namespace replacer
} // namespace HaruhiDB