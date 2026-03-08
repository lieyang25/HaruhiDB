/**
 * CXX/src/include/storage/page/table_page.h
 */
#include "record/tuple.h"
#include "page.h"
#include "common/config.h"

#include <expected>
namespace HaruhiDB
{
namespace storage
{
    struct Slot
    {
        uint16_t offset;
        uint16_t length;

        uint16_t GetOffset() const {return offset;}
        uint16_t GetLength() const {return length & TUPLE_LENGTH;}

        void SetOffset(uint16_t offset) {offset = offset;}
        void SetLength(uint16_t length) {length |= length;}

        void SetDeleted() {length |= TUPLE_FLAG_DEL;}
        void SetMoved() {length |= TUPLE_FLAG_MOV;}
        
        bool IsDeleted() const {return (length & TUPLE_FLAG_DEL) != 0;}
        bool IsMoved() const {return (length & TUPLE_FLAG_MOV) != 0;}
    };
    
    class TablePage
    {
    public:
        explicit TablePage(Page* page):page_(page){}
        ~TablePage() = default;

        slot_id_t InsertTuple(record::Tuple& tuple);
        bool UpdateTuple(slot_id_t slot_id,record::Tuple& tuple);
        bool MarkDelTuple(slot_id_t slot_id);
        bool GetTuple(slot_id_t slot_id,record::Tuple& tuple);

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
