/**
 * CXX/src/include/table/table_iterator.h
 */

#pragma once

#include "storage/record/rid.h"
#include "storage/record/tuple.h"

#include <cstddef>

namespace HaruhiDB {
namespace storage {
    class Page;
}
}

namespace HaruhiDB {
namespace table {

class TableHeap;

/**
 * TableIterator
 *
 * 作用：
 *  - 跨页顺序扫描表
 *  - 在遍历期间管理 pin/unpin 与页锁（保证读取稳定）
 *
 * 实现策略：
 *  - iterator 内部持有当前 page_id 与当前 slot index
 *  - operator* 返回 Tuple 的拷贝（即读取 page 并拷贝 tuple）
 *  - operator++ 前进到下一个 alive slot；若到页尾则跳到 header.next_page_id 并继续
 */
class TableIterator {
public:
    // 构造一个 end iterator
    TableIterator();

    // 构造一个从指定 RID 开始的 iterator（TableHeap 负责 Begin/End 的构造）
    TableIterator(TableHeap *heap, page_id_t start_page, slot_id_t start_slot);

    // 解引用：读取并返回当前 tuple（拷贝）
    record::Tuple operator*() const;

    // 前缀 ++
    TableIterator &operator++();

    bool operator==(const TableIterator &other) const noexcept;
    bool operator!=(const TableIterator &other) const noexcept;

    bool IsEnd() const noexcept { return at_end_; }

private:
    // helper: 尝试移动到下一个有效位置（slot）；返回 true 表示有有效条目
    bool AdvanceToNextValid();

    TableHeap *heap_;
    page_id_t cur_page_id_;
    slot_id_t cur_slot_;
    bool at_end_;
};

} // namespace table
} // namespace HaruhiDB
