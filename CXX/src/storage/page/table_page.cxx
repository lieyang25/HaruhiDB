/**
 * CXX/src/storage/page/table_page.cxx
 */

#include "storage/page/table_page.h"

#include <cstring>

namespace HaruhiDB
{
namespace storage
{
    namespace
    {
        struct TupleCounters
        {
            uint16_t alive{0};
            uint16_t deleted{0};
        };

        bool IsTupleCountersConsistent(const TablePageHeaderData* header)
        {
            return static_cast<uint32_t>(header->alive_tuple_count) +
                    static_cast<uint32_t>(header->deleted_tuple_count) ==
                static_cast<uint32_t>(header->slot_count);
        }

        TupleCounters ComputeTupleCounters(const TablePage& table_page, const TablePageHeaderData* header)
        {
            uint32_t deleted = 0;
            for (slot_id_t slot = 0; slot < header->slot_count; ++slot) {
                if (table_page.GetSlot(slot)->IsDeleted()) {
                    ++deleted;
                }
            }
            const auto deleted_count = static_cast<uint16_t>(deleted);
            const auto alive_count = static_cast<uint16_t>(header->slot_count - deleted_count);
            return TupleCounters{alive_count, deleted_count};
        }

        class WritePageGuard
        {
        public:
            explicit WritePageGuard(Page* page) : page_(page) { page_->WLock(); }
            ~WritePageGuard() { page_->WUnLock(); }

            WritePageGuard(const WritePageGuard&) = delete;
            WritePageGuard& operator=(const WritePageGuard&) = delete;

        private:
            Page* page_;
        };

        class ReadPageGuard
        {
        public:
            explicit ReadPageGuard(Page* page) : page_(page) { page_->RLock(); }
            ~ReadPageGuard() { page_->RUnLock(); }

            ReadPageGuard(const ReadPageGuard&) = delete;
            ReadPageGuard& operator=(const ReadPageGuard&) = delete;

        private:
            Page* page_;
        };
    } // namespace

    void TablePage::InitForNewPage(page_id_t page_id)
    {
        if (page_ == nullptr) {
            return;
        }

        WritePageGuard guard(page_);
        auto* base_header = page_->Header();
        base_header->lsn = 0;
        base_header->page_id = page_id;
        base_header->page_type = PageType::HEAP;

        auto* table_header = HeaderData();
        table_header->next_page_id = INVALID_PAGE_ID;
        table_header->slot_count = 0;
        table_header->alive_tuple_count = 0;
        table_header->deleted_tuple_count = 0;
        table_header->free_space_offset = PAGE_SIZE;
        table_header->free_list_head = INVALID_SLOT_ID;
        table_header->reserved = 0;
        page_->MarkDirty();
    }

    TablePageHeaderData* TablePage::HeaderData()
    {
        return reinterpret_cast<TablePageHeaderData*>(page_->Header()->opaque);
    }

    const TablePageHeaderData* TablePage::HeaderData() const
    {
        return reinterpret_cast<const TablePageHeaderData*>(page_->Header()->opaque);
    }

    page_id_t TablePage::NextPageId() const
    {
        return HeaderData()->next_page_id;
    }

    void TablePage::SetNextPageId(page_id_t next_page_id)
    {
        HeaderData()->next_page_id = next_page_id;
    }

    slot_id_t TablePage::SlotCount() const
    {
        return HeaderData()->slot_count;
    }

    std::expected<slot_id_t, TablePageErr> TablePage::InsertTuple(const record::Tuple& tuple)
    {
        if (page_ == nullptr) {
            return std::unexpected(TablePageErr{"TablePage::InsertTuple: null page", TablePageErrCode::NullPage});
        }

        WritePageGuard guard(page_);
        const uint16_t tuple_size = tuple.Size();
        if (tuple_size == 0 || tuple_size > PAGE_SIZE - sizeof(PersistentHeader) - sizeof(Slot)) {
            return std::unexpected(
                TablePageErr{"TablePage::InsertTuple: invalid tuple size", TablePageErrCode::InvalidTupleSize});
        }

        auto* header = HeaderData();
        RepairTupleCounters();

        slot_id_t reuse_slot = INVALID_SLOT_ID;
        slot_id_t reuse_next = INVALID_SLOT_ID;
        if (header->free_list_head != INVALID_SLOT_ID) {
            reuse_slot = header->free_list_head;
            if (reuse_slot >= header->slot_count) {
                return std::unexpected(
                    TablePageErr{"TablePage::InsertTuple: free list corrupted", TablePageErrCode::FreeListCorrupted});
            }
            const Slot* reusable = GetSlot(reuse_slot);
            if (!reusable->IsDeleted()) {
                return std::unexpected(
                    TablePageErr{"TablePage::InsertTuple: free list corrupted", TablePageErrCode::FreeListCorrupted});
            }

            reuse_next = reusable->GetOffset();
            if (reuse_next != INVALID_SLOT_ID && reuse_next >= header->slot_count) {
                return std::unexpected(
                    TablePageErr{"TablePage::InsertTuple: free list corrupted", TablePageErrCode::FreeListCorrupted});
            }
        }

        const bool adding_new_slot = (reuse_slot == INVALID_SLOT_ID);
        uint16_t needed = tuple_size;
        if (adding_new_slot) {
            needed = static_cast<uint16_t>(needed + sizeof(Slot));
        }
        if (FreeSpace() < needed) {
            return std::unexpected(
                TablePageErr{"TablePage::InsertTuple: not enough free space", TablePageErrCode::InsufficientSpace});
        }

        if (!adding_new_slot) {
            if (header->deleted_tuple_count == 0) {
                return std::unexpected(
                    TablePageErr{"TablePage::InsertTuple: free list corrupted", TablePageErrCode::FreeListCorrupted});
            }
            header->free_list_head = reuse_next;
            header->deleted_tuple_count = static_cast<uint16_t>(header->deleted_tuple_count - 1);
        }

        const uint16_t new_data_offset = static_cast<uint16_t>(header->free_space_offset - tuple_size);
        std::memcpy(page_->RawData() + new_data_offset, tuple.Data(), tuple_size);
        header->free_space_offset = new_data_offset;

        const slot_id_t slot_id = adding_new_slot ? header->slot_count : reuse_slot;
        Slot* slot = GetSlot(slot_id);
        slot->SetOffset(new_data_offset);
        slot->SetLength(tuple_size);
        if (adding_new_slot) {
            header->slot_count = static_cast<slot_id_t>(header->slot_count + 1);
        }
        header->alive_tuple_count = static_cast<uint16_t>(header->alive_tuple_count + 1);

        page_->MarkDirty();
        return slot_id;
    }

    std::expected<void, TablePageErr> TablePage::UpdateTuple(slot_id_t slot_id, const record::Tuple& tuple)
    {
        if (page_ == nullptr) {
            return std::unexpected(TablePageErr{"TablePage::UpdateTuple: null page", TablePageErrCode::NullPage});
        }

        WritePageGuard guard(page_);
        auto* header = HeaderData();
        RepairTupleCounters();
        if (slot_id >= header->slot_count) {
            return std::unexpected(
                TablePageErr{"TablePage::UpdateTuple: slot id out of range", TablePageErrCode::SlotOutOfRange});
        }

        Slot* slot = GetSlot(slot_id);
        if (slot->IsDeleted()) {
            return std::unexpected(
                TablePageErr{"TablePage::UpdateTuple: slot already deleted", TablePageErrCode::SlotAlreadyDeleted});
        }

        const uint16_t new_len = tuple.Size();
        if (new_len == 0 || new_len > PAGE_SIZE - sizeof(PersistentHeader) - sizeof(Slot)) {
            return std::unexpected(
                TablePageErr{"TablePage::UpdateTuple: invalid tuple size", TablePageErrCode::InvalidTupleSize});
        }

        const uint16_t old_len = slot->GetLength();
        if (new_len <= old_len) {
            std::memcpy(page_->RawData() + slot->GetOffset(), tuple.Data(), new_len);
            slot->SetLength(new_len);
            page_->MarkDirty();
            return {};
        }

        if (FreeSpace() < new_len) {
            return std::unexpected(
                TablePageErr{"TablePage::UpdateTuple: not enough free space", TablePageErrCode::InsufficientSpace});
        }

        const uint16_t new_data_offset = static_cast<uint16_t>(header->free_space_offset - new_len);
        std::memcpy(page_->RawData() + new_data_offset, tuple.Data(), new_len);
        header->free_space_offset = new_data_offset;
        slot->SetOffset(new_data_offset);
        slot->SetLength(new_len);

        page_->MarkDirty();
        return {};
    }

    std::expected<void, TablePageErr> TablePage::MarkDelTuple(slot_id_t slot_id)
    {
        if (page_ == nullptr) {
            return std::unexpected(TablePageErr{"TablePage::MarkDelTuple: null page", TablePageErrCode::NullPage});
        }

        WritePageGuard guard(page_);
        auto* header = HeaderData();
        RepairTupleCounters();
        if (slot_id >= header->slot_count) {
            return std::unexpected(
                TablePageErr{"TablePage::MarkDelTuple: slot id out of range", TablePageErrCode::SlotOutOfRange});
        }

        Slot* slot = GetSlot(slot_id);
        if (slot->IsDeleted()) {
            return std::unexpected(
                TablePageErr{"TablePage::MarkDelTuple: slot already deleted", TablePageErrCode::SlotAlreadyDeleted});
        }

        slot->SetOffset(header->free_list_head);
        slot->SetDeleted();
        header->free_list_head = slot_id;
        if (header->alive_tuple_count > 0) {
            header->alive_tuple_count = static_cast<uint16_t>(header->alive_tuple_count - 1);
        }
        header->deleted_tuple_count = static_cast<uint16_t>(header->deleted_tuple_count + 1);
        page_->MarkDirty();
        return {};
    }

    std::expected<void, TablePageErr> TablePage::GetTuple(slot_id_t slot_id, record::Tuple& tuple) const
    {
        if (page_ == nullptr) {
            return std::unexpected(TablePageErr{"TablePage::GetTuple: null page", TablePageErrCode::NullPage});
        }

        ReadPageGuard guard(page_);
        const auto* header = HeaderData();
        if (slot_id >= header->slot_count) {
            return std::unexpected(
                TablePageErr{"TablePage::GetTuple: slot id out of range", TablePageErrCode::SlotOutOfRange});
        }

        const Slot* slot = GetSlot(slot_id);
        if (slot->IsDeleted()) {
            return std::unexpected(
                TablePageErr{"TablePage::GetTuple: slot already deleted", TablePageErrCode::SlotAlreadyDeleted});
        }

        const uint16_t offset = slot->GetOffset();
        const uint16_t length = slot->GetLength();
        if (length == 0 || static_cast<size_t>(offset) + length > PAGE_SIZE) {
            return std::unexpected(
                TablePageErr{"TablePage::GetTuple: invalid slot content", TablePageErrCode::InvalidSlotContent});
        }

        std::byte* data_ptr = page_->RawData() + offset;
        tuple = record::Tuple(std::span<std::byte>(data_ptr, length));
        return {};
    }

    bool TablePage::TupleCountersConsistent() const
    {
        if (page_ == nullptr) {
            return false;
        }
        return IsTupleCountersConsistent(HeaderData());
    }

    uint16_t TablePage::AliveTupleCount() const
    {
        if (page_ == nullptr) {
            return 0;
        }
        const auto* header = HeaderData();
        if (IsTupleCountersConsistent(header)) {
            return header->alive_tuple_count;
        }
        return ComputeTupleCounters(*this, header).alive;
    }

    uint16_t TablePage::DeletedTupleCount() const
    {
        if (page_ == nullptr) {
            return 0;
        }
        const auto* header = HeaderData();
        if (IsTupleCountersConsistent(header)) {
            return header->deleted_tuple_count;
        }
        return ComputeTupleCounters(*this, header).deleted;
    }

    void TablePage::RepairTupleCounters()
    {
        if (page_ == nullptr) {
            return;
        }
        auto* header = HeaderData();
        if (IsTupleCountersConsistent(header)) {
            return;
        }
        const auto counts = ComputeTupleCounters(*this, header);
        header->alive_tuple_count = counts.alive;
        header->deleted_tuple_count = counts.deleted;
        page_->MarkDirty();
    }

    Slot* TablePage::SlotArray()
    {
        return reinterpret_cast<Slot*>(page_->RawData() + sizeof(PersistentHeader));
    }

    const Slot* TablePage::SlotArray() const
    {
        return reinterpret_cast<const Slot*>(page_->RawData() + sizeof(PersistentHeader));
    }

    Slot* TablePage::GetSlot(slot_id_t slot_id)
    {
        return &SlotArray()[slot_id];
    }

    const Slot* TablePage::GetSlot(slot_id_t slot_id) const
    {
        return &SlotArray()[slot_id];
    }

    uint16_t TablePage::FreeSpace()
    {
        const auto* header = HeaderData();
        const uint32_t slot_area_end =
            static_cast<uint32_t>(sizeof(PersistentHeader)) +
            static_cast<uint32_t>(header->slot_count) * static_cast<uint32_t>(sizeof(Slot));

        if (header->free_space_offset <= slot_area_end) {
            return 0;
        }
        return static_cast<uint16_t>(header->free_space_offset - slot_area_end);
    }

} // namespace storage
} // namespace HaruhiDB
