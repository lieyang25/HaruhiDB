/**
 * CXX/src/include/storage/page/b_plus_tree_leaf_page.h
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

    struct BPlusTreeLeafExtraHeader
    {
        page_id_t next_page_id{INVALID_PAGE_ID};
    };

    static_assert(std::is_trivially_copyable_v<BPlusTreeLeafExtraHeader>);
    static_assert((sizeof(PersistentHeader) % alignof(BPlusTreeLeafExtraHeader)) == 0);

    class BPlusTreeLeafPage : public BPlusTreePage
    {
    public:
        using KeyType = int32_t;
        using ValueType = record::RID;

        struct MappingType
        {
            KeyType key;
            ValueType value;
        };

        static_assert(std::is_trivially_copyable_v<MappingType>);
        static_assert(
            ((sizeof(PersistentHeader) + sizeof(BPlusTreeLeafExtraHeader)) % alignof(MappingType)) == 0);

    public:
        // Concurrency contract:
        // Callers must hold appropriate page latches before mutating/reading the
        // page content. This wrapper does not acquire latches internally.
        explicit BPlusTreeLeafPage(Page* page)
            : BPlusTreePage(page)
        {
        }

        bool InitForNewLeaf(
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        uint16_t ComputeMaxSize() const noexcept;

        page_id_t GetNextPageId() const noexcept;
        void SetNextPageId(page_id_t next_page_id) noexcept;

        const MappingType& ItemAt(uint16_t index) const noexcept;
        const KeyType& KeyAt(uint16_t index) const noexcept;
        const ValueType& ValueAt(uint16_t index) const noexcept;

        uint16_t KeyIndex(const KeyType& key) const noexcept;
        bool Lookup(const KeyType& key, ValueType* out_value) const noexcept;
        bool Insert(const KeyType& key, const ValueType& value) noexcept;
        bool Remove(const KeyType& key) noexcept;

        void MoveHalfTo(BPlusTreeLeafPage* recipient) noexcept;
        void MoveAllTo(BPlusTreeLeafPage* recipient) noexcept;
        bool MoveFirstToEndOf(BPlusTreeLeafPage* recipient) noexcept;
        bool MoveLastToFrontOf(BPlusTreeLeafPage* recipient) noexcept;

    private:
        BPlusTreeLeafExtraHeader* LeafHeader() noexcept;
        const BPlusTreeLeafExtraHeader* LeafHeader() const noexcept;

        MappingType* Array() noexcept;
        const MappingType* Array() const noexcept;
    };

} // namespace storage
} // namespace HaruhiDB
