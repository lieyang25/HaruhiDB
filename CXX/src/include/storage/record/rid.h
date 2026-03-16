/**
 * CXX/src/include/storage/record/rid.h
 *
 * ========================= 设计目标 =========================
 *
 * RID 用于唯一定位表中的一条记录。
 *
 * 它由：
 *
 * 1. page_id
 * 2. slot_id
 *
 * 两部分组成。
 *
 *
 * ========================= 为什么需要 RID =========================
 *
 * 在堆表结构中，tuple 并不是通过主键直接定位，
 * 而是通过“页号 + 槽位号”定位。
 *
 * 因此上层模块在执行以下操作时都需要 RID：
 *
 * - 读取某条记录
 * - 更新某条记录
 * - 删除某条记录
 * - 迭代扫描表数据
 *
 *
 * ========================= RID 在系统中的位置 =========================
 *
 * TableHeap
 *   ├── InsertTuple -> RID
 *   ├── GetTuple(RID)
 *   ├── UpdateTuple(RID)
 *   └── MarkDelete(RID)
 *
 * RID
 *   ├── page_id
 *   └── slot_id
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

        /**
         * @param page_id 页面编号
         * @param slot_id 槽位编号
         */
        RID(page_id_t page_id, slot_id_t slot_id)
            : page_id(page_id), slot_id(slot_id)
        {
        }

        /**
         * 返回所属页面编号。
         */
        page_id_t GetPageId() const
        {
            return this->page_id;
        }

        /**
         * 返回所属槽位编号。
         */
        slot_id_t GetSlotId() const
        {
            return this->slot_id;
        }

        /**
         * 重设 RID。
         *
         * @param page_id 页面编号
         * @param slot_id 槽位编号
         */
        void SetRID(page_id_t page_id, slot_id_t slot_id)
        {
            this->page_id = page_id;
            this->slot_id = slot_id;
        }

        /**
         * 判断两个 RID 是否指向同一条记录。
         */
        bool operator==(const RID& other) const
        {
            return this->page_id == other.page_id &&
                   this->slot_id == other.slot_id;
        }

    private:
        /// 所属页面编号
        page_id_t page_id{INVALID_PAGE_ID};

        /// 页面内槽位编号
        slot_id_t slot_id{INVALID_SLOT_ID};
    };

} // namespace record
} // namespace HaruhiDB