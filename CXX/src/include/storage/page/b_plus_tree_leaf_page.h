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
        explicit BPlusTreeLeafPage(Page* page)
            : BPlusTreePage(page)
        {
        }

        bool InitForNewLeaf(
            page_id_t page_id,
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        uint16_t ComputeMaxSize() const noexcept;

        page_id_t GetNextPageId() const noexcept;
        void SetNextPageId(page_id_t next_page_id) noexcept;

        const MappingType& ItemAt(uint16_t index) const noexcept;
        const KeyType& KeyAt(uint16_t index) const noexcept;
        const ValueType& ValueAt(uint16_t index) const noexcept;

        template <typename KeyComparator>
        uint16_t KeyIndex(const KeyType& key, const KeyComparator& comparator) const noexcept
        {
            return KeyIndexImpl(key, &CompareThunk<KeyComparator>, &comparator);
        }

        template <typename KeyComparator>
        bool Lookup(const KeyType& key, ValueType* out_value, const KeyComparator& comparator) const noexcept
        {
            return LookupImpl(key, out_value, &CompareThunk<KeyComparator>, &comparator);
        }

        template <typename KeyComparator>
        bool Insert(const KeyType& key, const ValueType& value, const KeyComparator& comparator) noexcept
        {
            return InsertImpl(key, value, &CompareThunk<KeyComparator>, &comparator);
        }

        void MoveHalfTo(BPlusTreeLeafPage* recipient) noexcept;

    private:
        using KeyComparatorFn = int (*)(const KeyType&, const KeyType&, const void*) noexcept;

        template <typename KeyComparator>
        static int CompareThunk(
            const KeyType& lhs,
            const KeyType& rhs,
            const void* comparator_ctx) noexcept
        {
            const auto* comparator = static_cast<const KeyComparator*>(comparator_ctx);
            return (*comparator)(lhs, rhs);
        }

        uint16_t KeyIndexImpl(
            const KeyType& key,
            KeyComparatorFn comparator,
            const void* comparator_ctx) const noexcept;

        bool LookupImpl(
            const KeyType& key,
            ValueType* out_value,
            KeyComparatorFn comparator,
            const void* comparator_ctx) const noexcept;

        bool InsertImpl(
            const KeyType& key,
            const ValueType& value,
            KeyComparatorFn comparator,
            const void* comparator_ctx) noexcept;

        BPlusTreeLeafExtraHeader* LeafHeader() noexcept;
        const BPlusTreeLeafExtraHeader* LeafHeader() const noexcept;

        MappingType* Array() noexcept;
        const MappingType* Array() const noexcept;
    };

} // namespace storage
} // namespace HaruhiDB
