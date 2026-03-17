/**
 * CXX/src/storage/index/b_plus_tree.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 BPlusTree 的整树逻辑。
 *
 * 主要完成：
 *
 * 1. header page 初始化与加载
 * 2. 根页管理
 * 3. 查找叶子页
 * 4. 查询
 * 5. 插入与分裂上传
 * 6. 删除与重平衡
 * 7. 迭代器起点定位
 *
 *
 * ========================= 核心流程 =========================
 *
 * Insert:
 *   查找目标叶子页
 *   若未满则直接插入
 *   若已满则分裂叶子页
 *   再把 split_key 上传到父节点
 *
 * Remove:
 *   查找目标叶子页
 *   删除 key
 *   若发生下溢则执行借键或合并
 *   必要时调整根页
 *
 * FindLeafPage:
 *   从 root 开始
 *   在内部页中根据 key 路由
 *   直到落到叶子页
 */

#include "storage/index/b_plus_tree.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace HaruhiDB
{
namespace storage
{
    namespace
    {
        /**
         * header page 的 opaque 区域元数据。
         */
        struct BPlusTreeMetaOpaque
        {
            uint32_t magic;
            uint32_t version;
            page_id_t root_page_id;
            uint32_t reserved;
        };

        constexpr uint32_t BPTREE_META_MAGIC = 0x42505448;   // "BPTH"
        constexpr uint32_t BPTREE_META_VERSION = 1;

        static_assert(sizeof(BPlusTreeMetaOpaque) == PAGE_HEADER_OPAQUE_SIZE);
    } // namespace

    /**
     * @param bpm 缓冲池管理器
     */
    BPlusTree::BPlusTree(buffer::BufferPoolManager* bpm)
        : bpm_(bpm)
    {
        if (bpm_ != nullptr) {
            (void)InitOrLoadHeaderPage(INVALID_PAGE_ID);
        }
    }

    /**
     * @param bpm            缓冲池管理器
     * @param header_page_id header page id
     */
    BPlusTree::BPlusTree(buffer::BufferPoolManager* bpm, page_id_t header_page_id)
        : bpm_(bpm)
    {
        if (bpm_ == nullptr) {
            return;
        }

        (void)InitOrLoadHeaderPage(header_page_id);
    }

    bool BPlusTree::IsEmpty() const noexcept
    {
        std::shared_lock lock(tree_latch_);
        return root_page_id_ == INVALID_PAGE_ID;
    }

    page_id_t BPlusTree::RootPageId() const noexcept
    {
        std::shared_lock lock(tree_latch_);
        return root_page_id_;
    }

    page_id_t BPlusTree::HeaderPageId() const noexcept
    {
        std::shared_lock lock(tree_latch_);
        return header_page_id_;
    }

    /**
     * @param key     目标键
     * @param out_rid 成功时返回对应 RID
     */
    bool BPlusTree::GetValue(int32_t key, record::RID* out_rid)
    {
        // step 1: 检查参数与树状态。
        if (out_rid == nullptr || bpm_ == nullptr) {
            return false;
        }

        std::shared_lock lock(tree_latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            return false;
        }

        // step 2: 找到目标叶子页。
        const page_id_t leaf_page_id = FindLeafPage(key);
        if (leaf_page_id == INVALID_PAGE_ID) {
            return false;
        }

        // step 3: 在叶子页内查找 key。
        auto page_exp = bpm_->FetchPage(leaf_page_id);
        if (!page_exp.has_value()) {
            return false;
        }

        Page* page = page_exp.value();
        page->RLock();
        BPlusTreeLeafPage leaf(page);
        const bool found = leaf.Lookup(key, out_rid);
        page->RUnLock();

        const bool unpinned = bpm_->UnpinPage(leaf_page_id, false);
        return found && unpinned;
    }

    IndexIterator BPlusTree::Begin()
    {
        std::shared_lock lock(tree_latch_);
        if (bpm_ == nullptr || root_page_id_ == INVALID_PAGE_ID) {
            return End();
        }

        const page_id_t first_leaf = FindLeftMostLeafPage();
        if (first_leaf == INVALID_PAGE_ID) {
            return End();
        }

        return IndexIterator(bpm_, first_leaf, 0);
    }

    /**
     * @param key 起始键
     */
    IndexIterator BPlusTree::Begin(int32_t key)
    {
        std::shared_lock lock(tree_latch_);
        if (bpm_ == nullptr || root_page_id_ == INVALID_PAGE_ID) {
            return End();
        }

        // step 1: 先找到 key 应落到的叶子页。
        page_id_t leaf_page_id = FindLeafPage(key);
        if (leaf_page_id == INVALID_PAGE_ID) {
            return End();
        }

        uint16_t start_index = 0;
        {
            // step 2: 在该叶子页中定位 lower_bound 起点。
            auto page_exp = bpm_->FetchPage(leaf_page_id);
            if (!page_exp.has_value()) {
                return End();
            }

            Page* page = page_exp.value();
            page->RLock();
            BPlusTreeLeafPage leaf(page);
            if (leaf.GetPageType() != PageType::LEAF) {
                page->RUnLock();
                bpm_->UnpinPage(leaf_page_id, false);
                return End();
            }

            start_index = leaf.KeyIndex(key);
            if (start_index >= leaf.GetSize()) {
                leaf_page_id = leaf.GetNextPageId();
                start_index = 0;
            }

            page->RUnLock();
            bpm_->UnpinPage(page->PageId(), false);
        }

        if (leaf_page_id == INVALID_PAGE_ID) {
            return End();
        }

        return IndexIterator(bpm_, leaf_page_id, start_index);
    }

    IndexIterator BPlusTree::End() const noexcept
    {
        return IndexIterator();
    }

    /**
     * @param key 键
     * @param rid 值
     */
    bool BPlusTree::Insert(int32_t key, const record::RID& rid)
    {
        if (bpm_ == nullptr) {
            return false;
        }

        std::unique_lock lock(tree_latch_);

        // step 1: 空树时直接创建第一棵树。
        if (root_page_id_ == INVALID_PAGE_ID) {
            return StartNewTree(key, rid);
        }

        // step 2: 找到目标叶子页。
        const page_id_t leaf_page_id = FindLeafPage(key);
        if (leaf_page_id == INVALID_PAGE_ID) {
            return false;
        }

        auto leaf_page_exp = bpm_->FetchPage(leaf_page_id);
        if (!leaf_page_exp.has_value()) {
            return false;
        }

        Page* leaf_page = leaf_page_exp.value();
        leaf_page->WLock();
        BPlusTreeLeafPage leaf(leaf_page);
        if (leaf.GetPageType() != PageType::LEAF) {
            leaf_page->WUnLock();
            bpm_->UnpinPage(leaf_page_id, false);
            return false;
        }

        // step 3: 拒绝重复 key。
        record::RID existing;
        if (leaf.Lookup(key, &existing)) {
            leaf_page->WUnLock();
            bpm_->UnpinPage(leaf_page_id, false);
            return false;
        }

        // step 4: 若叶子页未满，则直接插入。
        if (leaf.GetSize() < leaf.GetMaxSize()) {
            const bool inserted = leaf.Insert(key, rid);
            leaf_page->WUnLock();
            const bool unpinned = bpm_->UnpinPage(leaf_page_id, inserted);
            return inserted && unpinned;
        }

        // step 5: 若叶子页已满，则新建右兄弟并做分裂。
        page_id_t new_leaf_page_id = INVALID_PAGE_ID;
        auto new_leaf_page_exp = bpm_->NewPage(&new_leaf_page_id, PageType::LEAF);
        if (!new_leaf_page_exp.has_value()) {
            leaf_page->WUnLock();
            bpm_->UnpinPage(leaf_page_id, false);
            return false;
        }

        Page* new_leaf_page = new_leaf_page_exp.value();
        BPlusTreeLeafPage new_leaf(new_leaf_page);

        bool ok =
            new_leaf.InitForNewLeaf(leaf.GetMaxSize(), leaf.GetParentPageId());
        bool new_leaf_locked = false;
        if (ok) {
            new_leaf_page->WLock();
            new_leaf_locked = true;
            leaf.MoveHalfTo(&new_leaf);
            if (new_leaf.GetSize() == 0) {
                ok = false;
            }
        }

        // step 6: 把新记录插入分裂后的正确一侧。
        if (ok) {
            if (key >= new_leaf.KeyAt(0)) {
                ok = new_leaf.Insert(key, rid);
            } else {
                ok = leaf.Insert(key, rid);
            }
        }

        int32_t split_key = 0;
        if (ok) {
            split_key = new_leaf.KeyAt(0);
        }

        leaf_page->WUnLock();
        if (new_leaf_locked) {
            new_leaf_page->WUnLock();
        }

        const bool unpin_old = bpm_->UnpinPage(leaf_page_id, ok);
        const bool unpin_new = bpm_->UnpinPage(new_leaf_page_id, ok);
        if (!ok) {
            bpm_->DeletePage(new_leaf_page_id);
            return false;
        }
        if (!unpin_old || !unpin_new) {
            return false;
        }

        // step 7: 将 split_key 上传到父节点。
        return InsertIntoParent(leaf_page_id, split_key, new_leaf_page_id);
    }

    /**
     * @param key 目标键
     */
    bool BPlusTree::Remove(int32_t key)
    {
        if (bpm_ == nullptr) {
            return false;
        }

        std::unique_lock lock(tree_latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            return false;
        }

        // step 1: 找到目标叶子页。
        const page_id_t leaf_page_id = FindLeafPage(key);
        if (leaf_page_id == INVALID_PAGE_ID) {
            return false;
        }

        auto leaf_page_exp = bpm_->FetchPage(leaf_page_id);
        if (!leaf_page_exp.has_value()) {
            return false;
        }

        Page* leaf_page = leaf_page_exp.value();
        leaf_page->WLock();
        BPlusTreeLeafPage leaf(leaf_page);
        if (leaf.GetPageType() != PageType::LEAF) {
            leaf_page->WUnLock();
            bpm_->UnpinPage(leaf_page_id, false);
            return false;
        }

        // step 2: 删除 key。
        const bool removed = leaf.Remove(key);
        if (!removed) {
            leaf_page->WUnLock();
            bpm_->UnpinPage(leaf_page_id, false);
            return false;
        }

        // step 3: 若删除发生在根叶子页，特殊处理空树情况。
        if (leaf_page_id == root_page_id_) {
            const bool should_reset_root = leaf.GetSize() == 0;
            leaf_page->WUnLock();
            if (!bpm_->UnpinPage(leaf_page_id, true)) {
                return false;
            }

            if (should_reset_root) {
                root_page_id_ = INVALID_PAGE_ID;
                if (!PersistRootPageIdLocked()) {
                    return false;
                }
                if (!bpm_->DeletePage(leaf_page_id)) {
                    return false;
                }
            }
            return true;
        }

        // step 4: 非根页若下溢，则进入重平衡。
        const bool need_rebalance = leaf.GetSize() < leaf.GetMinSize();
        leaf_page->WUnLock();
        if (!bpm_->UnpinPage(leaf_page_id, true)) {
            return false;
        }

        if (!need_rebalance) {
            return true;
        }

        return RebalanceAfterDelete(leaf_page_id);
    }

    /**
     * @param key 目标键
     */
    page_id_t BPlusTree::FindLeafPage(int32_t key)
    {
        if (bpm_ == nullptr || root_page_id_ == INVALID_PAGE_ID) {
            return INVALID_PAGE_ID;
        }

        page_id_t current = root_page_id_;
        while (current != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(current);
            if (!page_exp.has_value()) {
                return INVALID_PAGE_ID;
            }

            Page* page = page_exp.value();
            page->RLock();
            BPlusTreePage tree_page(page);
            const PageType page_type = tree_page.GetPageType();

            // step 1: 若已到叶子页，则返回当前页号。
            if (page_type == PageType::LEAF) {
                page->RUnLock();
                if (!bpm_->UnpinPage(current, false)) {
                    return INVALID_PAGE_ID;
                }
                return current;
            }

            // step 2: 若不是内部页，说明树结构非法。
            if (page_type != PageType::INTERNAL) {
                page->RUnLock();
                bpm_->UnpinPage(current, false);
                return INVALID_PAGE_ID;
            }

            // step 3: 在内部页中路由到下一层 child。
            BPlusTreeInternalPage internal(page);
            const page_id_t next = internal.Lookup(key);
            page->RUnLock();
            if (!bpm_->UnpinPage(current, false)) {
                return INVALID_PAGE_ID;
            }
            if (next == INVALID_PAGE_ID) {
                return INVALID_PAGE_ID;
            }
            current = next;
        }

        return INVALID_PAGE_ID;
    }

    page_id_t BPlusTree::FindLeftMostLeafPage()
    {
        if (bpm_ == nullptr || root_page_id_ == INVALID_PAGE_ID) {
            return INVALID_PAGE_ID;
        }

        page_id_t current = root_page_id_;
        while (current != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(current);
            if (!page_exp.has_value()) {
                return INVALID_PAGE_ID;
            }

            Page* page = page_exp.value();
            page->RLock();
            BPlusTreePage node(page);
            const PageType type = node.GetPageType();

            // step 1: 若已到叶子页，则返回当前页号。
            if (type == PageType::LEAF) {
                page->RUnLock();
                if (!bpm_->UnpinPage(current, false)) {
                    return INVALID_PAGE_ID;
                }
                return current;
            }

            // step 2: 若不是内部页，说明树结构非法。
            if (type != PageType::INTERNAL) {
                page->RUnLock();
                bpm_->UnpinPage(current, false);
                return INVALID_PAGE_ID;
            }

            // step 3: 按 left-most child 一路下降。
            BPlusTreeInternalPage internal(page);
            const page_id_t next = internal.GetLeftMostChild();
            page->RUnLock();
            if (!bpm_->UnpinPage(current, false)) {
                return INVALID_PAGE_ID;
            }

            current = next;
        }

        return INVALID_PAGE_ID;
    }

    /**
     * @param key 键
     * @param rid 值
     */
    bool BPlusTree::StartNewTree(int32_t key, const record::RID& rid)
    {
        if (bpm_ == nullptr) {
            return false;
        }

        // step 1: 分配一个新的根叶子页。
        page_id_t root_page_id = INVALID_PAGE_ID;
        auto root_page_exp = bpm_->NewPage(&root_page_id, PageType::LEAF);
        if (!root_page_exp.has_value()) {
            return false;
        }

        // step 2: 初始化根叶子页并写入第一条记录。
        Page* root_page = root_page_exp.value();
        BPlusTreeLeafPage leaf(root_page);

        const uint16_t max_size = leaf.ComputeMaxSize();
        bool ok = leaf.InitForNewLeaf(max_size);
        if (ok) {
            root_page->WLock();
            ok = leaf.Insert(key, rid);
            root_page->WUnLock();
        }

        const bool unpinned = bpm_->UnpinPage(root_page_id, ok);
        if (!ok || !unpinned) {
            bpm_->DeletePage(root_page_id);
            return false;
        }

        // step 3: 更新根页号并持久化到 header page。
        root_page_id_ = root_page_id;
        return PersistRootPageIdLocked();
    }

    /**
     * @param old_page_id 原节点页号
     * @param split_key   分裂键
     * @param new_page_id 新节点页号
     */
    bool BPlusTree::InsertIntoParent(
        page_id_t old_page_id,
        int32_t split_key,
        page_id_t new_page_id)
    {
        if (bpm_ == nullptr ||
            old_page_id == INVALID_PAGE_ID ||
            new_page_id == INVALID_PAGE_ID) {
            return false;
        }

        // step 1: 先读取 old_page 的父页。
        auto old_page_exp = bpm_->FetchPage(old_page_id);
        if (!old_page_exp.has_value()) {
            return false;
        }

        Page* old_page = old_page_exp.value();
        old_page->RLock();
        BPlusTreePage old_node(old_page);
        const page_id_t parent_page_id = old_node.GetParentPageId();
        old_page->RUnLock();
        if (!bpm_->UnpinPage(old_page_id, false)) {
            return false;
        }

        // step 2: 若 old_page 原本无父节点，则创建新根。
        if (parent_page_id == INVALID_PAGE_ID) {
            page_id_t new_root_page_id = INVALID_PAGE_ID;
            auto new_root_page_exp = bpm_->NewPage(&new_root_page_id, PageType::INTERNAL);
            if (!new_root_page_exp.has_value()) {
                return false;
            }

            Page* new_root_page = new_root_page_exp.value();
            BPlusTreeInternalPage new_root(new_root_page);

            const uint16_t max_size = new_root.ComputeMaxSize();
            bool ok = new_root.InitForNewInternal(max_size);
            if (ok) {
                new_root_page->WLock();
                ok = new_root.PopulateNewRoot(old_page_id, split_key, new_page_id);
                new_root_page->WUnLock();
            }
            if (!ok) {
                bpm_->UnpinPage(new_root_page_id, false);
                bpm_->DeletePage(new_root_page_id);
                return false;
            }

            if (!SetChildParent(old_page_id, new_root_page_id)) {
                bpm_->UnpinPage(new_root_page_id, false);
                return false;
            }
            if (!SetChildParent(new_page_id, new_root_page_id)) {
                bpm_->UnpinPage(new_root_page_id, false);
                return false;
            }
            if (!bpm_->UnpinPage(new_root_page_id, true)) {
                return false;
            }

            root_page_id_ = new_root_page_id;
            return PersistRootPageIdLocked();
        }

        // step 3: 若父节点未满，则直接插入父节点。
        auto parent_page_exp = bpm_->FetchPage(parent_page_id);
        if (!parent_page_exp.has_value()) {
            return false;
        }
        Page* parent_page = parent_page_exp.value();
        parent_page->WLock();
        BPlusTreeInternalPage parent(parent_page);
        if (parent.GetPageType() != PageType::INTERNAL) {
            parent_page->WUnLock();
            bpm_->UnpinPage(parent_page_id, false);
            return false;
        }

        if (parent.GetSize() < parent.GetMaxSize()) {
            const bool inserted = parent.InsertAfter(old_page_id, split_key, new_page_id);
            parent_page->WUnLock();
            if (!bpm_->UnpinPage(parent_page_id, inserted)) {
                return false;
            }
            if (!inserted) {
                return false;
            }
            return SetChildParent(new_page_id, parent_page_id);
        }

        // step 4: 若父节点已满，则分裂父节点。
        page_id_t new_internal_page_id = INVALID_PAGE_ID;
        auto new_internal_page_exp = bpm_->NewPage(&new_internal_page_id, PageType::INTERNAL);
        if (!new_internal_page_exp.has_value()) {
            parent_page->WUnLock();
            bpm_->UnpinPage(parent_page_id, false);
            return false;
        }

        Page* new_internal_page = new_internal_page_exp.value();
        BPlusTreeInternalPage new_internal(new_internal_page);

        bool ok =
            new_internal.InitForNewInternal(parent.GetMaxSize(), parent.GetParentPageId());
        bool new_internal_locked = false;
        if (ok) {
            new_internal_page->WLock();
            new_internal_locked = true;
            parent.MoveHalfTo(&new_internal);
            ok = new_internal.GetSize() > 0;
        }
        if (ok) {
            ok = UpdateInternalChildrenParent(&new_internal, new_internal_page_id);
        }

        page_id_t holder_page_id = parent_page_id;
        if (ok) {
            if (parent.InsertAfter(old_page_id, split_key, new_page_id)) {
                holder_page_id = parent_page_id;
            } else if (new_internal.InsertAfter(old_page_id, split_key, new_page_id)) {
                holder_page_id = new_internal_page_id;
            } else {
                ok = false;
            }
        }

        int32_t promoted_key = 0;
        if (ok) {
            promoted_key = new_internal.KeyAt(0);
        }

        parent_page->WUnLock();
        if (new_internal_locked) {
            new_internal_page->WUnLock();
        }

        const bool unpin_parent = bpm_->UnpinPage(parent_page_id, ok);
        const bool unpin_new_internal = bpm_->UnpinPage(new_internal_page_id, ok);
        if (!ok) {
            bpm_->DeletePage(new_internal_page_id);
            return false;
        }
        if (!unpin_parent || !unpin_new_internal) {
            return false;
        }

        // step 5: 修正新 child 的父指针，并递归上传 promoted_key。
        if (!SetChildParent(new_page_id, holder_page_id)) {
            return false;
        }

        return InsertIntoParent(parent_page_id, promoted_key, new_internal_page_id);
    }

    /**
     * @param child_page_id  子页页号
     * @param parent_page_id 父页页号
     */
    bool BPlusTree::SetChildParent(page_id_t child_page_id, page_id_t parent_page_id)
    {
        if (bpm_ == nullptr || child_page_id == INVALID_PAGE_ID) {
            return false;
        }

        auto child_page_exp = bpm_->FetchPage(child_page_id);
        if (!child_page_exp.has_value()) {
            return false;
        }

        Page* child_page = child_page_exp.value();
        child_page->WLock();
        BPlusTreePage child(child_page);
        child.SetParentPageId(parent_page_id);
        child_page->WUnLock();
        return bpm_->UnpinPage(child_page_id, true);
    }

    /**
     * @param internal_page    目标内部页
     * @param internal_page_id 该内部页页号
     */
    bool BPlusTree::UpdateInternalChildrenParent(
        BPlusTreeInternalPage* internal_page,
        page_id_t internal_page_id)
    {
        if (internal_page == nullptr || internal_page_id == INVALID_PAGE_ID) {
            return false;
        }

        std::vector<page_id_t> children;
        children.reserve(static_cast<size_t>(internal_page->GetSize()) + 1);
        children.push_back(internal_page->GetLeftMostChild());
        for (uint16_t i = 0; i < internal_page->GetSize(); ++i) {
            children.push_back(internal_page->ChildAt(i));
        }

        for (page_id_t child_page_id : children) {
            if (child_page_id == INVALID_PAGE_ID) {
                return false;
            }
            if (!SetChildParent(child_page_id, internal_page_id)) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param header_page_id_hint 给定的 header page，若无则创建
     */
    bool BPlusTree::InitOrLoadHeaderPage(page_id_t header_page_id_hint)
    {
        if (bpm_ == nullptr) {
            return false;
        }

        // step 1: 若未给定 header page，则新建一个 header page。
        if (header_page_id_hint == INVALID_PAGE_ID) {
            page_id_t new_header_page_id = INVALID_PAGE_ID;
            auto new_page_exp = bpm_->NewPage(&new_header_page_id, PageType::HEADER);
            if (!new_page_exp.has_value()) {
                return false;
            }

            Page* header_page = new_page_exp.value();
            header_page->WLock();
            auto* persistent = header_page->Header();
            BPlusTreeMetaOpaque meta{
                .magic = BPTREE_META_MAGIC,
                .version = BPTREE_META_VERSION,
                .root_page_id = INVALID_PAGE_ID,
                .reserved = 0,
            };
            std::memcpy(persistent->opaque, &meta, sizeof(meta));
            header_page->MarkDirty();
            header_page->WUnLock();

            if (!bpm_->UnpinPage(new_header_page_id, true)) {
                return false;
            }

            header_page_id_ = new_header_page_id;
            root_page_id_ = INVALID_PAGE_ID;
            return true;
        }

        // step 2: 若给定了 header page，则加载并校验元数据。
        auto header_page_exp = bpm_->FetchPage(header_page_id_hint);
        if (!header_page_exp.has_value()) {
            return false;
        }

        Page* header_page = header_page_exp.value();
        header_page->WLock();
        auto* persistent = header_page->Header();
        if (persistent->page_type != PageType::HEADER) {
            header_page->WUnLock();
            bpm_->UnpinPage(header_page_id_hint, false);
            return false;
        }

        BPlusTreeMetaOpaque meta{};
        std::memcpy(&meta, persistent->opaque, sizeof(meta));
        if (meta.magic != BPTREE_META_MAGIC || meta.version != BPTREE_META_VERSION) {
            header_page->WUnLock();
            bpm_->UnpinPage(header_page_id_hint, false);
            return false;
        }

        header_page_id_ = header_page_id_hint;
        root_page_id_ = meta.root_page_id;

        header_page->WUnLock();
        return bpm_->UnpinPage(header_page_id_hint, false);
    }

    bool BPlusTree::PersistRootPageIdLocked()
    {
        if (bpm_ == nullptr) {
            return false;
        }

        if (header_page_id_ == INVALID_PAGE_ID) {
            return true;
        }

        auto header_page_exp = bpm_->FetchPage(header_page_id_);
        if (!header_page_exp.has_value()) {
            return false;
        }

        Page* header_page = header_page_exp.value();
        header_page->WLock();
        auto* persistent = header_page->Header();
        if (persistent->page_type != PageType::HEADER) {
            header_page->WUnLock();
            bpm_->UnpinPage(header_page_id_, false);
            return false;
        }

        BPlusTreeMetaOpaque meta{};
        std::memcpy(&meta, persistent->opaque, sizeof(meta));
        meta.magic = BPTREE_META_MAGIC;
        meta.version = BPTREE_META_VERSION;
        meta.root_page_id = root_page_id_;
        std::memcpy(persistent->opaque, &meta, sizeof(meta));
        header_page->MarkDirty();
        header_page->WUnLock();
        return bpm_->UnpinPage(header_page_id_, true);
    }

    /**
     * @param root_page_id 当前根页
     */
    bool BPlusTree::AdjustRootAfterDelete(page_id_t root_page_id)
    {
        if (bpm_ == nullptr || root_page_id == INVALID_PAGE_ID) {
            return true;
        }

        // step 1: 检查当前根页是否需要收缩。
        auto root_page_exp = bpm_->FetchPage(root_page_id);
        if (!root_page_exp.has_value()) {
            return false;
        }

        Page* root_page = root_page_exp.value();
        root_page->WLock();
        BPlusTreePage root(root_page);

        bool shrink_root = false;
        page_id_t new_root_page_id = root_page_id;

        if (root.GetPageType() == PageType::LEAF) {
            if (root.GetSize() == 0) {
                shrink_root = true;
                new_root_page_id = INVALID_PAGE_ID;
            }
        } else if (root.GetPageType() == PageType::INTERNAL) {
            BPlusTreeInternalPage internal_root(root_page);
            if (internal_root.GetSize() == 0) {
                shrink_root = true;
                new_root_page_id = internal_root.GetLeftMostChild();
            }
        } else {
            root_page->WUnLock();
            bpm_->UnpinPage(root_page_id, false);
            return false;
        }

        root_page->WUnLock();
        if (!bpm_->UnpinPage(root_page_id, false)) {
            return false;
        }

        if (!shrink_root) {
            return true;
        }

        // step 2: 若发生根收缩，则修正新根父指针。
        if (new_root_page_id != INVALID_PAGE_ID) {
            if (!SetChildParent(new_root_page_id, INVALID_PAGE_ID)) {
                return false;
            }
        }

        // step 3: 更新 root_page_id_，写回 header，并删除旧根。
        root_page_id_ = new_root_page_id;
        if (!PersistRootPageIdLocked()) {
            return false;
        }

        return bpm_->DeletePage(root_page_id);
    }

    /**
     * @param page_id 失衡节点页号
     */
    bool BPlusTree::RebalanceAfterDelete(page_id_t page_id)
    {
        if (bpm_ == nullptr || page_id == INVALID_PAGE_ID) {
            return false;
        }

        page_id_t current_page_id = page_id;
        while (current_page_id != INVALID_PAGE_ID) {
            // step 1: 若当前节点已成为根，则只需检查根收缩。
            if (current_page_id == root_page_id_) {
                return AdjustRootAfterDelete(current_page_id);
            }

            auto current_page_exp = bpm_->FetchPage(current_page_id);
            if (!current_page_exp.has_value()) {
                return false;
            }

            Page* current_page = current_page_exp.value();
            current_page->WLock();
            BPlusTreePage current_node(current_page);
            const PageType node_type = current_node.GetPageType();
            const page_id_t parent_page_id = current_node.GetParentPageId();

            if (parent_page_id == INVALID_PAGE_ID) {
                current_page->WUnLock();
                if (!bpm_->UnpinPage(current_page_id, false)) {
                    return false;
                }
                return AdjustRootAfterDelete(current_page_id);
            }

            if (node_type != PageType::LEAF && node_type != PageType::INTERNAL) {
                current_page->WUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return false;
            }

            if (current_node.GetSize() >= current_node.GetMinSize()) {
                current_page->WUnLock();
                return bpm_->UnpinPage(current_page_id, false);
            }

            // step 2: 取父页并定位当前节点在父页中的 child 下标。
            auto parent_page_exp = bpm_->FetchPage(parent_page_id);
            if (!parent_page_exp.has_value()) {
                current_page->WUnLock();
                bpm_->UnpinPage(current_page_id, false);
                return false;
            }

            Page* parent_page = parent_page_exp.value();
            parent_page->WLock();
            BPlusTreeInternalPage parent(parent_page);
            if (parent.GetPageType() != PageType::INTERNAL) {
                parent_page->WUnLock();
                current_page->WUnLock();
                bpm_->UnpinPage(parent_page_id, false);
                bpm_->UnpinPage(current_page_id, false);
                return false;
            }

            uint16_t current_index = 0;
            if (!parent.FindChildIndex(current_page_id, &current_index)) {
                parent_page->WUnLock();
                current_page->WUnLock();
                bpm_->UnpinPage(parent_page_id, false);
                bpm_->UnpinPage(current_page_id, false);
                return false;
            }

            auto child_at_index = [&parent](uint16_t child_index) -> page_id_t {
                if (child_index == 0) {
                    return parent.GetLeftMostChild();
                }
                return parent.ChildAt(static_cast<uint16_t>(child_index - 1));
            };

            const bool has_left = current_index > 0;
            const bool has_right = current_index < parent.GetSize();
            const page_id_t left_page_id = has_left
                ? child_at_index(static_cast<uint16_t>(current_index - 1))
                : INVALID_PAGE_ID;
            const page_id_t right_page_id = has_right
                ? child_at_index(static_cast<uint16_t>(current_index + 1))
                : INVALID_PAGE_ID;

            // step 3: 优先尝试向左兄弟借键。
            if (left_page_id != INVALID_PAGE_ID) {
                auto left_page_exp = bpm_->FetchPage(left_page_id);
                if (!left_page_exp.has_value()) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }

                Page* left_page = left_page_exp.value();
                left_page->WLock();
                BPlusTreePage left_node(left_page);

                bool borrowed = false;
                if (left_node.GetPageType() == node_type && left_node.GetSize() > left_node.GetMinSize()) {
                    if (node_type == PageType::LEAF) {
                        BPlusTreeLeafPage left_leaf(left_page);
                        BPlusTreeLeafPage current_leaf(current_page);
                        borrowed = left_leaf.MoveLastToFrontOf(&current_leaf);
                        if (borrowed) {
                            borrowed = parent.SetKeyAt(
                                static_cast<uint16_t>(current_index - 1),
                                current_leaf.KeyAt(0));
                        }
                    } else {
                        BPlusTreeInternalPage left_internal(left_page);
                        BPlusTreeInternalPage current_internal(current_page);
                        int32_t new_separator = 0;
                        borrowed = left_internal.MoveLastToFrontOf(
                            &current_internal,
                            parent.KeyAt(static_cast<uint16_t>(current_index - 1)),
                            &new_separator);
                        if (borrowed) {
                            borrowed = parent.SetKeyAt(
                                static_cast<uint16_t>(current_index - 1),
                                new_separator);
                        }
                        if (borrowed) {
                            borrowed = SetChildParent(
                                current_internal.GetLeftMostChild(),
                                current_page_id);
                        }
                    }
                }

                left_page->WUnLock();
                if (borrowed) {
                    parent_page->WUnLock();
                    current_page->WUnLock();

                    const bool unpin_left = bpm_->UnpinPage(left_page_id, true);
                    const bool unpin_parent = bpm_->UnpinPage(parent_page_id, true);
                    const bool unpin_current = bpm_->UnpinPage(current_page_id, true);
                    return unpin_left && unpin_parent && unpin_current;
                }

                if (!bpm_->UnpinPage(left_page_id, false)) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }
            }

            // step 4: 若左借失败，则尝试向右兄弟借键。
            if (right_page_id != INVALID_PAGE_ID) {
                auto right_page_exp = bpm_->FetchPage(right_page_id);
                if (!right_page_exp.has_value()) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }

                Page* right_page = right_page_exp.value();
                right_page->WLock();
                BPlusTreePage right_node(right_page);

                bool borrowed = false;
                if (right_node.GetPageType() == node_type && right_node.GetSize() > right_node.GetMinSize()) {
                    if (node_type == PageType::LEAF) {
                        BPlusTreeLeafPage right_leaf(right_page);
                        BPlusTreeLeafPage current_leaf(current_page);
                        borrowed = right_leaf.MoveFirstToEndOf(&current_leaf);
                        if (borrowed) {
                            borrowed = parent.SetKeyAt(current_index, right_leaf.KeyAt(0));
                        }
                    } else {
                        BPlusTreeInternalPage right_internal(right_page);
                        BPlusTreeInternalPage current_internal(current_page);
                        int32_t new_separator = 0;
                        borrowed = right_internal.MoveFirstToEndOf(
                            &current_internal,
                            parent.KeyAt(current_index),
                            &new_separator);
                        if (borrowed) {
                            borrowed = parent.SetKeyAt(current_index, new_separator);
                        }
                        if (borrowed && current_internal.GetSize() > 0) {
                            borrowed = SetChildParent(
                                current_internal.ChildAt(static_cast<uint16_t>(current_internal.GetSize() - 1)),
                                current_page_id);
                        }
                    }
                }

                right_page->WUnLock();
                if (borrowed) {
                    parent_page->WUnLock();
                    current_page->WUnLock();

                    const bool unpin_right = bpm_->UnpinPage(right_page_id, true);
                    const bool unpin_parent = bpm_->UnpinPage(parent_page_id, true);
                    const bool unpin_current = bpm_->UnpinPage(current_page_id, true);
                    return unpin_right && unpin_parent && unpin_current;
                }

                if (!bpm_->UnpinPage(right_page_id, false)) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }
            }

            // step 5: 若借键失败，则优先与左兄弟合并。
            if (left_page_id != INVALID_PAGE_ID) {
                auto left_page_exp = bpm_->FetchPage(left_page_id);
                if (!left_page_exp.has_value()) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }

                Page* left_page = left_page_exp.value();
                left_page->WLock();

                bool merged = false;
                if (node_type == PageType::LEAF) {
                    BPlusTreeLeafPage left_leaf(left_page);
                    BPlusTreeLeafPage current_leaf(current_page);
                    current_leaf.MoveAllTo(&left_leaf);
                    merged = parent.RemoveChildAt(current_index);
                } else {
                    BPlusTreeInternalPage left_internal(left_page);
                    BPlusTreeInternalPage current_internal(current_page);
                    const int32_t separator = parent.KeyAt(static_cast<uint16_t>(current_index - 1));
                    current_internal.MoveAllTo(&left_internal, separator);
                    merged = parent.RemoveChildAt(current_index);
                    if (merged) {
                        merged = UpdateInternalChildrenParent(&left_internal, left_page_id);
                    }
                }

                left_page->WUnLock();
                parent_page->WUnLock();
                current_page->WUnLock();

                const bool unpin_left = bpm_->UnpinPage(left_page_id, merged);
                const bool unpin_parent = bpm_->UnpinPage(parent_page_id, merged);
                const bool unpin_current = bpm_->UnpinPage(current_page_id, false);
                if (!merged || !unpin_left || !unpin_parent || !unpin_current) {
                    return false;
                }

                if (!bpm_->DeletePage(current_page_id)) {
                    return false;
                }

                current_page_id = parent_page_id;
                continue;
            }

            // step 6: 若无左兄弟，则把右兄弟并入当前页。
            if (right_page_id != INVALID_PAGE_ID) {
                auto right_page_exp = bpm_->FetchPage(right_page_id);
                if (!right_page_exp.has_value()) {
                    parent_page->WUnLock();
                    current_page->WUnLock();
                    bpm_->UnpinPage(parent_page_id, false);
                    bpm_->UnpinPage(current_page_id, false);
                    return false;
                }

                Page* right_page = right_page_exp.value();
                right_page->WLock();

                bool merged = false;
                if (node_type == PageType::LEAF) {
                    BPlusTreeLeafPage current_leaf(current_page);
                    BPlusTreeLeafPage right_leaf(right_page);
                    right_leaf.MoveAllTo(&current_leaf);
                    merged = parent.RemoveChildAt(static_cast<uint16_t>(current_index + 1));
                } else {
                    BPlusTreeInternalPage current_internal(current_page);
                    BPlusTreeInternalPage right_internal(right_page);
                    const int32_t separator = parent.KeyAt(current_index);
                    right_internal.MoveAllTo(&current_internal, separator);
                    merged = parent.RemoveChildAt(static_cast<uint16_t>(current_index + 1));
                    if (merged) {
                        merged = UpdateInternalChildrenParent(&current_internal, current_page_id);
                    }
                }

                right_page->WUnLock();
                parent_page->WUnLock();
                current_page->WUnLock();

                const bool unpin_right = bpm_->UnpinPage(right_page_id, false);
                const bool unpin_parent = bpm_->UnpinPage(parent_page_id, merged);
                const bool unpin_current = bpm_->UnpinPage(current_page_id, merged);
                if (!merged || !unpin_right || !unpin_parent || !unpin_current) {
                    return false;
                }

                if (!bpm_->DeletePage(right_page_id)) {
                    return false;
                }

                current_page_id = parent_page_id;
                continue;
            }

            parent_page->WUnLock();
            current_page->WUnLock();
            bpm_->UnpinPage(parent_page_id, false);
            bpm_->UnpinPage(current_page_id, false);
            return false;
        }

        return true;
    }

} // namespace storage
} // namespace HaruhiDB