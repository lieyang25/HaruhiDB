/**
 * CXX/src/include/storage/index/b_plus_tree.h
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/record/rid.h"

#include <mutex>
#include <shared_mutex>

namespace HaruhiDB
{
namespace storage
{

    class BPlusTree
    {
    public:
        explicit BPlusTree(buffer::BufferPoolManager* bpm);
        BPlusTree(buffer::BufferPoolManager* bpm, page_id_t header_page_id);

        bool IsEmpty() const noexcept;

        bool GetValue(int32_t key, record::RID* out_rid);

        bool Insert(int32_t key, const record::RID& rid);
        bool Remove(int32_t key);

        IndexIterator Begin();
        IndexIterator Begin(int32_t key);
        IndexIterator End() const noexcept;

        page_id_t RootPageId() const noexcept;
        page_id_t HeaderPageId() const noexcept;

    private:
        page_id_t FindLeafPage(int32_t key);
        page_id_t FindLeftMostLeafPage();

        bool StartNewTree(int32_t key, const record::RID& rid);

        bool InsertIntoParent(
            page_id_t old_page_id,
            int32_t split_key,
            page_id_t new_page_id);

        bool SetChildParent(page_id_t child_page_id, page_id_t parent_page_id);

        bool UpdateInternalChildrenParent(
            BPlusTreeInternalPage* internal_page,
            page_id_t internal_page_id);

        bool InitOrLoadHeaderPage(page_id_t header_page_id_hint);
        bool PersistRootPageIdLocked();
        bool RebalanceAfterDelete(page_id_t page_id);
        bool AdjustRootAfterDelete(page_id_t root_page_id);

    private:
        buffer::BufferPoolManager* bpm_{nullptr};
        page_id_t root_page_id_{INVALID_PAGE_ID};
        page_id_t header_page_id_{INVALID_PAGE_ID};
        mutable std::shared_mutex tree_latch_;
    };

} // namespace storage
} // namespace HaruhiDB
