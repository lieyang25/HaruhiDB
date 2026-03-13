/**
 * CXX/src/storage/page/b_plus_tree_leaf_page.cxx
 */

#include "storage/page/b_plus_tree_leaf_page.h"

#include <cassert>
#include <cstring>

namespace HaruhiDB
{
namespace storage
{

    bool BPlusTreeLeafPage::InitForNewLeaf(
        uint16_t max_size,
        page_id_t parent_page_id) noexcept
    {
        if (page_ == nullptr) {
            return false;
        }

        // page_id is expected to be assigned by BufferPoolManager::NewPage.
        const page_id_t page_id = page_->PageId();
        if (page_id == INVALID_PAGE_ID) {
            return false;
        }

        const uint16_t physical_max_size = ComputeMaxSize();
        if (physical_max_size == 0) {
            return false;
        }
        if (max_size == 0 || max_size > physical_max_size) {
            max_size = physical_max_size;
        }

        if (!InitForNewPage(page_id, PageType::LEAF, max_size, parent_page_id)) {
            return false;
        }

        auto* leaf = LeafHeader();
        if (leaf == nullptr) {
            return false;
        }

        leaf->next_page_id = INVALID_PAGE_ID;
        page_->MarkDirty();
        return true;
    }

    uint16_t BPlusTreeLeafPage::ComputeMaxSize() const noexcept
    {
        constexpr size_t body_offset =
            sizeof(PersistentHeader) + sizeof(BPlusTreeLeafExtraHeader);

        if (body_offset >= PAGE_SIZE) {
            return 0;
        }

        return static_cast<uint16_t>((PAGE_SIZE - body_offset) / sizeof(MappingType));
    }

    page_id_t BPlusTreeLeafPage::GetNextPageId() const noexcept
    {
        const auto* header = LeafHeader();
        return header == nullptr ? INVALID_PAGE_ID : header->next_page_id;
    }

    void BPlusTreeLeafPage::SetNextPageId(page_id_t next_page_id) noexcept
    {
        auto* header = LeafHeader();
        if (header == nullptr) {
            return;
        }

        header->next_page_id = next_page_id;
        page_->MarkDirty();
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

    bool BPlusTreeLeafPage::Insert(const KeyType& key, const ValueType& value) noexcept
    {
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

        const uint16_t idx = KeyIndex(key);
        if (idx < size && array[idx].key == key) {
            return false;
        }

        if (idx < size) {
            std::memmove(
                array + idx + 1,
                array + idx,
                sizeof(MappingType) * (size - idx));
        }

        array[idx].key = key;
        array[idx].value = value;

        SetSize(static_cast<uint16_t>(size + 1));
        return true;
    }

    bool BPlusTreeLeafPage::Remove(const KeyType& key) noexcept
    {
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

        const uint16_t idx = KeyIndex(key);
        if (idx >= size || array[idx].key != key) {
            return false;
        }

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

    void BPlusTreeLeafPage::MoveHalfTo(BPlusTreeLeafPage* recipient) noexcept
    {
        // Caller must hold write latches for both source and recipient pages.
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

        std::memcpy(
            dst,
            src + move_start,
            sizeof(MappingType) * move_count);
        std::memset(src + move_start, 0, sizeof(MappingType) * move_count);

        recipient->SetSize(move_count);
        SetSize(move_start);

        recipient->SetNextPageId(GetNextPageId());
        SetNextPageId(recipient->GetPageId());
    }

    void BPlusTreeLeafPage::MoveAllTo(BPlusTreeLeafPage* recipient) noexcept
    {
        // Caller must hold write latches for both source and recipient pages.
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

        std::memcpy(
            dst + dst_size,
            src,
            sizeof(MappingType) * src_size);
        std::memset(src, 0, sizeof(MappingType) * src_size);

        recipient->SetSize(static_cast<uint16_t>(dst_size + src_size));
        SetSize(0);
        recipient->SetNextPageId(GetNextPageId());
        SetNextPageId(INVALID_PAGE_ID);
    }

    bool BPlusTreeLeafPage::MoveFirstToEndOf(BPlusTreeLeafPage* recipient) noexcept
    {
        // Caller must hold write latches for both source and recipient pages.
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

    bool BPlusTreeLeafPage::MoveLastToFrontOf(BPlusTreeLeafPage* recipient) noexcept
    {
        // Caller must hold write latches for both source and recipient pages.
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

    BPlusTreeLeafExtraHeader* BPlusTreeLeafPage::LeafHeader() noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        auto* raw = page_->RawData();
        return reinterpret_cast<BPlusTreeLeafExtraHeader*>(raw + sizeof(PersistentHeader));
    }

    const BPlusTreeLeafExtraHeader* BPlusTreeLeafPage::LeafHeader() const noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        const auto* raw = page_->RawData();
        return reinterpret_cast<const BPlusTreeLeafExtraHeader*>(raw + sizeof(PersistentHeader));
    }

    BPlusTreeLeafPage::MappingType* BPlusTreeLeafPage::Array() noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        auto* raw = page_->RawData();
        return reinterpret_cast<MappingType*>(
            raw + sizeof(PersistentHeader) + sizeof(BPlusTreeLeafExtraHeader));
    }

    const BPlusTreeLeafPage::MappingType* BPlusTreeLeafPage::Array() const noexcept
    {
        if (page_ == nullptr) {
            return nullptr;
        }

        const auto* raw = page_->RawData();
        return reinterpret_cast<const MappingType*>(
            raw + sizeof(PersistentHeader) + sizeof(BPlusTreeLeafExtraHeader));
    }

} // namespace storage
} // namespace HaruhiDB
