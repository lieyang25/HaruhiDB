/**
 * CXX/src/storage/page/b_plus_tree_internal_page.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 BPlusTreeInternalPage 的内部页逻辑。
 *
 * 主要完成：
 *
 * 1. 内部页初始化
 * 2. left-most child 读写
 * 3. key / child 查找
 * 4. 新根构造
 * 5. 插入与删除 child
 * 6. 分裂时半页搬移
 * 7. 合并时整页搬移
 * 8. 兄弟页再分配
 *
 *
 * ========================= 核心机制 =========================
 *
 * Lookup:
 *   对分隔键做 upper_bound 风格二分查找
 *   再返回对应 child
 *
 * InsertAfter:
 *   先找到 old_child 的逻辑位置
 *   再把 [new_key, new_child] 插入其后
 *
 * RemoveChildAt:
 *   child_index == 0 表示删除 left-most child
 *   其他位置对应删除数组中的某个映射项
 *
 * MoveHalfTo / MoveAllTo:
 *   用于分裂与合并
 *
 * MoveFirstToEndOf / MoveLastToFrontOf:
 *   用于兄弟页再分配
 */

#include "storage/page/b_plus_tree_internal_page.h"

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
    bool BPlusTreeInternalPage::InitForNewInternal(
        uint16_t max_size,
        page_id_t parent_page_id) noexcept
    {
        // step 1: 检查底层 page 与 page_id 是否有效。
        if (GetPage() == nullptr) {
            return false;
        }

        const page_id_t page_id = GetPage()->PageId();
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

        // step 3: 初始化公共 B+Tree 页头，并清空 left-most child。
        if (!tree_page_.InitForNewPage(page_id, PageType::INTERNAL, max_size, parent_page_id)) {
            return false;
        }

        SetLeftMostChild(INVALID_PAGE_ID);
        return true;
    }

    uint16_t BPlusTreeInternalPage::ComputeMaxSize() const noexcept
    {
        constexpr size_t body_offset = sizeof(PersistentHeader);

        if (body_offset >= PAGE_SIZE) {
            return 0;
        }

        return static_cast<uint16_t>((PAGE_SIZE - body_offset) / sizeof(MappingType));
    }

    page_id_t BPlusTreeInternalPage::GetLeftMostChild() const noexcept
    {
        return tree_page_.GetNodeLinkPageId();
    }

    void BPlusTreeInternalPage::SetLeftMostChild(page_id_t child_page_id) noexcept
    {
        tree_page_.SetNodeLinkPageId(child_page_id);
    }

    const BPlusTreeInternalPage::MappingType& BPlusTreeInternalPage::ItemAt(uint16_t index) const noexcept
    {
        const auto* array = Array();
        assert(array != nullptr);
        assert(index < GetSize());
        return array[index];
    }

    const BPlusTreeInternalPage::KeyType& BPlusTreeInternalPage::KeyAt(uint16_t index) const noexcept
    {
        return ItemAt(index).key;
    }

    bool BPlusTreeInternalPage::SetKeyAt(uint16_t index, const KeyType& key) noexcept
    {
        auto* array = Array();
        if (array == nullptr || index >= GetSize()) {
            return false;
        }

        array[index].key = key;
        GetPage()->MarkDirty();
        return true;
    }

    page_id_t BPlusTreeInternalPage::ChildAt(uint16_t index) const noexcept
    {
        return ItemAt(index).child_page_id;
    }

    /**
     * @param child_page_id 目标 child
     * @param out_index     成功时返回 child 下标
     */
    bool BPlusTreeInternalPage::FindChildIndex(
        page_id_t child_page_id, uint16_t* out_index) const noexcept
    {
        if (child_page_id == INVALID_PAGE_ID || out_index == nullptr) {
            return false;
        }

        if (GetLeftMostChild() == child_page_id) {
            *out_index = 0;
            return true;
        }

        const auto* array = Array();
        if (array == nullptr) {
            return false;
        }

        const uint16_t size = GetSize();
        for (uint16_t i = 0; i < size; ++i) {
            if (array[i].child_page_id == child_page_id) {
                *out_index = static_cast<uint16_t>(i + 1);
                return true;
            }
        }

        return false;
    }

    /**
     * @param key 查找键
     */
    page_id_t BPlusTreeInternalPage::Lookup(const KeyType& key) const noexcept
    {
        const uint16_t size = GetSize();
        if (size == 0) {
            return GetLeftMostChild();
        }

        const auto* array = Array();
        if (array == nullptr) {
            return INVALID_PAGE_ID;
        }

        // step 1: 对分隔键做 upper_bound 查找。
        uint16_t left = 0;
        uint16_t right = size;
        while (left < right) {
            const uint16_t mid = static_cast<uint16_t>(left + (right - left) / 2);
            if (array[mid].key <= key) {
                left = static_cast<uint16_t>(mid + 1);
            } else {
                right = mid;
            }
        }

        // step 2: 根据 upper_bound 结果选择目标 child。
        if (left == 0) {
            return GetLeftMostChild();
        }
        return array[left - 1].child_page_id;
    }

    /**
     * @param left_child  左子页
     * @param split_key   分裂键
     * @param right_child 右子页
     */
    bool BPlusTreeInternalPage::PopulateNewRoot(
        page_id_t left_child,
        const KeyType& split_key,
        page_id_t right_child) noexcept
    {
        // step 1: 检查当前页状态是否适合做新根。
        if (GetPage() == nullptr) {
            return false;
        }
        if (GetPageType() != PageType::INTERNAL) {
            return false;
        }
        if (left_child == INVALID_PAGE_ID || right_child == INVALID_PAGE_ID) {
            return false;
        }
        if (GetSize() != 0 || GetMaxSize() == 0) {
            return false;
        }

        auto* array = Array();
        if (array == nullptr) {
            return false;
        }

        // step 2: 组织为 [left-most child] + [split_key -> right_child]。
        SetLeftMostChild(left_child);
        array[0].key = split_key;
        array[0].child_page_id = right_child;
        SetSize(1);
        return true;
    }

    /**
     * @param old_child 原有 child
     * @param new_key   新分隔键
     * @param new_child 新 child
     */
    bool BPlusTreeInternalPage::InsertAfter(
        page_id_t old_child,
        const KeyType& new_key,
        page_id_t new_child) noexcept
    {
        // step 1: 检查基础状态与容量。
        if (GetPage() == nullptr) {
            return false;
        }
        if (old_child == INVALID_PAGE_ID || new_child == INVALID_PAGE_ID) {
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

        // step 2: 找到 old_child 的后继插入位置。
        uint16_t insert_idx = 0;
        if (old_child == GetLeftMostChild()) {
            insert_idx = 0;
        } else {
            bool found = false;
            for (uint16_t i = 0; i < size; ++i) {
                if (array[i].child_page_id == old_child) {
                    insert_idx = static_cast<uint16_t>(i + 1);
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }

        // step 3: 做局部有序性检查。
        if (insert_idx < size) {
            if (new_key > array[insert_idx].key) {
                return false;
            }
        }
        if (insert_idx > 0) {
            if (array[insert_idx - 1].key > new_key) {
                return false;
            }
        }

        // step 4: 整体后移并插入新映射项。
        if (insert_idx < size) {
            std::memmove(
                array + insert_idx + 1,
                array + insert_idx,
                sizeof(MappingType) * (size - insert_idx));
        }

        array[insert_idx].key = new_key;
        array[insert_idx].child_page_id = new_child;
        SetSize(static_cast<uint16_t>(size + 1));
        return true;
    }

    /**
     * @param child_index child 下标
     */
    bool BPlusTreeInternalPage::RemoveChildAt(uint16_t child_index) noexcept
    {
        // step 1: 检查当前页状态。
        if (GetPage() == nullptr) {
            return false;
        }

        const uint16_t size = GetSize();
        if (child_index > size) {
            return false;
        }

        auto* array = Array();
        if (array == nullptr && size > 0) {
            return false;
        }

        // step 2: 特殊处理空页。
        if (size == 0) {
            if (child_index != 0) {
                return false;
            }
            SetLeftMostChild(INVALID_PAGE_ID);
            return true;
        }

        // step 3: 删除 left-most child。
        if (child_index == 0) {
            SetLeftMostChild(array[0].child_page_id);
            if (size > 1) {
                std::memmove(
                    array,
                    array + 1,
                    sizeof(MappingType) * (size - 1));
            }
            std::memset(array + (size - 1), 0, sizeof(MappingType));
            SetSize(static_cast<uint16_t>(size - 1));
            return true;
        }

        // step 4: 删除数组中的某个映射项。
        const uint16_t mapping_idx = static_cast<uint16_t>(child_index - 1);
        if (mapping_idx >= size) {
            return false;
        }

        if (mapping_idx + 1 < size) {
            std::memmove(
                array + mapping_idx,
                array + mapping_idx + 1,
                sizeof(MappingType) * (size - mapping_idx - 1));
        }
        std::memset(array + (size - 1), 0, sizeof(MappingType));
        SetSize(static_cast<uint16_t>(size - 1));
        return true;
    }

    /**
     * @param recipient 目标内部页
     */
    void BPlusTreeInternalPage::MoveHalfTo(BPlusTreeInternalPage* recipient) noexcept
    {
        // step 1: 检查源页、目标页和基本状态。
        if (GetPage() == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return;
        }
        if (recipient == this) {
            return;
        }
        if (GetPageType() != PageType::INTERNAL || recipient->GetPageType() != PageType::INTERNAL) {
            return;
        }
        if (recipient->GetSize() != 0) {
            return;
        }

        const uint16_t size = GetSize();
        if (size == 0) {
            return;
        }

        // step 2: 计算后半部分映射项搬移范围。
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

        // step 3: 计算 recipient 的 left-most child。
        page_id_t recipient_leftmost = GetLeftMostChild();
        if (move_start > 0) {
            recipient_leftmost = src[move_start - 1].child_page_id;
        }
        if (recipient_leftmost == INVALID_PAGE_ID) {
            return;
        }

        // step 4: 搬移后半段数据并更新两页状态。
        std::memcpy(
            dst,
            src + move_start,
            sizeof(MappingType) * move_count);
        std::memset(src + move_start, 0, sizeof(MappingType) * move_count);

        recipient->SetLeftMostChild(recipient_leftmost);
        recipient->SetSize(move_count);
        SetSize(move_start);

        if (move_start == 0) {
            SetLeftMostChild(INVALID_PAGE_ID);
        }
    }

    /**
     * @param recipient          目标内部页
     * @param middle_key         父页下推键
     * @param out_new_middle_key 成功时返回新的上推键
     */
    bool BPlusTreeInternalPage::MoveFirstToEndOf(
        BPlusTreeInternalPage* recipient,
        const KeyType& middle_key,
        KeyType* out_new_middle_key) noexcept
    {
        // step 1: 检查源页、目标页与参数状态。
        if (GetPage() == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return false;
        }
        if (recipient == this || out_new_middle_key == nullptr) {
            return false;
        }
        if (GetPageType() != PageType::INTERNAL || recipient->GetPageType() != PageType::INTERNAL) {
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

        const page_id_t moved_child = GetLeftMostChild();
        if (moved_child == INVALID_PAGE_ID) {
            return false;
        }

        // step 2: 把父页下推键与当前 left-most child 追加到 recipient 尾部。
        dst[dst_size].key = middle_key;
        dst[dst_size].child_page_id = moved_child;

        // step 3: 当前页提升新的 middle key，并更新 left-most child。
        *out_new_middle_key = src[0].key;
        SetLeftMostChild(src[0].child_page_id);

        // step 4: 删掉源页最前面的映射项。
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
     * @param recipient          目标内部页
     * @param middle_key         父页下推键
     * @param out_new_middle_key 成功时返回新的上推键
     */
    bool BPlusTreeInternalPage::MoveLastToFrontOf(
        BPlusTreeInternalPage* recipient,
        const KeyType& middle_key,
        KeyType* out_new_middle_key) noexcept
    {
        // step 1: 检查源页、目标页与参数状态。
        if (GetPage() == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return false;
        }
        if (recipient == this || out_new_middle_key == nullptr) {
            return false;
        }
        if (GetPageType() != PageType::INTERNAL || recipient->GetPageType() != PageType::INTERNAL) {
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

        const page_id_t borrowed_child = src[src_size - 1].child_page_id;
        const KeyType borrowed_key = src[src_size - 1].key;
        if (borrowed_child == INVALID_PAGE_ID) {
            return false;
        }

        // step 2: recipient 整体后移，把父页下推键插入头部。
        if (dst_size > 0) {
            std::memmove(
                dst + 1,
                dst,
                sizeof(MappingType) * dst_size);
        }
        dst[0].key = middle_key;
        dst[0].child_page_id = recipient->GetLeftMostChild();

        // step 3: 用借来的 child 替换 recipient 的 left-most child。
        recipient->SetLeftMostChild(borrowed_child);

        // step 4: 删除源页最后一个映射项，并把其 key 上推。
        std::memset(src + (src_size - 1), 0, sizeof(MappingType));

        recipient->SetSize(static_cast<uint16_t>(dst_size + 1));
        SetSize(static_cast<uint16_t>(src_size - 1));
        *out_new_middle_key = borrowed_key;
        return true;
    }

    /**
     * @param recipient  目标内部页
     * @param middle_key 父页下推键
     */
    void BPlusTreeInternalPage::MoveAllTo(
        BPlusTreeInternalPage* recipient,
        const KeyType& middle_key) noexcept
    {
        // step 1: 检查源页、目标页和容量状态。
        if (GetPage() == nullptr || recipient == nullptr || recipient->GetPage() == nullptr) {
            return;
        }
        if (recipient == this) {
            return;
        }
        if (GetPageType() != PageType::INTERNAL || recipient->GetPageType() != PageType::INTERNAL) {
            return;
        }

        const uint16_t src_size = GetSize();
        const uint16_t dst_size = recipient->GetSize();
        if (static_cast<uint32_t>(dst_size) + src_size + 1 > recipient->GetMaxSize()) {
            return;
        }

        auto* src = Array();
        auto* dst = recipient->Array();
        if (src == nullptr || dst == nullptr) {
            return;
        }

        const page_id_t src_leftmost = GetLeftMostChild();
        if (src_leftmost == INVALID_PAGE_ID) {
            return;
        }

        // step 2: 先把父页下推键与 src 的 left-most child 追加到 recipient。
        dst[dst_size].key = middle_key;
        dst[dst_size].child_page_id = src_leftmost;

        // step 3: 再把 src 的全部映射项整体搬到 recipient 尾部。
        if (src_size > 0) {
            std::memcpy(
                dst + dst_size + 1,
                src,
                sizeof(MappingType) * src_size);
            std::memset(src, 0, sizeof(MappingType) * src_size);
        }

        // step 4: 清空源页逻辑内容。
        recipient->SetSize(static_cast<uint16_t>(dst_size + src_size + 1));
        SetLeftMostChild(INVALID_PAGE_ID);
        SetSize(0);
    }

    BPlusTreeInternalPage::MappingType* BPlusTreeInternalPage::Array() noexcept
    {
        if (GetPage() == nullptr) {
            return nullptr;
        }

        auto* raw = GetPage()->RawData();
        return reinterpret_cast<MappingType*>(raw + sizeof(PersistentHeader));
    }

    const BPlusTreeInternalPage::MappingType* BPlusTreeInternalPage::Array() const noexcept
    {
        if (GetPage() == nullptr) {
            return nullptr;
        }

        const auto* raw = GetPage()->RawData();
        return reinterpret_cast<const MappingType*>(raw + sizeof(PersistentHeader));
    }

} // namespace storage
} // namespace HaruhiDB