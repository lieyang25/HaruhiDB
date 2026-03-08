/**
 * CXX/src/include/storage/page/table_page.h
 */
#include "page.h"
#include "common/config.h"

#include <expected>
namespace HaruhiDB
{
namespace storage
{
    struct Slot
    {
        uint16_t offest;
        uint16_t length;
    };
    
    class TablePage
    {
    public:
        explicit TablePage(Page* page):page_(page){}
        ~TablePage() = default;

        void InsertTuple();
        void UpdateTuple();
        void DelTuple();
        void GetTuple();
        Slot* SlotArray();
        const Slot* SlotArray() const;
        Slot* GetSlot(slot_id_t slot_id);
        const Slot* GetSlot(slot_id_t slot_id) const;
        uint16_t FreeSpace();

    private:
        Page* page_;
    };

} // namespace storage
} // namespace HaruhiDB
