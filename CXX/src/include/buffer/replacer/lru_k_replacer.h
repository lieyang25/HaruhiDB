/**
 * CXX/src/include/buffer/replacer/lru_k_replacer.h
 *
 * LRU-K Replacer
 *
 * English:
 * This component implements the LRU-K page replacement algorithm used by the
 * Buffer Pool Manager. When the buffer pool is full and a new page needs to
 * be loaded from disk, the BufferPoolManager will request this replacer to
 * select a victim frame that can be evicted.
 *
 * Compared with the traditional LRU algorithm, LRU-K records the last K access
 * timestamps of each frame. The eviction decision is based on the K-th most
 * recent access, which provides a better estimation of long-term access
 * frequency.
 *
 * Core idea:
 * - If a frame has been accessed fewer than K times, its backward K-distance
 *   is treated as infinite, meaning it is more likely to be evicted.
 * - If a frame has been accessed at least K times, the algorithm calculates
 *   the backward K-distance using the timestamp of the K-th most recent access.
 *
 * The replacer works closely with BufferPoolManager:
 * - BufferPoolManager reports page accesses via RecordAccess()
 * - It marks frames as evictable or non-evictable using SetEvictable()
 * - When space is needed, Victim() is called to choose a frame to evict
 *
 * Thread safety:
 * All internal data structures are protected by a mutex (latch_).
 *
 *
 * 中文：
 * 该组件实现了数据库缓冲池中的 LRU-K 页面替换算法。当 BufferPoolManager
 * 的缓冲池已满且需要加载新的磁盘页时，会调用该替换器选择一个可以被
 * 淘汰（evict）的 frame。
 *
 * 与传统的 LRU 算法不同，LRU-K 会记录每个 frame 最近 K 次访问的时间戳，
 * 并根据第 K 次访问来估计页面的长期访问频率，从而做出更合理的淘汰决策。
 *
 * 核心思想：
 * - 如果某个 frame 的访问次数少于 K 次，则认为其 backward K-distance
 *   为无穷大，因此更容易被淘汰。
 * - 如果访问次数达到 K 次，则根据第 K 次最近访问的时间戳计算
 *   backward K-distance。
 *
 * 该模块与 BufferPoolManager 协同工作：
 * - BufferPoolManager 在页面被访问时调用 RecordAccess()
 * - 使用 SetEvictable() 标记 frame 是否允许被淘汰
 * - 当需要腾出空间时调用 Victim() 选择淘汰页
 *
 * 线程安全：
 * 所有内部数据结构通过互斥锁 latch_ 进行保护。
 */

#pragma once

#include "common/config.h"

#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <optional>
#include <cstdint>
#include <limits>

namespace HaruhiDB
{
namespace replacer
{

    /**
     * English:
     * LruKReplacer manages frame replacement decisions using the LRU-K algorithm.
     *
     * It keeps track of access history for each frame in the buffer pool and
     * determines which frame should be evicted when required.
     *
     * 中文：
     * LruKReplacer 使用 LRU-K 算法管理缓冲池中 frame 的替换策略，
     * 通过记录访问历史来决定在需要淘汰页面时选择哪个 frame。
     */
    class LruKReplacer
    {
    public:

        /**
         * English:
         * Constructor.
         * Initializes the replacer with the buffer pool size and the parameter K.
         *
         * @param pool_size total number of frames in the buffer pool
         * @param k number of historical accesses tracked by LRU-K
         *
         * 中文：
         * 构造函数。
         * 初始化替换器，指定缓冲池 frame 数量以及 LRU-K 的参数 K。
         *
         * @param pool_size 缓冲池中 frame 的数量
         * @param k LRU-K 算法中记录的访问历史次数
         */
        explicit LruKReplacer(size_t pool_size, size_t k = 2);
        ~LruKReplacer() = default;
        /**
         * English:
         * Records an access to a frame. This updates the access history
         * and increments the global timestamp.
         *
         * 中文：
         * 记录某个 frame 的一次访问。
         * 该函数会更新访问历史，并增加全局时间戳。
         */
        void RecordAccess(frame_id_t frame_id);

        /**
         * English:
         * Sets whether a frame is evictable.
         * Frames that are currently pinned should be marked as non-evictable.
         *
         * 中文：
         * 设置某个 frame 是否可以被淘汰。
         * 如果 frame 当前被 pin，则应标记为不可淘汰。
         */
        void SetEvictable(frame_id_t frame_id, bool evictable);

        /**
         * English:
         * Selects a victim frame for eviction.
         *
         * @param frame_id output parameter that stores the victim frame id
         * @return true if a victim is found, false otherwise
         *
         * 中文：
         * 选择一个可以淘汰的 victim frame。
         *
         * @param frame_id 输出参数，返回被选择的 frame id
         * @return 如果找到可淘汰 frame 返回 true，否则返回 false
         */
        bool Victim(frame_id_t& frame_id);

        /**
         * English:
         * Removes a frame from the replacer.
         * This is typically called when a page is deleted.
         *
         * 中文：
         * 从 replacer 中移除某个 frame。
         * 通常在页面被删除时调用。
         */
        void Remove(frame_id_t frame_id);

        /**
         * English:
         * Returns the number of evictable frames currently managed.
         *
         * 中文：
         * 返回当前可淘汰 frame 的数量。
         */
        size_t Size() const;

    private:

        /**
         * English:
         * Metadata describing a single frame.
         *
         * 中文：
         * 描述单个 frame 的元信息。
         */
        struct FrameInfo
        {
            // English: history of the last K access timestamps
            // 中文：记录最近 K 次访问的时间戳
            std::deque<seq_t> history;

            // English: whether this frame can be evicted
            // 中文：该 frame 是否允许被淘汰
            bool evictable{false};
        };

    private:

        // English: mutex protecting all internal structures
        // 中文：保护内部数据结构的互斥锁
        mutable std::mutex latch_;

        // English: total number of frames in the buffer pool
        // 中文：缓冲池中 frame 的总数量
        const size_t pool_size_;

        // English: the K parameter of LRU-K
        // 中文：LRU-K 算法中的参数 K
        const size_t k_;

        // English: global increasing timestamp used for access ordering
        // 中文：全局递增时间戳，用于记录访问顺序
        uint64_t current_timestamp_{0};

        // English: metadata storage for all frames
        // 中文：所有 frame 的元信息存储
        std::vector<FrameInfo> frames_;

        // English: number of frames that are currently evictable
        // 中文：当前允许被淘汰的 frame 数量
        size_t evictable_size_{0};

        /**
         * English:
         * Internal helper function to select a victim frame according
         * to the LRU-K policy.
         *
         * 中文：
         * 内部函数，根据 LRU-K 策略选择一个 victim frame。
         */
        frame_id_t PickVictimInternal();
    };

} // namespace replacer
} // namespace HaruhiDB