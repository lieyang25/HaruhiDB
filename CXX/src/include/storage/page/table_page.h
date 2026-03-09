/**
 * CXX/src/include/storage/page/table_page.h
 */

#pragma once

#include "storage/record/tuple.h"
#include "storage/page/page.h"
#include "common/config.h"

#include <expected>
#include <string>
namespace HaruhiDB
{
namespace storage
{
    enum class TablePageErrCode : int {
        NullPage = 1,
        InvalidTupleSize,
        InsufficientSpace,
        SlotOutOfRange,
        SlotAlreadyDeleted,
        FreeListCorrupted,
        InvalidSlotContent
    };

    struct TablePageErr {
        std::string msg;
        TablePageErrCode err_code;
    };

    struct Slot
    {
        uint16_t offset;
        uint16_t length;

        uint16_t GetOffset() const {return offset;}
        uint16_t GetLength() const {return length & TUPLE_LENGTH;}

        void SetOffset(uint16_t new_offset) {offset = new_offset;}
        void SetLength(uint16_t new_length) {length = static_cast<uint16_t>(new_length & TUPLE_LENGTH);}

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

        auto InsertTuple(const record::Tuple& tuple) -> std::expected<slot_id_t, TablePageErr>;
        auto UpdateTuple(slot_id_t slot_id,const record::Tuple& tuple) -> std::expected<void, TablePageErr>;
        auto MarkDelTuple(slot_id_t slot_id) -> std::expected<void, TablePageErr>;
        auto GetTuple(slot_id_t slot_id,record::Tuple& tuple) const -> std::expected<void, TablePageErr>;

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
