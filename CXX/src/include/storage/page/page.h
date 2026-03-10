/**
 * CXX/src/include/storage/page/page.h
 */

#pragma once

#include "common/config.h"

#include <atomic>
#include <shared_mutex>
#include <span>
#include <expected>

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


    struct PersistentHeader
    {

        lsn_t lsn;


        page_id_t page_id;

        page_id_t next_page_id;

        slot_id_t slot_count;

        uint16_t free_space_offset;

        uint16_t free_list_head;

        PageType page_type;

        uint8_t reserved[9];
    };

    static_assert(std::is_trivially_copyable_v<PersistentHeader>);

    static_assert(sizeof(PersistentHeader) <= HEADER_SIZE,"PersistentHeader size must lower HEADER_SIZE");


    class Page
    {
    public:

        Page();

        ~Page() = default;

        void InitBlank(page_id_t page_id,PageType page_type);

        void ResetMetaData(page_id_t page_id);

        PersistentHeader* Header() noexcept;

        const PersistentHeader* Header() const noexcept;


        page_id_t PageId() noexcept;


        PageType Type() noexcept;

        void Pin() noexcept;


        void UnPin() noexcept;


        int PinCount() const noexcept;


        void MarkDirty() noexcept;


        void ClearDirty() noexcept;

        bool IsDirty() const noexcept;


  
        void RLock() ;

        void RUnLock() ;


    
        void WLock() ;


        void WUnLock() ;

        std::byte* RawData() noexcept;


        const std::byte* RawData() const noexcept;


        page_data_t& Data() noexcept;

        const page_data_t& Data() const noexcept;
        
    private:

        std::atomic<int> pin_count_;

        std::atomic<bool> is_dirty_;

        page_data_t data_;

        std::shared_mutex latch_;
    };

} // namespace storage
} // namespace HaruhiDB