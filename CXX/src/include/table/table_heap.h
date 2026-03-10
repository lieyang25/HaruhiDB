/**
 * CXX/src/include/table/table_heap.h
 */
#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/page/table_page.h"
#include "storage/record/rid.h"
#include "storage/record/tuple.h"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <cstdint>

namespace HaruhiDB {
namespace table {

class TableIterator;


/**
 * TableHeap
 *
 * 作用：
 *  - 管理表的页链（first_page_id_）
 *  - 提供表级操作：Insert/Delete/Update/Get（把单页操作组合为跨页/表级语义）
 *  - 维护一个简单的 free-space map (page_id -> free_bytes) 以加速插入页定位
 *
 * 说明：
 *  - 具体单页的 Insert/Update/Get/Delete 由 TablePage 实现（你已完成）
 *  - TableHeap 负责 pin/unpin/page 链维护/分配和 free-space map 更新
 */
class TableHeap {
public:
    explicit TableHeap(buffer::BufferPoolManager *bpm, page_id_t first_page_id = INVALID_PAGE_ID);

    // 插入 tuple，成功返回 true，并把 RID 写到 out_rid
    // out_rid 不为 nullptr 时写入新记录位置
    bool InsertTuple(const record::Tuple &tuple, record::RID *out_rid = nullptr);

    // 根据 record::RID 获取 tuple（拷贝到 out_tuple）
    bool GetTuple(const record::RID &rid, record::Tuple *out_tuple);

    // 标记删除（逻辑删除）对应 slot
    bool DeleteTuple(const record::RID &rid);

    // 更新 tuple；若 page 内无法原地更新，TablePage 可能返回需要迁移的错误，
    // TableHeap 会尝试将 tuple 移动到有空间的 page 并更新 record::RID（若需要）
    bool UpdateTuple(const record::RID &rid, const record::Tuple &new_tuple);

    // 迭代器接口
    TableIterator Begin();
    TableIterator End();

    // 获取首页 id
    page_id_t FirstPageId() const noexcept { return first_page_id_; }
    void SetFirstPageId(page_id_t pid) noexcept { first_page_id_ = pid; }

private:
    friend class TableIterator;

    // 查找一个有足够 free space 的 page（从 free_space_map 中尝试快速定位）
    // 若找不到则返回 nullopt
    std::optional<page_id_t> FindPageWithFreeSpace(uint32_t need);

    // 分配并初始化一个新页面（返回 page_id），并把它串入页链
    // 若成功返回 page_id；失败返回 nullopt
    std::optional<page_id_t> AllocateNewPage();

    // 更新 free-space map（内部调用，带锁）
    void UpdateFreeSpaceMap(page_id_t page_id, uint32_t free_space);

    // 删除 page（回收）并修正页链（仅在安全可回收时调用）
    // 返回 true 代表删除成功
    bool ReclaimPageIfEmpty(page_id_t page_id);

    buffer::BufferPoolManager *bpm_;
    page_id_t first_page_id_;

    // 简单 free-space map（内存缓存），记录 page_id -> free_bytes
    // 需要在插入/删除后更新
    std::unordered_map<page_id_t, uint32_t> free_space_map_;

    // 保护 free_space_map_ 和 页链更新的 mutex
    mutable std::mutex meta_mutex_;
};

} // namespace table
} // namespace HaruhiDB
