/**
 * CXX/src/storage/page/table_page.cxx
 */

#include "page/table_page.h"

namespace HaruhiDB
{
namespace storage
{
    void TablePage::InsertTuple()
    {
        
    }
    void TablePage::UpdateTuple()
    {

    }
    void TablePage::DelTuple()
    {

    }
    void TablePage::GetTuple()
    {

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

