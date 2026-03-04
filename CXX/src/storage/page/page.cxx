/**
 * CXX/src/storage/page/page.cxx
 */

#include "storage/page/page.h"
#include <cstring>


namespace HaruhiDB
{
namespace storage
{
    Page::Page() : pin_count_(0),is_dirty_(false)
    {
        std::memset(data_.data(),0,PAGE_SIZE);
    }

    // Page::~Page() is default, no special cleanup needed
    void Page::InitBlank(page_id_t page_id,PageType page_type)
    {
        PersistentHeader* header = Header();
        header->page_id = page_id;
        header->page_type = page_type;
        header->slot_count = 0;
        header->lsn = 0;
        header->free_space_offset = PAGE_SIZE;

        pin_count_.store(0);
        is_dirty_.store(false);
    }

    // PersistentHeader* Page::Header() and const version
    PersistentHeader* Page::Header() 
    {
        return reinterpret_cast<PersistentHeader*>(data_.data());
    }
    const PersistentHeader* Page::Header() const 
    {
        return reinterpret_cast<const PersistentHeader*>(data_.data());
    }

    page_id_t Page::PageId() 
    {
        return Header()->page_id;
    }
    PageType Page::Type() 
    {
        return Header()->page_type;
    }

    Slot* Page::SlotArray()
    {
        return reinterpret_cast<Slot*>(data_.data() + sizeof(PersistentHeader));
    }
    const Slot* Page::SlotArray() const
    {
        return reinterpret_cast<const Slot*>(data_.data() + sizeof(PersistentHeader));
    }
    std::expected<Slot*,bool> Page::GetSlot(slot_id_t slot_id)
    {
        if (slot_id < Header()->slot_count) {
            return std::unexpected(false);
        }
        return &SlotArray()[slot_id];
    }

    size_t Page::FreeSpace() const
    {
        size_t slot_area_end = sizeof(PersistentHeader)
            + Header()->slot_count * sizeof(Slot);
        return Header()->free_space_offset - slot_area_end;
    }

    // bool Page::InsertRecord(std::span<const std::byte> record) is implemented below.
    bool Page::InsertRecord(std::span<const std::byte> record)
    {
        size_t needed = record.size() + sizeof(Slot);
        if (FreeSpace() < needed) {
            return false;
        }

        PersistentHeader* header = Header();
        header->free_space_offset -= record.size();

        std::memcpy(data_.data() + header->free_space_offset,record.data(),record.size());

        SlotArray()[header->slot_count] = {
            static_cast<uint16_t>(header->free_space_offset),
            static_cast<uint16_t>(record.size())
        };

        header->slot_count ++;

        MarkDirty();
        return true;
    }

    void Page::Pin() 
    {
        pin_count_.fetch_add(1,std::memory_order_relaxed);
    }
    void Page::UnPin() 
    {
        pin_count_.fetch_sub(1,std::memory_order_relaxed);
    }
    int Page::PinCount() const 
    {
        return pin_count_.load(std::memory_order_relaxed);
    }

    void Page::MarkDirty() 
    {
        is_dirty_.store(true,std::memory_order_relaxed);
    }
    bool Page::IsDirty() const 
    {
        return is_dirty_.load(std::memory_order_relaxed);
    }

    void Page::RLock() 
    {
        latch_.lock_shared();
    }
    void Page::RUnLock() 
    {
        latch_.unlock_shared();
    }
    void Page::WLock() 
    {
        latch_.lock();
    }
    void Page::WUnLock() 
    {
        latch_.unlock();
    }

    std::byte* Page::RawData() 
    {
        return data_.data();
    }
    const std::byte* Page::RawData() const 
    {
        return data_.data();
    }
} // namespace Storage
} // namespace HaruhiDB

 