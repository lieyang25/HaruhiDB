/**
 * CXX/src/buffer/replacer/lru_k_replacer.cxx
 *
 * English:
 * Implementation of the LRU-K replacer used by the BufferPoolManager.
 *
 * This module maintains access history for each frame and selects a victim
 * frame according to the LRU-K replacement policy when the buffer pool needs
 * to evict a page.
 *
 * Core mechanism:
 * 1. Each frame maintains the last K access timestamps.
 * 2. The replacer computes the backward K-distance when selecting a victim.
 * 3. Frames with fewer than K accesses are treated as having infinite distance.
 * 4. Only frames marked as evictable are candidates for eviction.
 *
 * Thread safety:
 * All public operations are protected by the internal mutex `latch_`.
 *
 *
 * 中文：
 * 本文件实现了 LRU-K 页面替换算法，用于 BufferPoolManager 在缓冲池
 * 满时选择要淘汰的 frame。
 *
 * 主要机制：
 * 1. 每个 frame 记录最近 K 次访问时间戳。
 * 2. 淘汰时计算 backward K-distance。
 * 3. 若访问次数不足 K 次，则认为距离为无穷大（优先淘汰）。
 * 4. 只有被标记为 evictable 的 frame 才可以被淘汰。
 *
 * 线程安全：
 * 所有对内部状态的访问都通过互斥锁 `latch_` 保护。
 */

#include "buffer/replacer/lru_k_replacer.h"

namespace HaruhiDB
{
namespace replacer
{

    /**
     * English:
     * Constructor of LruKReplacer.
     * Initializes the replacer with a fixed number of frames and parameter K.
     *
     * 中文：
     * LruKReplacer 构造函数。
     * 初始化替换器并设置缓冲池 frame 数量以及 K 值。
     */
    LruKReplacer::LruKReplacer(size_t pool_size, size_t k)
        : pool_size_(pool_size), k_(k)
    {
        // English: allocate metadata for all frames
        // 中文：为所有 frame 分配元数据结构
        frames_.resize(pool_size_);
    }


    /**
     * English:
     * Records an access to the specified frame.
     * Updates the access history and maintains only the last K timestamps.
     *
     * 中文：
     * 记录某个 frame 的访问。
     * 更新访问历史，并且只保留最近 K 次访问时间戳。
     */
    void LruKReplacer::RecordAccess(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // English: ignore invalid frame ids
        // 中文：忽略非法 frame_id
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];

        // English: advance global timestamp
        // 中文：递增全局时间戳
        current_timestamp_++;

        // English: record the access time
        // 中文：记录访问时间
        frame.history.push_back(current_timestamp_);

        // English: keep only the last K timestamps
        // 中文：只保留最近 K 次访问
        if (frame.history.size() > k_) {
            frame.history.pop_front();
        }
    }


    /**
     * English:
     * Sets whether the frame is evictable.
     *
     * If a frame becomes evictable, the replacer increases the evictable count.
     * If it becomes non-evictable, the count decreases.
     *
     * 中文：
     * 设置 frame 是否允许被淘汰。
     *
     * 当 frame 变为 evictable 时增加计数，
     * 变为不可淘汰时减少计数。
     */
    void LruKReplacer::SetEvictable(frame_id_t frame_id, bool evictable)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // English: ignore invalid frame ids
        // 中文：忽略非法 frame_id
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];

        // English: no change needed if state is identical
        // 中文：若状态相同则无需修改
        if (frame.evictable == evictable) {
            return;
        }

        frame.evictable = evictable;

        // English: update evictable frame counter
        // 中文：更新可淘汰 frame 计数
        if (evictable) {
            evictable_size_++;
        } else {
            evictable_size_--;
        }
    }


    /**
     * English:
     * Selects a victim frame according to the LRU-K policy.
     *
     * After selecting the victim, the frame is removed from the replacer
     * and its metadata is cleared.
     *
     * 中文：
     * 根据 LRU-K 算法选择一个 victim frame。
     *
     * 选择后该 frame 会从 replacer 中移除，并清除历史信息。
     */
    bool LruKReplacer::Victim(frame_id_t& frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // English: no evictable frame exists
        // 中文：没有可淘汰 frame
        if (evictable_size_ == 0) {
            return false;
        }

        // English: select victim internally
        // 中文：内部选择 victim
        frame_id = PickVictimInternal();

        if (frame_id == -1) {
            return false;
        }

        auto &frame = frames_[frame_id];

        // English: remove frame from replacer
        // 中文：从 replacer 中移除该 frame
        frame.evictable = false;
        evictable_size_--;

        // English: clear access history
        // 中文：清空访问历史
        frame.history.clear();

        return true;
    }


    /**
     * English:
     * Removes a frame from the replacer.
     * Typically used when the page is deleted.
     *
     * 中文：
     * 从 replacer 中移除某个 frame。
     * 通常在页面被删除时调用。
     */
    void LruKReplacer::Remove(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // English: ignore invalid frame ids
        // 中文：忽略非法 frame_id
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];

        // English: cannot remove non-evictable frame
        // 中文：不可移除不可淘汰的 frame
        if (!frame.evictable) {
            return;
        }

        // English: clear metadata
        // 中文：清除元信息
        frame.history.clear();
        frame.evictable = false;

        evictable_size_--;
    }


    /**
     * English:
     * Returns the number of evictable frames.
     *
     * 中文：
     * 返回当前可淘汰 frame 的数量。
     */
    size_t LruKReplacer::Size() const
    {
        std::lock_guard<std::mutex> guard(latch_);
        return evictable_size_;
    }


    /**
     * English:
     * Internal victim selection algorithm for LRU-K.
     *
     * Selection rules:
     * 1. Prefer frames with largest backward K-distance.
     * 2. Frames with fewer than K accesses have infinite distance.
     * 3. If multiple frames have infinite distance, choose the one
     *    with the earliest access timestamp.
     *
     * 中文：
     * LRU-K 的内部 victim 选择算法。
     *
     * 规则：
     * 1. 优先选择 backward K-distance 最大的 frame。
     * 2. 访问次数少于 K 的 frame 视为距离无限大。
     * 3. 若多个 frame 为无限距离，则选择最早访问的。
     */
    frame_id_t LruKReplacer::PickVictimInternal()
    {
        // English: selected victim frame id
        // 中文：最终选择的 victim
        frame_id_t victim = -1;

        // English: largest backward distance found so far
        // 中文：当前最大 backward distance
        uint64_t max_distance = 0;

        // English: earliest timestamp used for tie-breaking
        // 中文：用于打破平局的最早访问时间
        uint64_t earliest_timestamp = UINT64_MAX;

        bool found = false;

        // English: scan all frames
        // 中文：遍历所有 frame
        for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size_); i++)
        {
            auto &frame = frames_[i];

            // English: skip non-evictable or unused frames
            // 中文：跳过不可淘汰或未访问 frame
            if (!frame.evictable || frame.history.empty()) {
                continue;
            }

            uint64_t distance = 0;

            // English: first recorded access
            // 中文：最早记录的访问时间
            uint64_t first_access = frame.history.front();

            // English: compute backward K-distance
            // 中文：计算 backward K-distance
            if (frame.history.size() < k_) {
                distance = UINT64_MAX;
            } else {
                distance = current_timestamp_ - frame.history.front();
            }

            // English: first candidate
            // 中文：第一个候选
            if (!found) {
                victim = i;
                max_distance = distance;
                earliest_timestamp = first_access;
                found = true;
            }
            // English: prefer larger backward distance
            // 中文：优先更大的 distance
            else if (distance > max_distance) {
                victim = i;
                max_distance = distance;
                earliest_timestamp = first_access;
            }
            // English: tie-break when both are infinite distance
            // 中文：若都是无限距离，则选择更早访问的
            else if (distance == UINT64_MAX && max_distance == UINT64_MAX) {
                if (first_access < earliest_timestamp) {
                    victim = i;
                    earliest_timestamp = first_access;
                }
            }
        }

        return victim;
    }

} // namespace replacer
} // namespace HaruhiDB