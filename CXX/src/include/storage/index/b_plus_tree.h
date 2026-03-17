/**
 * CXX/src/include/storage/index/b_plus_tree.h
 *
 * ========================= 设计目标 =========================
 *
 * BPlusTree 是当前系统中的整数键 B+Tree 索引实现。
 *
 * 它负责管理整棵 B+Tree 的逻辑结构，
 * 对外提供查找、插入、删除与顺序扫描能力。
 *
 * 核心职责：
 *
 * 1. 管理 root_page_id
 * 2. 管理 header_page 元数据
 * 3. 根据 key 查找叶子页
 * 4. 完成插入与分裂上传
 * 5. 完成删除与下溢调整
 * 6. 提供索引迭代入口
 *
 *
 * ========================= 为什么需要 BPlusTree =========================
 *
 * 单个叶子页或内部页只能表示局部节点。
 *
 * 但真正的索引操作需要解决：
 *
 * - 根页在哪里
 * - 如何从根一路下降到叶子
 * - 节点分裂后如何插入父节点
 * - 删除后如何借键或合并
 * - 如何从最左叶子开始顺序遍历
 *
 * 这些都是整棵树层面的职责，
 * 需要由 BPlusTree 统一组织。
 *
 *
 * ========================= BPlusTree 在系统中的位置 =========================
 *
 * TableInfo / Catalog
 *   └── BPlusTree
 *         ├── header page
 *         ├── internal page
 *         ├── leaf page
 *         └── IndexIterator
 *
 *
 * ========================= 组织形式 =========================
 *
 *   header_page
 *       │
 *       └── root_page_id
 *               │
 *               ▼
 *          +----------+
 *          |  root    |
 *          +----------+
 *            /      \
 *           /        \
 *   +----------+   +----------+
 *   | internal |   |  leaf    |
 *   +----------+   +----------+
 *
 * 叶子页之间还通过 next_page_id 串成链表，
 * 用于范围扫描与顺序遍历。
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
        /**
         * 创建一个新的 B+Tree。
         *
         * @param bpm 缓冲池管理器
         */
        explicit BPlusTree(buffer::BufferPoolManager* bpm);

        /**
         * 从已有 header page 加载 B+Tree。
         *
         * @param bpm            缓冲池管理器
         * @param header_page_id header page id
         */
        BPlusTree(buffer::BufferPoolManager* bpm, page_id_t header_page_id);

        /**
         * 判断当前树是否为空。
         */
        bool IsEmpty() const noexcept;

        /**
         * 查找 key 对应的 RID。
         *
         * @param key     目标键
         * @param out_rid 成功时返回对应 RID
         * @return 找到返回 true
         */
        bool GetValue(int32_t key, record::RID* out_rid);

        /**
         * 插入一个 key -> RID。
         *
         * @param key 键
         * @param rid 值
         * @return 成功返回 true
         */
        bool Insert(int32_t key, const record::RID& rid);

        /**
         * 删除指定 key。
         *
         * @param key 目标键
         * @return 成功返回 true
         */
        bool Remove(int32_t key);

        /**
         * 返回最左叶子页起始迭代器。
         */
        IndexIterator Begin();

        /**
         * 返回不小于 key 的起始迭代器。
         *
         * @param key 起始键
         */
        IndexIterator Begin(int32_t key);

        /**
         * 返回 end 迭代器。
         */
        IndexIterator End() const noexcept;

        /**
         * 返回根页页号。
         */
        page_id_t RootPageId() const noexcept;

        /**
         * 返回 header page 页号。
         */
        page_id_t HeaderPageId() const noexcept;

    private:
        /**
         * 从根开始下降，查找 key 应落到的叶子页。
         *
         * @param key 目标键
         * @return 成功返回叶子页 page id
         */
        page_id_t FindLeafPage(int32_t key);

        /**
         * 从根开始下降，查找最左叶子页。
         *
         * @return 成功返回最左叶子页 page id
         */
        page_id_t FindLeftMostLeafPage();

        /**
         * 当树为空时创建第一棵树。
         *
         * @param key 键
         * @param rid 值
         * @return 成功返回 true
         */
        bool StartNewTree(int32_t key, const record::RID& rid);

        /**
         * 把分裂结果插入父节点。
         *
         * @param old_page_id 原节点页号
         * @param split_key   分裂键
         * @param new_page_id 新节点页号
         * @return 成功返回 true
         */
        bool InsertIntoParent(
            page_id_t old_page_id,
            int32_t split_key,
            page_id_t new_page_id);

        /**
         * 更新某个 child 的父指针。
         *
         * @param child_page_id  子页页号
         * @param parent_page_id 父页页号
         * @return 成功返回 true
         */
        bool SetChildParent(page_id_t child_page_id, page_id_t parent_page_id);

        /**
         * 批量更新一个内部页下所有 child 的父指针。
         *
         * @param internal_page    目标内部页
         * @param internal_page_id 该内部页页号
         * @return 成功返回 true
         */
        bool UpdateInternalChildrenParent(
            BPlusTreeInternalPage* internal_page,
            page_id_t internal_page_id);

        /**
         * 初始化或加载 header page。
         *
         * @param header_page_id_hint 给定的 header page，若无则创建
         * @return 成功返回 true
         */
        bool InitOrLoadHeaderPage(page_id_t header_page_id_hint);

        /**
         * 将当前 root_page_id_ 写回 header page。
         *
         * @note 调用方需已持有 tree_latch_
         */
        bool PersistRootPageIdLocked();

        /**
         * 删除后对下溢节点做重平衡。
         *
         * @param page_id 失衡节点页号
         * @return 成功返回 true
         */
        bool RebalanceAfterDelete(page_id_t page_id);

        /**
         * 删除后调整根节点。
         *
         * @param root_page_id 当前根页
         * @return 成功返回 true
         */
        bool AdjustRootAfterDelete(page_id_t root_page_id);

    private:
        buffer::BufferPoolManager* bpm_{nullptr};
        page_id_t root_page_id_{INVALID_PAGE_ID};
        page_id_t header_page_id_{INVALID_PAGE_ID};

        /// 保护整棵树的结构变更
        mutable std::shared_mutex tree_latch_;
    };

} // namespace storage
} // namespace HaruhiDB