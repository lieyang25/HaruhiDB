/**
 * CXX/src/include/storage/page/table_page.h
 *
 * ========================= 设计目标 =========================
 *
 * TablePage 用于管理表数据页中的 tuple 存储。
 *
 * 它建立在通用 Page 之上，
 * 负责把页面解释为“表记录页”并提供 tuple 级操作。
 *
 * 核心职责：
 *
 * 1. 维护表页头部信息
 * 2. 管理 slot 数组
 * 3. 插入 tuple
 * 4. 更新 tuple
 * 5. 标记删除 tuple
 * 6. 读取 tuple
 * 7. 维护页内空闲空间与 free list
 *
 *
 * ========================= 为什么需要 TablePage =========================
 *
 * Page 只提供“一个固定大小内存页”的通用能力，
 * 并不知道页内数据该如何组织。
 *
 * TablePage 负责把一个 Page 解释成：
 *
 * - 表页头
 * - slot 数组
 * - tuple 数据区
 * - 已删除 slot 的 free list
 *
 * 这样 TableHeap 才能在页内进行 tuple 的插入、删除、更新与读取。
 *
 *
 * ========================= TablePage 在系统中的位置 =========================
 *
 * TableHeap
 *   └── TablePage
 *         └── Page
 *
 *
 * ========================= 页面组织 =========================
 *
 *   +------------------------------+
 *   | PersistentHeader             |
 *   +------------------------------+
 *   | Slot[0]                      |
 *   | Slot[1]                      |
 *   | ...                          |
 *   +------------------------------+
 *   |           free space         |
 *   +------------------------------+
 *   | tuple bodies ...             |
 *   +------------------------------+
 *
 * 其中：
 *
 * - PersistentHeader::opaque 被解释为 TablePageHeaderData
 * - Slot 数组从前向后增长
 * - tuple body 从后向前增长
 * - 已删除 slot 通过 free_list_head 串成隐式链表
 */

#pragma once

#include "common/config.h"
#include "storage/page/page.h"
#include "storage/record/tuple.h"

#include <cstddef>
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

    /**
     * TablePage 相关错误。
     */
    struct TablePageErr
    {
        std::string msg;
        TablePageErrCode err_code;
    };

    /**
     * TablePage 在 PersistentHeader::opaque 中保存的页内头部数据。
     */
    struct TablePageHeaderData
    {
        page_id_t next_page_id;
        slot_id_t slot_count;
        uint16_t alive_tuple_count;
        uint16_t deleted_tuple_count;
        uint16_t free_space_offset;
        uint16_t free_list_head;
        uint16_t reserved;
    };

    static_assert(std::is_trivially_copyable_v<TablePageHeaderData>);
    static_assert(sizeof(TablePageHeaderData) == PAGE_HEADER_OPAQUE_SIZE);
    static_assert((offsetof(PersistentHeader, opaque) % alignof(TablePageHeaderData)) == 0);

    /**
     * 表页中的槽位描述符。
     *
     * offset 指向 tuple body 的起始位置，
     * length 低位保存 tuple 长度，高位保存状态标记。
     */
    struct Slot
    {
        uint16_t offset;
        uint16_t length;

        uint16_t GetOffset() const { return offset; }
        uint16_t GetLength() const { return length & TUPLE_LENGTH; }

        void SetOffset(uint16_t new_offset) { offset = new_offset; }
        void SetLength(uint16_t new_length) { length = static_cast<uint16_t>(new_length & TUPLE_LENGTH); }

        void SetDeleted() { length |= TUPLE_FLAG_DEL; }
        void SetMoved() { length |= TUPLE_FLAG_MOV; }

        bool IsDeleted() const { return (length & TUPLE_FLAG_DEL) != 0; }
        bool IsMoved() const { return (length & TUPLE_FLAG_MOV) != 0; }
    };

    class TablePage
    {
    public:
        /**
         * @param page 底层通用页面对象
         */
        explicit TablePage(Page* page) : page_(page) {}

        ~TablePage() = default;

        /**
         * 将底层页面初始化为新的 HEAP 表页。
         *
         * @param page_id 新页号
         */
        void InitForNewPage(page_id_t page_id);

        /**
         * 返回表页头部数据指针。
         */
        TablePageHeaderData* HeaderData();

        /**
         * 返回表页头部数据常量指针。
         */
        const TablePageHeaderData* HeaderData() const;

        /**
         * 返回下一页页号。
         */
        page_id_t NextPageId() const;

        /**
         * 设置下一页页号。
         *
         * @param next_page_id 下一页页号
         */
        void SetNextPageId(page_id_t next_page_id);

        /**
         * 返回当前 slot 数量。
         */
        slot_id_t SlotCount() const;

        /**
         * 插入一个 tuple。
         *
         * @param tuple 待插入 tuple
         * @return 成功时返回 slot_id
         */
        auto InsertTuple(const record::Tuple& tuple) -> std::expected<slot_id_t, TablePageErr>;

        /**
         * 更新指定 slot 的 tuple。
         *
         * @param slot_id 目标槽位
         * @param tuple   新 tuple
         */
        auto UpdateTuple(slot_id_t slot_id, const record::Tuple& tuple)
            -> std::expected<void, TablePageErr>;

        /**
         * 标记删除指定 slot 的 tuple。
         *
         * @param slot_id 目标槽位
         */
        auto MarkDelTuple(slot_id_t slot_id) -> std::expected<void, TablePageErr>;

        /**
         * 读取指定 slot 的 tuple。
         *
         * @param slot_id 目标槽位
         * @param tuple   输出 tuple
         */
        auto GetTuple(slot_id_t slot_id, record::Tuple& tuple) const
            -> std::expected<void, TablePageErr>;

        /**
         * 检查 alive / deleted 计数是否一致。
         */
        bool TupleCountersConsistent() const;

        /**
         * 返回存活 tuple 数量。
         */
        uint16_t AliveTupleCount() const;

        /**
         * 返回已删除 tuple 数量。
         */
        uint16_t DeletedTupleCount() const;

        /**
         * 修复 alive / deleted 计数。
         */
        void RepairTupleCounters();

        /**
         * 返回 slot 数组首地址。
         */
        Slot* SlotArray();

        /**
         * 返回 slot 数组首地址。
         */
        const Slot* SlotArray() const;

        /**
         * 返回指定 slot 指针。
         *
         * @param slot_id 目标槽位
         */
        Slot* GetSlot(slot_id_t slot_id);

        /**
         * 返回指定 slot 常量指针。
         *
         * @param slot_id 目标槽位
         */
        const Slot* GetSlot(slot_id_t slot_id) const;

        /**
         * 返回当前可用连续空闲空间。
         */
        uint16_t FreeSpace();

    private:
        /**
         * 压缩 tuple body 区域。
         *
         * @param skip_slot 压缩时跳过的槽位
         * @return 成功返回 true
         */
        bool CompactTupleBodies(slot_id_t skip_slot = INVALID_SLOT_ID);

        Page* page_;
    };

} // namespace storage
} // namespace HaruhiDB