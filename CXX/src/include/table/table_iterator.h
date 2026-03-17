/**
 * CXX/src/include/table/table_iterator.h
 *
 * ========================= 设计目标 =========================
 *
 * TableIterator 用于在 TableHeap 上做顺序扫描。
 *
 * 它提供最小前向迭代能力，
 * 用于按页链顺序访问表中的有效 tuple。
 *
 * 核心职责：
 *
 * 1. 维护当前位置
 * 2. 跳过已删除记录
 * 3. 跨页前进
 * 4. 返回当前 tuple 的拷贝
 * 5. 返回当前记录 RID
 *
 *
 * ========================= 为什么需要 TableIterator =========================
 *
 * TableHeap 负责表级组织，
 * 但顺序扫描整张表时，调用方不应手动处理：
 *
 * - 当前页号
 * - 当前 slot
 * - 页尾跳转
 * - 删除记录跳过
 * - 取页与 unpin
 *
 * 这些都由 TableIterator 统一封装。
 *
 *
 * ========================= TableIterator 在系统中的位置 =========================
 *
 * TableHeap
 *   ├── Begin()
 *   └── End()
 *        │
 *        ▼
 *   TableIterator
 *        │
 *        ├── operator*()
 *        ├── operator++()
 *        └── GetRID()
 *
 *
 * ========================= 当前语义 =========================
 *
 * 1. 仅遍历未删除 tuple
 * 2. 解引用返回 tuple 拷贝
 * 3. 迭代器本身不拥有页面对象
 * 4. 页访问期间依赖 TableHeap + BufferPoolManager 完成 pin/unpin
 */

#pragma once

#include "storage/record/rid.h"
#include "storage/record/tuple.h"

#include <cstddef>
#include <vector>

namespace HaruhiDB
{
namespace table
{
class TableHeap;

class TableIterator
{
public:
    /**
     * 构造 end 迭代器。
     */
    TableIterator();

    /**
     * 从指定位置构造迭代器。
     *
     * @param heap       关联表堆
     * @param start_page 起始页号
     * @param start_slot 起始槽位
     */
    TableIterator(TableHeap* heap, page_id_t start_page, slot_id_t start_slot);

    /**
     * 解引用当前记录，返回 tuple 拷贝。
     */
    record::Tuple operator*() const;

    /**
     * 返回当前记录的 RID。
     *
     * @note 若为 end 迭代器，则返回无效 RID
     */
    record::RID GetRID() const noexcept;

    /**
     * 前缀递增，移动到下一条有效记录。
     */
    TableIterator& operator++();

    /**
     * 比较两个迭代器是否相等。
     */
    bool operator==(const TableIterator& other) const noexcept;

    /**
     * 比较两个迭代器是否不等。
     */
    bool operator!=(const TableIterator& other) const noexcept;

    /**
     * 判断当前是否为 end 迭代器。
     */
    bool IsEnd() const noexcept { return at_end_; }

private:
    /**
     * 从当前位置开始定位下一条有效 tuple。
     *
     * @return 找到返回 true，否则返回 false
     */
    bool AdvanceToNextValid();

    TableHeap* heap_{nullptr};
    page_id_t cur_page_id_{INVALID_PAGE_ID};
    slot_id_t cur_slot_{INVALID_SLOT_ID};
    bool at_end_{true};

    /// 当前 tuple 的缓存拷贝
    mutable std::vector<std::byte> tuple_buffer_;

    /// 当前缓存是否有效
    mutable bool tuple_cached_{false};
};

} // namespace table
} // namespace HaruhiDB