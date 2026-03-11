/**
 * CXX/src/include/storage/record/rid.h
 */

#pragma once

#include "common/config.h"

namespace HaruhiDB
{
namespace record
{
    class RID
    {
    public:
        RID() = default;
        ~RID() = default;
        RID(page_id_t page_id,slot_id_t slot_id)
            :page_id(page_id),slot_id(slot_id){}
        page_id_t GetPageId() const {return this->page_id;};
        slot_id_t GetSlotId() const {return this->slot_id;};
        void SetRID(page_id_t page_id,slot_id_t slot_id) {
            this->page_id = page_id,this->slot_id = slot_id;
        } 
        bool operator==(const RID& other)const{
            return this->page_id == other.page_id &&
                   this->slot_id == other.slot_id;
        }
    private:
        page_id_t page_id{INVALID_PAGE_ID};
        slot_id_t slot_id{INVALID_SLOT_ID};
    };
} // namespace record
} // namespace HaruhiDB
