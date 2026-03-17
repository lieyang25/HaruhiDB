/**
 * CXX/src/include/storage/page/b_plus_tree_internal_page.h
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

        struct MappingType
        {
            KeyType key;
            ValueType child_page_id;
        };

        static_assert(std::is_trivially_copyable_v<MappingType>);
        static_assert((sizeof(PersistentHeader) % alignof(MappingType)) == 0);

    public:
        // Concurrency contract:
        // Callers must hold appropriate page latches before mutating/reading the
        // page content. This wrapper does not acquire latches internally.
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

        bool InitForNewInternal(
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        uint16_t ComputeMaxSize() const noexcept;

        page_id_t GetLeftMostChild() const noexcept;
        void SetLeftMostChild(page_id_t child_page_id) noexcept;

        const MappingType& ItemAt(uint16_t index) const noexcept;
        const KeyType& KeyAt(uint16_t index) const noexcept;
        bool SetKeyAt(uint16_t index, const KeyType& key) noexcept;
        page_id_t ChildAt(uint16_t index) const noexcept;
        bool FindChildIndex(page_id_t child_page_id, uint16_t* out_index) const noexcept;

        page_id_t Lookup(const KeyType& key) const noexcept;

        bool PopulateNewRoot(
            page_id_t left_child,
            const KeyType& split_key,
            page_id_t right_child) noexcept;

        bool InsertAfter(
            page_id_t old_child,
            const KeyType& new_key,
            page_id_t new_child) noexcept;

        bool RemoveChildAt(uint16_t child_index) noexcept;

        void MoveHalfTo(BPlusTreeInternalPage* recipient) noexcept;
        bool MoveFirstToEndOf(
            BPlusTreeInternalPage* recipient,
            const KeyType& middle_key,
            KeyType* out_new_middle_key) noexcept;
        bool MoveLastToFrontOf(
            BPlusTreeInternalPage* recipient,
            const KeyType& middle_key,
            KeyType* out_new_middle_key) noexcept;
        void MoveAllTo(BPlusTreeInternalPage* recipient, const KeyType& middle_key) noexcept;

    private:
        MappingType* Array() noexcept;
        const MappingType* Array() const noexcept;

    private:
        BPlusTreePage tree_page_;
    };

} // namespace storage
} // namespace HaruhiDB
