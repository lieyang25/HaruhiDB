/**
 * CXX/src/include/buffer/replacer/lru_k_replacer.h
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

    class LruKReplacer
    {
    public:
        explicit LruKReplacer(size_t pool_size, size_t k = 2);

        void RecordAccess(frame_id_t frame_id);

        void SetEvictable(frame_id_t frame_id, bool evictable);

        bool Victim(frame_id_t& frame_id);

        void Remove(frame_id_t frame_id);

        size_t Size() const;

    private:

        struct FrameInfo
        {
            std::deque<seq_t> history;
            bool evictable{false};
        };

    private:

        mutable std::mutex latch_;

        const size_t pool_size_;
        const size_t k_;

        uint64_t current_timestamp_{0};

        std::vector<FrameInfo> frames_;

        size_t evictable_size_{0};

        frame_id_t PickVictimInternal();
    };
} // namespace replacer
} // namespace HaruhiDB