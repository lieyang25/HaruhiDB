/**
 * CXX/src/include/storage/page/b_plus_tree_internal_page.h
 *
 * ========================= 设计目标 =========================
 *
 * BPlusTreeInternalPage 用于表示 B+Tree 的内部页。
 *
 * 它建立在 BPlusTreePage 之上，
 * 负责管理内部页中的分隔键与子页指针关系。
 *
 * 核心职责：
 *
 * 1. 维护 left-most child
 * 2. 维护有序的 key -> child_page_id 映射
 * 3. 根据 key 选择下降子页
 * 4. 支持插入与删除子指针
 * 5. 支持分裂、合并与兄弟页再分配
 *
 *
 * ========================= 为什么需要 BPlusTreeInternalPage =========================
 *
 * 在 B+Tree 中，内部页不保存真实记录，
 * 而是保存：
 *
 * - 用于路由查找的分隔键
 * - 指向子节点的页号
 *
 * 因此内部页的职责是“导航”，
 * 不是“存储最终键值”。
 *
 *
 * ========================= BPlusTreeInternalPage 在系统中的位置 =========================
 *
 * Page
 *   └── BPlusTreePage
 *         └── BPlusTreeInternalPage
 *
 *
 * ========================= 页面组织 =========================
 *
 *   +------------------------------+
 *   | PersistentHeader             |
 *   +------------------------------+
 *   | MappingType array            |
 *   +------------------------------+
 *
 * 其中：
 *
 * - BPlusTreePage::opaque 保存公共 B+Tree 页头
 * - left-most child 单独保存在公共页头的 link 字段中
 * - MappingType 数组保存 [key, child_page_id]
 *
 *
 * ========================= 当前语义 =========================
 *
 * child 序列逻辑上为：
 *
 *   [left-most child] [key0 -> child1] [key1 -> child2] ...
 *
 * 即：
 *
 * - child 数量 = key 数量 + 1
 * - 第 0 个 child 不在 MappingType 数组中
 * - 数组中的每一项表示“某个分隔键右侧的 child”
 */

#pragma once

#include "storage/page/b_plus_tree_page.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HaruhiDB
{
namespace storage
{

    class BPlusTreeInternalPage
    {
    public:
        using KeyType = int32_t;
        using ValueType = page_id_t;

        /**
         * 内部页中的键与右侧子页指针映射项。
         */
        struct MappingType
        {
            KeyType key;
            ValueType child_page_id;
        };

        static_assert(std::is_trivially_copyable_v<MappingType>);
        static_assert((sizeof(PersistentHeader) % alignof(MappingType)) == 0);

    public:
        /**
         * 并发约定：
         *
         * 调用方在读写页面内容前，
         * 应自行持有合适的 page latch。
         * 该包装层不主动加锁。
         *
         * @param page 底层通用页面对象
         */
        explicit BPlusTreeInternalPage(Page* page)
            : tree_page_(page)
        {
        }

        Page* GetPage() noexcept { return tree_page_.GetPage(); }
        const Page* GetPage() const noexcept { return tree_page_.GetPage(); }
        page_id_t GetPageId() const noexcept { return tree_page_.GetPageId(); }
        PageType GetPageType() const noexcept { return tree_page_.GetPageType(); }
        bool IsLeafPage() const noexcept { return tree_page_.IsLeafPage(); }
        bool IsInternalPage() const noexcept { return tree_page_.IsInternalPage(); }
        bool IsRootPage() const noexcept { return tree_page_.IsRootPage(); }
        page_id_t GetParentPageId() const noexcept { return tree_page_.GetParentPageId(); }
        void SetParentPageId(page_id_t parent_page_id) noexcept { tree_page_.SetParentPageId(parent_page_id); }
        uint16_t GetSize() const noexcept { return tree_page_.GetSize(); }
        void SetSize(uint16_t size) noexcept { tree_page_.SetSize(size); }
        void IncreaseSize(int delta) noexcept { tree_page_.IncreaseSize(delta); }
        uint16_t GetMaxSize() const noexcept { return tree_page_.GetMaxSize(); }
        void SetMaxSize(uint16_t max_size) noexcept { tree_page_.SetMaxSize(max_size); }
        uint16_t GetMinSize() const noexcept { return tree_page_.GetMinSize(); }

        /**
         * 将当前页面初始化为新的内部页。
         *
         * @param max_size       最大容量
         * @param parent_page_id 父页页号
         * @return 成功返回 true
         */
        bool InitForNewInternal(
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        /**
         * 根据物理页面空间计算内部页最大容量。
         */
        uint16_t ComputeMaxSize() const noexcept;

        /**
         * 返回最左子页页号。
         */
        page_id_t GetLeftMostChild() const noexcept;

        /**
         * 设置最左子页页号。
         *
         * @param child_page_id 子页页号
         */
        void SetLeftMostChild(page_id_t child_page_id) noexcept;

        /**
         * 返回指定位置的映射项。
         *
         * @param index 下标
         */
        const MappingType& ItemAt(uint16_t index) const noexcept;

        /**
         * 返回指定位置的分隔键。
         *
         * @param index 下标
         */
        const KeyType& KeyAt(uint16_t index) const noexcept;

        /**
         * 设置指定位置的分隔键。
         *
         * @param index 下标
         * @param key   新键值
         * @return 成功返回 true
         */
        bool SetKeyAt(uint16_t index, const KeyType& key) noexcept;

        /**
         * 返回指定位置映射项的右侧 child。
         *
         * @param index 下标
         */
        page_id_t ChildAt(uint16_t index) const noexcept;

        /**
         * 查找某个 child 在当前内部页中的逻辑位置。
         *
         * @param child_page_id 目标 child
         * @param out_index     成功时返回 child 下标
         * @return 找到返回 true
         */
        bool FindChildIndex(page_id_t child_page_id, uint16_t* out_index) const noexcept;

        /**
         * 根据 key 选择下降子页。
         *
         * @param key 查找键
         * @return 目标 child page id
         */
        page_id_t Lookup(const KeyType& key) const noexcept;

        /**
         * 把当前内部页初始化为新根页内容。
         *
         * @param left_child  左子页
         * @param split_key   分裂键
         * @param right_child 右子页
         * @return 成功返回 true
         */
        bool PopulateNewRoot(
            page_id_t left_child,
            const KeyType& split_key,
            page_id_t right_child) noexcept;

        /**
         * 在 old_child 之后插入新的 [key, child]。
         *
         * @param old_child 原有 child
         * @param new_key   新分隔键
         * @param new_child 新 child
         * @return 成功返回 true
         */
        bool InsertAfter(
            page_id_t old_child,
            const KeyType& new_key,
            page_id_t new_child) noexcept;

        /**
         * 删除指定逻辑 child。
         *
         * @param child_index child 下标
         * @return 成功返回 true
         */
        bool RemoveChildAt(uint16_t child_index) noexcept;

        /**
         * 把后半部分元素移动到 recipient。
         *
         * @param recipient 目标内部页
         */
        void MoveHalfTo(BPlusTreeInternalPage* recipient) noexcept;

        /**
         * 把第一个逻辑 child 移到 recipient 尾部。
         *
         * @param recipient          目标内部页
         * @param middle_key         父页下推键
         * @param out_new_middle_key 成功时返回新的上推键
         * @return 成功返回 true
         */
        bool MoveFirstToEndOf(
            BPlusTreeInternalPage* recipient,
            const KeyType& middle_key,
            KeyType* out_new_middle_key) noexcept;

        /**
         * 把最后一个逻辑 child 移到 recipient 头部。
         *
         * @param recipient          目标内部页
         * @param middle_key         父页下推键
         * @param out_new_middle_key 成功时返回新的上推键
         * @return 成功返回 true
         */
        bool MoveLastToFrontOf(
            BPlusTreeInternalPage* recipient,
            const KeyType& middle_key,
            KeyType* out_new_middle_key) noexcept;

        /**
         * 把全部元素移动到 recipient。
         *
         * @param recipient  目标内部页
         * @param middle_key 父页下推键
         */
        void MoveAllTo(BPlusTreeInternalPage* recipient, const KeyType& middle_key) noexcept;

    private:
        /**
         * 返回映射数组首地址。
         */
        MappingType* Array() noexcept;

        /**
         * 返回映射数组首地址。
         */
        const MappingType* Array() const noexcept;

    private:
        BPlusTreePage tree_page_;
    };

} // namespace storage
} // namespace HaruhiDB