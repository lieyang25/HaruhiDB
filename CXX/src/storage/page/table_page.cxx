/**
 * CXX/src/storage/page/table_page.cxx
 */

#include "page/table_page.h"
#include <cstring>

namespace HaruhiDB
{
namespace storage
{
    //采用隐式链表法，链表头在header的free_list_head
    //插入时优先查找链表，INVALID_SLOT_ID在此处代表 无删除过 的空slot位置
    //如果不存在则在slot后方新增一个位置
    slot_id_t TablePage::InsertTuple(record::Tuple& tuple)
    {
        // size check
        page_->WLock();
        uint16_t size = tuple.Size();
        if (size == 0 || size > PAGE_SIZE - sizeof(PersistentHeader) - sizeof(Slot)) {
            return INVALID_SLOT_ID;
        }

        auto header = page_->Header();

        // lock page for write

        // try reuse free slot
        slot_id_t reuse_slot = INVALID_SLOT_ID;
        if (header->free_list_head != INVALID_SLOT_ID) {
            reuse_slot = header->free_list_head;
            // pop head
            Slot* slot = GetSlot(reuse_slot);
            // slot.offset currently stores next_free (by convention)
            header->free_list_head = slot->offset;
            // we'll reuse this slot: allocate data area below
            // Note: slot's previous offset no longer points to data; we'll overwrite it.
        }

        // compute required extra header bytes if adding a new slot
        bool adding_new_slot = (reuse_slot == INVALID_SLOT_ID);
        uint16_t needed = size;
        if (adding_new_slot) {
            needed += static_cast<uint16_t>(sizeof(Slot));
        }

        if (FreeSpace() < needed) {
            page_->WUnLock();
            return INVALID_SLOT_ID;
        }

        // allocate data area: grow downward
        uint16_t new_data_offset = static_cast<uint16_t>(header->free_space_offset - size);
        std::byte* dest = page_->RawData() + new_data_offset;

        // copy tuple bytes (assume tuple.Data() returns std::span<std::byte>)
        std::memcpy(dest, tuple.Data(), size);

        // update free_space_offset
        header->free_space_offset = new_data_offset;

        slot_id_t result_slot_id = INVALID_SLOT_ID;
        if (adding_new_slot) {
            result_slot_id = header->slot_count;
            // initialize new slot
            Slot* slot = GetSlot(result_slot_id);
            slot->SetOffset(new_data_offset);
            slot->SetLength(size);

            header->slot_count += 1;
        } else {
            result_slot_id = reuse_slot;

            Slot* slot = GetSlot(result_slot_id);
            slot->SetOffset(new_data_offset);
            slot->SetLength(size);
        }

        page_->MarkDirty();
        page_->WUnLock();
        return result_slot_id;
    }


    bool TablePage::UpdateTuple(slot_id_t slot_id, record::Tuple& tuple) {
        // TODO: 1. 线程安全优化
        // 检查 header->slot_count 应该在加锁之后，防止在检查和加锁的间隙，
        // 其他线程执行了压缩（Defragment）或截断操作。
        
        page_->WLock();
        
        auto header = page_->Header();
        if (slot_id >= header->slot_count) {
            return false;
        }

        Slot* slot = GetSlot(slot_id);
        if (slot->IsDeleted()) {
            page_->WUnLock();
            return false;
        }

        uint16_t old_len = slot->GetLength();
        uint16_t new_len = tuple.Size();

        if (new_len == 0) {
            page_->WUnLock();
            return false;
        }

        if (new_len <= old_len) {
            std::byte* dest = page_->RawData() + slot->GetOffset();
            std::memcpy(dest,tuple.Data(),new_len);
            slot->SetLength(new_len);
            page_->MarkDirty();
            page_->WUnLock();
            return true;
        }

        if (FreeSpace() < new_len) {
            page_->WUnLock();
            return false;
        }

        uint16_t new_data_offset = static_cast<uint16_t>(header->free_space_offset - new_len);
        std::byte* dest = page_->RawData() + new_data_offset;

        // copy tuple bytes (assume tuple.Data() returns std::span<std::byte>)
        std::memcpy(dest, tuple.Data(), new_len);

        // update free_space_offset
        header->free_space_offset = new_data_offset;
        slot->SetOffset(new_data_offset);
        slot->SetLength(new_len);

        page_->MarkDirty();
        page_->WUnLock();
        return true;
    }

    //采用隐式链表法，此处只是标记删除，标记位为0x8000
    bool TablePage::MarkDelTuple(slot_id_t slot_id)
    {
        page_->WLock();
        auto header = page_->Header();
        if (slot_id >= header->slot_count) {
            return false;
        }
        

        Slot* slot = GetSlot(slot_id);
        if (slot->IsDeleted()) {
            page_->WUnLock();
            return false;
        }

        slot->offset = header->free_list_head;
        slot->SetDeleted();

        header->free_list_head = slot_id;

        page_->MarkDirty();
        page_->WUnLock();
        return true;
    }

    //获取指定slot位置的tuple
    //根据slot_id获取slot槽位的信息
    //根据槽位信息再去取tuple
    bool TablePage::GetTuple(slot_id_t slot_id,record::Tuple& tuple)
    {
        page_->RLock();
        auto header = page_->Header();
        if (slot_id >= header->slot_count) {
            page_->RUnLock();
            return false;
        }

        Slot* slot = GetSlot(slot_id);

        if (slot->IsDeleted()) {
            page_->RUnLock();
            return false;
        }

        std::byte* data_ptr = slot->GetOffset() + page_->RawData();

        tuple = record::Tuple(std::span<std::byte>(data_ptr,slot->GetLength()));
        page_->RUnLock();
        return true;
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
        return page_->Header()->free_space_offset - static_cast<uint16_t>(
            sizeof(PersistentHeader) + page_->Header()->slot_count * sizeof(Slot)
        );
    }
} // namespace storage
} // namespace HaruhiDB

