/**
 * CXX/src/include/table/table_iterator.h
 *
 * ========================= 设计目标 =========================
 *
 * TableIterator 负责在 TableHeap 上做顺序扫描。
 *
 * 它提供 STL 风格的最小前向迭代接口：
 *
 * - `operator*` 读取当前 tuple（拷贝语义）
 * - `operator++` 前进到下一条可见记录
 * - `IsEnd` 判断是否到达末尾
 *
 *
 * ========================= 与 TableHeap 的关系 =========================
 *
 * TableHeap 持有页链结构和 BufferPoolManager。
 * TableIterator 依赖 TableHeap 完成：
 *
 * - 取页（FetchPage）
 * - 解除 pin（UnpinPage）
 * - 页链跳转（NextPageId）
 *
 * 迭代器本身不拥有任何页对象，只维护当前位置状态。
 */

#pragma once

#include "storage/record/rid.h"
#include "storage/record/tuple.h"
#include "table/table_heap.h"

#include <cstddef>
#include <vector>

namespace HaruhiDB
{
namespace table
{
/**
 * TableIterator
 *
 * 作用：
 * - 跨页顺序扫描表中的有效 tuple
 * - 在读取期间配合页锁与 pin/unpin 保证访问安全
 */
class TableIterator {
public:
    /**
     * 构造 end 迭代器。
     */
    TableIterator();

    /**
     * 构造从指定位置开始的迭代器。
     *
     * @param heap       关联的表堆
     * @param start_page 起始页号
     * @param start_slot 起始 slot
     */
    TableIterator(TableHeap* heap, page_id_t start_page, slot_id_t start_slot);

    /**
     * 解引用当前记录，返回 tuple 拷贝。
     */
    record::Tuple operator*() const;

    /**
     * 返回当前迭代器指向记录的 RID。
     *
     * 若为 end 迭代器，返回 INVALID RID。
     */
    record::RID GetRID() const noexcept;

    /**
     * 前缀递增，移动到下一条有效记录。
     */
    TableIterator& operator++();

    bool operator==(const TableIterator& other) const noexcept;
    bool operator!=(const TableIterator& other) const noexcept;

    bool IsEnd() const noexcept { return at_end_; }

private:
    /**
     * 从当前 `(cur_page_id_, cur_slot_)` 出发，
     * 定位下一条可见 tuple。
     *
     * @return 找到返回 true，否则 false
     */
    bool AdvanceToNextValid();

    TableHeap* heap_{nullptr};
    page_id_t cur_page_id_{INVALID_PAGE_ID};
    slot_id_t cur_slot_{INVALID_SLOT_ID};
    bool at_end_{true};
    mutable std::vector<std::byte> tuple_buffer_;
    mutable bool tuple_cached_{false};
};

} // namespace table
} // namespace HaruhiDB
