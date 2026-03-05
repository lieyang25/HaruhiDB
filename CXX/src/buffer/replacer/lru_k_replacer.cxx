/**
 * CXX/src/buffer/replacer/lru_k_replacer.cxx
 */

 #include "buffer/replacer/lru_k_replacer.h"

namespace HaruhiDB
{
namespace replacer
{
    LruKReplacer::LruKReplacer(size_t pool_size, size_t k = 2) : pool_size_(pool_size),k_(k)
    {
        frames_.resize(pool_size_);
    }

    void LruKReplacer::RecordAccess(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);
        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];
        current_timestamp_ ++;

        frame.history.push_back(current_timestamp_);
        if (frame.history.size() > k_) {
            frame.history.pop_front();
        }
    }

    void LruKReplacer::SetEvictable(frame_id_t frame_id, bool evictable)
    {
        std::lock_guard<std::mutex> guard(latch_);

        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];

        if (frame.evictable == evictable) {
            return;
        }

        frame.evictable = evictable;

        if (evictable) {
            evictable_size_ ++;
        } else {
            evictable_size_ --;
        }
    }

    bool LruKReplacer::Victim(frame_id_t& frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        if (evictable_size_ == 0) {
            return false;
        }

        frame_id = PickVictimInternal();

        if (frame_id == -1 ) {
            return false;
        }

        auto &frame = frames_[frame_id];
        frame.evictable = false;
        evictable_size_ --;
        frame.history.clear();

        return true;
    }

    void LruKReplacer::Remove(frame_id_t frame_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        if (frame_id >= static_cast<frame_id_t>(pool_size_)) {
            return;
        }

        auto &frame = frames_[frame_id];

        if (!frame.evictable) {
            return;
        }

        frame.history.clear();
        frame.evictable = false;
        evictable_size_ --;
    }

    size_t LruKReplacer::Size() const
    {
        std::lock_guard<std::mutex> guard(latch_);
        return evictable_size_;
    }

    frame_id_t LruKReplacer::PickVictimInternal()
    {
        frame_id_t victim = -1;
        uint64_t max_distance = 0;
        uint64_t earliest_timestamp = UINT64_MAX;

        bool found = false;

        for (frame_id_t i = 0;i < static_cast<frame_id_t>(pool_size_);i ++)
        {
            auto &frame = frames_[i];

            if (!frame.evictable || frame.history.empty()) {
                continue;
            }

            uint64_t distance = 0;
            uint64_t first_access = frame.history.front();

            if (frame.history.size() < k_) {
                distance = UINT64_MAX;
            } else {
                distance = current_timestamp_ - frame.history.front();
            }

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
 