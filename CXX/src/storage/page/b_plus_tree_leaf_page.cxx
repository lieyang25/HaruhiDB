/**
 * CXX/src/storage/page/b_plus_tree_leaf_page.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 BPlusTreeLeafPage 的叶子页逻辑。
 *
 * 主要完成：
 *
 * 1. 叶子页初始化
 * 2. next_page_id 读写
 * 3. 有序数组查找
 * 4. 插入与删除
 * 5. 分裂时半页搬移
 * 6. 合并时整页搬移
 * 7. 兄弟页间借键
 *
 *
 * ========================= 核心机制 =========================
 *
 * KeyIndex:
 *   对有序数组做 lower_bound 风格二分查找
 *
 * Insert:
 *   先定位插入位置
 *   再整体后移
 *   最后写入新项
 *
 * Remove:
 *   先定位目标项
 *   再整体前移
 *
 * MoveHalfTo / MoveAllTo:
 *   用于分裂与合并场景
 *
 * MoveFirstToEndOf / MoveLastToFrontOf:
 *   用于兄弟页再分配场景
 */

#include "storage/page/b_plus_tree_leaf_page.h"

#include <cassert>
#include <cstring>

namespace HaruhiDB
{
namespace storage
{

    /**
     * @param max_size       最大容量
     * @param parent_page_id 父页页号
     */
    bool BPlusTreeLeafPage::InitForNewLeaf(
        uint16_t max_size,
        page_id_t parent_page_id) noexcept
    {
        // step 1: 检查底层 page 与 page_id 是否有效。
        if (page_ == nullptr) {
            return false;
        }

        const page_id_t page_id = page_->PageId();
        if (page_id == INVALID_PAGE_ID) {
            return false;
        }

        // step 2: 根据物理页面能力修正 max_size。
        const uint16_t physical_max_size = ComputeMaxSize();
        if (physical_max_size == 0) {
            return false;
        }
        if (max_size == 0 || max_size > physical_max_size) {
            max_size = physical_max_size;
        }

        // step 3: 初始化公共 B+Tree 页头。
        if (!InitForNewPage(page_id, PageType::LEAF, max_size, parent_page_id)) {
            return false;
        }

        // step 4: 初始化叶子链表指针。
        SetNextPageId(INVALID_PAGE_ID);
        return true;
    }

    uint16_t BPlusTreeLeafPage::ComputeMaxSize() const noexcept
    {
        constexpr size_t body_offset = sizeof(PersistentHeader);

        if (body_offset >= PAGE_SIZE) {
            return 0;
        }

        return static_cast<uint16_t>((PAGE_SIZE - body_offset) / sizeof(MappingType));
    }

    page_id_t BPlusTreeLeafPage::GetNextPageId() const noexcept
    {
        return GetNodeLinkPageId();
    }

    void BPlusTreeLeafPage::SetNextPageId(page_id_t next_page_id) noexcept
    {
        SetNodeLinkPageId(next_page_id);
    }

    const BPlusTreeLeafPage::MappingType& BPlusTreeLeafPage::ItemAt(uint16_t index) const noexcept
    {
        const auto* array = Array();
        assert(array != nullptr);
        assert(index < GetSize());
        return array[index];
    }

    const BPlusTreeLeafPage::KeyType& BPlusTreeLeafPage::KeyAt(uint16_t index) const noexcept
    {
        return ItemAt(index).key;
    }

    const BPlusTreeLeafPage::ValueType& BPlusTreeLeafPage::ValueAt(uint16_t index) const noexcept
    {
        return ItemAt(index).value;
    }

    /**
     * @param key 目标键
     */
    uint16_t BPlusTreeLeafPage::KeyIndex(const KeyType& key) const noexcept
    {
        const auto* array = Array();
        if (array == nullptr) {
            return 0;
        }

        uint16_t left = 0;
        uint16_t right = GetSize();

        while (left < right) {
            const uint16_t mid = static_cast<uint16_t>(left + (right - left) / 2);
            if (array[mid].key < key) {
                left = static_cast<uint16_t>(mid + 1);
            } else {
                right = mid;
            }
        }
        return left;
    }

    /**
     * @param key       目标键
     * @param out_value 成功时返回对应 RID
     */
    bool BPlusTreeLeafPage::Lookup(const KeyType& key, ValueType* out_value) const noexcept
    {
        if (out_value == nullptr) {
            return false;
        }

        const uint16_t size = GetSize();
        if (size == 0) {
            return false;
        }

        const uint16_t idx = KeyIndex(key);
        if (idx >= size) {
            return false;
        }

        const auto* array = Array();
        if (array == nullptr) {
            return false;
        }

        if (array[idx].key != key) {
            return false;
        }

        *out_value = array[idx].value;
        return true;
    }

    /**
     * @param key   键
     * @param value 值
     */
    bool BPlusTreeLeafPage::Insert(const KeyType& key, const ValueType& value) noexcept
    {
        // step 1: 检查当前页与容量是否合法。
        if (page_ == nullptr) {
            return false;
        }

        const uint16_t size = GetSize();
        const uint16_t max_size = GetMaxSize();
        if (size >= max_size) {
            return false;
        }

        auto* array = Array();
        if (array == nullptr) {
            return false;
        }

        // step 2: 找到插入位置，并拒绝重复 key。
        const uint16_t idx = KeyIndex(key);
        if (idx < size && array[idx].key == key) {
            return false;
        }

        // step 3: 为新元素腾出位置。
        if (idx < size) {
            std::memmove(
                array + idx + 1,
                array + idx,
                sizeof(MappingType) * (size - idx));
        }

        // step 4: 写入新元素并更新 size。
        array[idx].key = key;
        array[idx].value = value;

        SetSize(static_cast<uint16_t>(size + 1));
        return true;
    }

    /**
     * @param key 目标键
     */
    bool BPlusTreeLeafPage::Remove(const KeyType& key) noexcept
    {
        // step 1: 检查当前页与数组状态。
        if (page_ == nullptr) {
            return false;
        }

        const uint16_t size = GetSize();
        if (size == 0) {
            return false;
        }

        auto* array = Array();
        if (array == nullptr) {
            return false;
        }

        // step 2: 定位目标 key。
        const uint16_t idx = KeyIndex(key);
        if (idx >= size || array[idx].key != key) {
            return false;
        }

        // step 3: 删除目标项并整体前移。
        if (idx + 1 < size) {
            std::memmove(
                array + idx,
                array + idx + 1,
                sizeof(MappingType) * (size - idx - 1));
        }

        std::memset(array + (size - 1), 0, sizeof(MappingType));
        SetSize(static_cast<uint16_t>(size - 1));
        return true;
    }

    /**
     * @param recipient 目标叶子页
     */
    void BPlusTreeLeafPage::MoveHalfTo(BPlusTreeLeafPage* recipient) noexcept
    {
        // step 1: 检查源页、目标页和基本状态。
        if (page_ == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return;
        }
        if (recipient == this) {
            return;
        }
        if (GetPageType() != PageType::LEAF || recipient->GetPageType() != PageType::LEAF) {
            return;
        }
        if (recipient->GetSize() != 0) {
            return;
        }

        const uint16_t size = GetSize();
        if (size == 0) {
            return;
        }

        // step 2: 计算后半部分搬移范围。
        const uint16_t move_start = static_cast<uint16_t>(size / 2);
        const uint16_t move_count = static_cast<uint16_t>(size - move_start);
        if (move_count == 0 || recipient->GetMaxSize() < move_count) {
            return;
        }

        auto* src = Array();
        auto* dst = recipient->Array();
        if (src == nullptr || dst == nullptr) {
            return;
        }
        if (recipient->GetPageId() == INVALID_PAGE_ID) {
            return;
        }

        // step 3: 复制后半段到目标页，并清理源页尾部。
        std::memcpy(
            dst,
            src + move_start,
            sizeof(MappingType) * move_count);
        std::memset(src + move_start, 0, sizeof(MappingType) * move_count);

        recipient->SetSize(move_count);
        SetSize(move_start);

        // step 4: 维护叶子链表。
        recipient->SetNextPageId(GetNextPageId());
        SetNextPageId(recipient->GetPageId());
    }

    /**
     * @param recipient 目标叶子页
     */
    void BPlusTreeLeafPage::MoveAllTo(BPlusTreeLeafPage* recipient) noexcept
    {
        // step 1: 检查源页、目标页和基本状态。
        if (page_ == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return;
        }
        if (recipient == this) {
            return;
        }
        if (GetPageType() != PageType::LEAF || recipient->GetPageType() != PageType::LEAF) {
            return;
        }

        const uint16_t src_size = GetSize();
        if (src_size == 0) {
            recipient->SetNextPageId(GetNextPageId());
            return;
        }

        const uint16_t dst_size = recipient->GetSize();
        if (static_cast<uint32_t>(dst_size) + src_size > recipient->GetMaxSize()) {
            return;
        }

        auto* src = Array();
        auto* dst = recipient->Array();
        if (src == nullptr || dst == nullptr) {
            return;
        }

        // step 2: 把源页全部内容追加到目标页末尾。
        std::memcpy(
            dst + dst_size,
            src,
            sizeof(MappingType) * src_size);
        std::memset(src, 0, sizeof(MappingType) * src_size);

        recipient->SetSize(static_cast<uint16_t>(dst_size + src_size));
        SetSize(0);

        // step 3: 维护叶子链表。
        recipient->SetNextPageId(GetNextPageId());
        SetNextPageId(INVALID_PAGE_ID);
    }

    /**
     * @param recipient 目标叶子页
     */
    bool BPlusTreeLeafPage::MoveFirstToEndOf(BPlusTreeLeafPage* recipient) noexcept
    {
        // step 1: 检查源页、目标页和容量状态。
        if (page_ == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return false;
        }
        if (recipient == this) {
            return false;
        }
        if (GetPageType() != PageType::LEAF || recipient->GetPageType() != PageType::LEAF) {
            return false;
        }

        const uint16_t src_size = GetSize();
        const uint16_t dst_size = recipient->GetSize();
        if (src_size == 0 || dst_size >= recipient->GetMaxSize()) {
            return false;
        }

        auto* src = Array();
        auto* dst = recipient->Array();
        if (src == nullptr || dst == nullptr) {
            return false;
        }

        // step 2: 把源页首元素追加到目标页尾部，并回收源页首位。
        dst[dst_size] = src[0];
        if (src_size > 1) {
            std::memmove(
                src,
                src + 1,
                sizeof(MappingType) * (src_size - 1));
        }
        std::memset(src + (src_size - 1), 0, sizeof(MappingType));

        recipient->SetSize(static_cast<uint16_t>(dst_size + 1));
        SetSize(static_cast<uint16_t>(src_size - 1));
        return true;
    }

    /**
     * @param recipient 目标叶子页
     */
    bool BPlusTreeLeafPage::MoveLastToFrontOf(BPlusTreeLeafPage* recipient) noexcept
    {
        // step 1: 检查源页、目标页和容量状态。
        if (page_ == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return false;
        }
        if (recipient == this) {
            return false;
        }
        if (GetPageType() != PageType::LEAF || recipient->GetPageType() != PageType::LEAF) {
            return false;
        }

        const uint16_t src_size = GetSize();
        const uint16_t dst_size = recipient->GetSize();
        if (src_size == 0 || dst_size >= recipient->GetMaxSize()) {
            return false;
        }

        auto* src = Array();
        auto* dst = recipient->Array();
        if (src == nullptr || dst == nullptr) {
            return false;
        }

        // step 2: 目标页整体后移，把源页尾元素插入到目标页头部。
        if (dst_size > 0) {
            std::memmove(
                dst + 1,
                dst,
                sizeof(MappingType) * dst_size);
        }
        dst[0] = src[src_size - 1];
        std::memset(src + (src_size - 1), 0, sizeof(MappingType));

        recipient->SetSize(static_cast<uint16_t>(dst_size + 1));
        SetSize(static_cast<uint16_t>(src_size - 1));
        return true;
    }

    BPlusTreeLeafPage::MappingType* BPlusTreeLeafPage::Array() noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        auto* raw = page_->RawData();
        return reinterpret_cast<MappingType*>(raw + sizeof(PersistentHeader));
    }

    const BPlusTreeLeafPage::MappingType* BPlusTreeLeafPage::Array() const noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        const auto* raw = page_->RawData();
        return reinterpret_cast<const MappingType*>(raw + sizeof(PersistentHeader));
    }

} // namespace storage
} // namespace HaruhiDB
