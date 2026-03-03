/**
 * CXX/src/include/storage/page/page.h
 */
#pragma once

#include "common/config.h"


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

    static_assert(sizeof(PersistentHeader) <= HEADER_SIZE, "PersistentHeader size must lower 32 bytes");

    class Page
    {
    public:
        Page();
        ~Page();
    private:
        std::array<std::byte,PAGE_SIZE> data_;
    };
} // namespace storage
} // namespace HaruhiDB
