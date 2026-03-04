/**
 * CXX/src/include/storage/page/page.h
 */
#pragma once

#include "common/config.h"
#include <atomic>
#include <shared_mutex>

namespace HaruhiDB
{
namespace storage
{
    enum class PageType : uint8_t {
        INVALID = 0,
        HEAP,
        INTERNAL,
        LEAF,
        HEADER,
        FREELIST
    };

    #pragma pack(push,1)
    struct PersistentHeader
    {
        page_id_t page_id;
        PageType page_type;
        uint16_t slot_count;
        uint16_t free_space_offest;
    };
    #pragma pack(pop)

    struct Slot
    {
        uint16_t offest;
        uint16_t length;
    };

    static_assert(sizeof(PersistentHeader) <= HEADER_SIZE, "PersistentHeader size must lower 32 bytes");

    class Page
    {
    public:
        Page();
        ~Page();

        PersistentHeader GetHeader() const;
        size_t GetFreeSpace() const;

        void Pin();
        void UnPin();
        int PinCount() const;

        void MarkDirty();
        bool IsDirty() const;

        std::shared_mutex& Latch();
        std::array<std::byte,PAGE_SIZE> RawData();
        
    private:
        page_id_t page_id_;
        std::atomic<int> pin_count_;
        std::atomic<bool> is_dirty_;
        std::array<std::byte,PAGE_SIZE> data_;
        std::shared_mutex latch_;
    };
} // namespace storage
} // namespace HaruhiDB
