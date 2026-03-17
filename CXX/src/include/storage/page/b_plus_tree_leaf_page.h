/**
 * CXX/src/include/storage/page/b_plus_tree_leaf_page.h
 *
 * ========================= 设计目标 =========================
 *
 * BPlusTreeLeafPage 用于表示 B+Tree 的叶子页。
 *
 * 它建立在 BPlusTreePage 之上，
 * 负责管理叶子页中的有序键值对数组以及叶子链表指针。
 *
 * 核心职责：
 *
 * 1. 管理叶子页 next_page_id
 * 2. 维护有序的 key -> RID 映射
 * 3. 支持查找 / 插入 / 删除
 * 4. 支持分裂与合并时的数据搬移
 * 5. 支持兄弟页间借键
 *
 *
 * ========================= 为什么需要 BPlusTreeLeafPage =========================
 *
 * 在 B+Tree 中：
 *
 * - 内部页负责导航
 * - 叶子页负责真正存放键值记录
 *
 * 因此叶子页不仅要维护有序数组，
 * 还要维护叶子页之间的链表关系，
 * 以支持范围扫描和顺序遍历。
 *
 *
 * ========================= BPlusTreeLeafPage 在系统中的位置 =========================
 *
 * Page
 *   └── BPlusTreePage
 *         └── BPlusTreeLeafPage
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
 * - BPlusTreePage::opaque 保存公共 B+Tree 页头（含 next_page_id）
 * - MappingType 数组按 key 有序排列
 */

#pragma once

#include "storage/page/b_plus_tree_page.h"
#include "storage/record/rid.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HaruhiDB
{
namespace storage
{
    class BPlusTreeLeafPage : public BPlusTreePage
    {
    public:
        using KeyType = int32_t;
        using ValueType = record::RID;

        /**
         * 叶子页中的键值映射项。
         */
        struct MappingType
        {
            KeyType key;
            ValueType value;
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
        explicit BPlusTreeLeafPage(Page* page)
            : BPlusTreePage(page)
        {
        }

        /**
         * 将当前页面初始化为新的叶子页。
         *
         * @param max_size       最大容量
         * @param parent_page_id 父页页号
         * @return 成功返回 true
         */
        bool InitForNewLeaf(
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        /**
         * 根据物理页面空间计算叶子页最大容量。
         */
        uint16_t ComputeMaxSize() const noexcept;

        /**
         * 返回下一叶子页页号。
         */
        page_id_t GetNextPageId() const noexcept;

        /**
         * 设置下一叶子页页号。
         *
         * @param next_page_id 下一叶子页页号
         */
        void SetNextPageId(page_id_t next_page_id) noexcept;

        /**
         * 返回指定位置的映射项。
         *
         * @param index 下标
         */
        const MappingType& ItemAt(uint16_t index) const noexcept;

        /**
         * 返回指定位置的 key。
         *
         * @param index 下标
         */
        const KeyType& KeyAt(uint16_t index) const noexcept;

        /**
         * 返回指定位置的 value。
         *
         * @param index 下标
         */
        const ValueType& ValueAt(uint16_t index) const noexcept;

        /**
         * 返回 key 的下界位置。
         *
         * @param key 目标键
         */
        uint16_t KeyIndex(const KeyType& key) const noexcept;

        /**
         * 查找指定 key。
         *
         * @param key       目标键
         * @param out_value 成功时返回对应 RID
         * @return 找到返回 true
         */
        bool Lookup(const KeyType& key, ValueType* out_value) const noexcept;

        /**
         * 插入一个新的 key -> value 映射。
         *
         * @param key   键
         * @param value 值
         * @return 成功返回 true
         */
        bool Insert(const KeyType& key, const ValueType& value) noexcept;

        /**
         * 删除指定 key。
         *
         * @param key 目标键
         * @return 成功返回 true
         */
        bool Remove(const KeyType& key) noexcept;

        /**
         * 把后半部分元素移动到 recipient。
         *
         * @param recipient 目标叶子页
         */
        void MoveHalfTo(BPlusTreeLeafPage* recipient) noexcept;

        /**
         * 把全部元素移动到 recipient。
         *
         * @param recipient 目标叶子页
         */
        void MoveAllTo(BPlusTreeLeafPage* recipient) noexcept;

        /**
         * 把第一个元素移动到 recipient 尾部。
         *
         * @param recipient 目标叶子页
         * @return 成功返回 true
         */
        bool MoveFirstToEndOf(BPlusTreeLeafPage* recipient) noexcept;

        /**
         * 把最后一个元素移动到 recipient 头部。
         *
         * @param recipient 目标叶子页
         * @return 成功返回 true
         */
        bool MoveLastToFrontOf(BPlusTreeLeafPage* recipient) noexcept;

    private:
        /**
         * 返回映射数组首地址。
         */
        MappingType* Array() noexcept;

        /**
         * 返回映射数组首地址。
         */
        const MappingType* Array() const noexcept;
    };

} // namespace storage
} // namespace HaruhiDB
