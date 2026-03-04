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
        slot_id_t slot_count;
        uint16_t free_space_offset;
        PageType page_type;
        uint8_t reserved[15];
    };
    static_assert(std::is_trivially_copyable_v<PersistentHeader>);
    static_assert(sizeof(PersistentHeader) <= HEADER_SIZE,"PersistentHeader size must lower HEADER_SIZE");

    struct Slot
    {
        uint16_t offset;
        uint16_t length;
    };

    class Page
    {
    public:
        Page();
        ~Page() = default;

        void InitBlank(page_id_t page_id,PageType page_type);

        // PersistentHeader* Header() and const version
        PersistentHeader* Header() noexcept;
        const PersistentHeader* Header() const noexcept;

        page_id_t PageId() noexcept;
        PageType Type() noexcept;

        // Slot* SlotArray() and const version
        Slot* SlotArray() noexcept;
        const Slot* SlotArray() const noexcept;
        std::expected<Slot*,bool> GetSlot(slot_id_t slot_id);

        size_t FreeSpace() const noexcept;

        bool InsertRecord(std::span<const std::byte> record);

        // Pinning methods
        void Pin() noexcept;
        void UnPin() noexcept;
        int PinCount() const noexcept;

        // Dirty flag methods
        void MarkDirty() noexcept;
        bool IsDirty() const noexcept;

        // Latch methods
        void RLock() ;
        void RUnLock() ;
        void WLock() ;
        void WUnLock() ;

        // Raw data access
        std::byte* RawData() noexcept;
        const std::byte* RawData() const noexcept;
        
    private:
        std::atomic<int> pin_count_;
        std::atomic<bool> is_dirty_;
        page_data_t data_;
        std::shared_mutex latch_;
    };
} // namespace storage
} // namespace HaruhiDB
