/**
 * CXX/src/buffer/replacer/lru_k_replacer.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 LruKReplacer 的替换逻辑。
 *
 * 主要完成：
 *
 * 1. 记录 frame 访问历史
 * 2. 维护 evictable 状态
 * 3. 统计可淘汰 frame 数量
 * 4. 按 LRU-K 规则选择 victim
 *
 *
 * ========================= 与 BufferPoolManager 的联动 =========================
 *
 * BufferPoolManager
 *   ├── 页面被访问时调用 RecordAccess
 *   ├── pin/unpin 时调用 SetEvictable
 *   ├── 需要淘汰时调用 Victim
 *   └── 页面删除时调用 Remove
 *
 *
 * ========================= 实现说明 =========================
 *
 * 本实现以 frame 为单位维护访问历史。
 *
 * 每个 frame 记录最近 K 次访问时间戳，
 * victim 选择只在 evictable frame 中进行。
 */

#include "buffer/replacer/lru_k_replacer.h"

namespace HaruhiDB
{
namespace replacer
{

    /**
     * @param pool_size 缓冲池 frame 总数
     * @param k         LRU-K 中的 K
     */
    LruKReplacer::LruKReplacer(size_t pool_size, size_t k)
        : pool_size_(pool_size), k_(k)
    {
        frames_.resize(pool_size_);
    }

    /**
     * @param frame_id 被访问的 frame
     */
    void LruKReplacer::RecordAccess(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 过滤非法 frame_id。
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto& frame = frames_[frame_id];

        // step 2: 推进全局时间戳，并写入本次访问。
        current_timestamp_++;
        frame.history.push_back(current_timestamp_);

        // step 3: 只保留最近 K 次访问历史。
        if (frame.history.size() > k_) {
            frame.history.pop_front();
        }
    }

    /**
     * @param frame_id 目标 frame
     * @param evictable 是否允许淘汰
     */
    void LruKReplacer::SetEvictable(frame_id_t frame_id, bool evictable)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 过滤非法 frame_id。
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto& frame = frames_[frame_id];

        // step 2: 若状态未变化，则直接返回。
        if (frame.evictable == evictable) {
            return;
        }

        // step 3: 更新 frame 状态与全局计数。
        frame.evictable = evictable;
        if (evictable) {
            evictable_size_++;
        } else {
            evictable_size_--;
        }
    }

    /**
     * @param frame_id 输出参数，返回 victim frame
     * @return 成功返回 true，否则返回 false
     */
    bool LruKReplacer::Victim(frame_id_t& frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 若没有可淘汰 frame，则失败。
        if (evictable_size_ == 0) {
            return false;
        }

        // step 2: 按 LRU-K 规则选择 victim。
        frame_id = PickVictimInternal();
        if (frame_id == std::numeric_limits<frame_id_t>::max()) {
            return false;
        }

        auto& frame = frames_[frame_id];

        // step 3: 从 replacer 视角移除该 frame。
        frame.evictable = false;
        evictable_size_--;
        frame.history.clear();

        return true;
    }

    /**
     * @param frame_id 要移除的 frame
     * @note 仅允许移除 evictable frame
     */
    void LruKReplacer::Remove(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto& frame = frames_[frame_id];
        if (!frame.evictable) {
            return;
        }

        frame.history.clear();
        frame.evictable = false;
        evictable_size_--;
    }

    /**
     * 返回当前可淘汰 frame 数量。
     */
    size_t LruKReplacer::Size() const
    {
        std::lock_guard<std::mutex> guard(latch_);
        return evictable_size_;
    }

    /**
     * 按 LRU-K 规则选择 victim。
     *
     * 规则：
     *
     * 1. 只考虑 evictable frame
     * 2. history.size() < k_ 视为无限距离
     * 3. 优先选择 backward K-distance 更大的 frame
     * 4. 若同为无限距离，则选择更早访问的 frame
     */
    frame_id_t LruKReplacer::PickVictimInternal()
    {
        frame_id_t victim = std::numeric_limits<frame_id_t>::max();
        uint64_t max_distance = 0;
        uint64_t earliest_timestamp = UINT64_MAX;
        bool found = false;

        // step 1: 遍历所有 frame，跳过不可淘汰项。
        for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size_); i++) {
            auto& frame = frames_[i];
            if (!frame.evictable) {
                continue;
            }

            // step 2: 计算当前 frame 的比较关键字。
            uint64_t distance = 0;
            uint64_t first_access = 0;

            if (frame.history.empty()) {
                distance = UINT64_MAX;
            } else if (frame.history.size() < k_) {
                first_access = frame.history.front();
                distance = UINT64_MAX;
            } else {
                first_access = frame.history.front();
                distance = current_timestamp_ - frame.history.front();
            }

            // step 3: 按距离优先、最早访问次优先的规则更新 victim。
            if (!found) {
                victim = i;
                max_distance = distance;
                earliest_timestamp = first_access;
                found = true;
            } else if (distance > max_distance) {
                victim = i;
                max_distance = distance;
                earliest_timestamp = first_access;
            } else if (distance == UINT64_MAX && max_distance == UINT64_MAX) {
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