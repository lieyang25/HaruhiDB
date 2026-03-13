/**
 * CXX/src/include/storage/index/index_iterator.h
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

        IndexIterator();
        IndexIterator(
            buffer::BufferPoolManager* bpm,
            page_id_t leaf_page_id,
            uint16_t index);

        MappingType operator*() const;
        IndexIterator& operator++();

        bool operator==(const IndexIterator& other) const noexcept;
        bool operator!=(const IndexIterator& other) const noexcept;

        bool IsEnd() const noexcept
        {
            return at_end_;
        }

    private:
        bool AdvanceToValid();

    private:
        buffer::BufferPoolManager* bpm_{nullptr};
        page_id_t leaf_page_id_{INVALID_PAGE_ID};
        uint16_t index_{0};
        bool at_end_{true};
    };

} // namespace storage
} // namespace HaruhiDB
