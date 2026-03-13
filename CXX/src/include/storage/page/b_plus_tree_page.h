/**
 * CXX/src/include/storage/page/b_plus_tree_page.h
 */
#pragma once

#include "storage/page/page.h"
#include "common/config.h"

#include <cstddef>
#include <limits>

namespace HaruhiDB
{
namespace storage
{

    struct BPlusTreeOpaqueHeader
    {
        page_id_t parent_page_id{INVALID_PAGE_ID};
        uint16_t size{0};
        uint16_t max_size{0};
        uint8_t reserved[8]{};
    };

    static_assert(std::is_trivially_copyable_v<BPlusTreeOpaqueHeader>);
    static_assert(sizeof(BPlusTreeOpaqueHeader) == PAGE_HEADER_OPAQUE_SIZE);
    static_assert((offsetof(PersistentHeader, opaque) % alignof(BPlusTreeOpaqueHeader)) == 0);

    class BPlusTreePage
    {
    public:
        explicit BPlusTreePage(Page* page) : page_(page) {}

        Page* GetPage() noexcept { return page_; }
        const Page* GetPage() const noexcept { return page_; }

        bool InitForNewPage(
            page_id_t page_id,
            PageType page_type,
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        page_id_t GetPageId() const noexcept
        {
            return page_ == nullptr ? INVALID_PAGE_ID : page_->Header()->page_id;
        }

        PageType GetPageType() const noexcept
        {
            return page_ == nullptr ? PageType::INVALID : page_->Header()->page_type;
        }

        bool IsLeafPage() const noexcept
        {
            return GetPageType() == PageType::LEAF;
        }

        bool IsInternalPage() const noexcept
        {
            return GetPageType() == PageType::INTERNAL;
        }

        bool IsRootPage() const noexcept
        {
            return GetParentPageId() == INVALID_PAGE_ID;
        }

        page_id_t GetParentPageId() const noexcept;

        void SetParentPageId(page_id_t parent_page_id) noexcept;

        uint16_t GetSize() const noexcept;

        void SetSize(uint16_t size) noexcept;

        void IncreaseSize(int delta) noexcept;

        uint16_t GetMaxSize() const noexcept;

        void SetMaxSize(uint16_t max_size) noexcept;

        uint16_t GetMinSize() const noexcept
        {
            if (IsRootPage()) {
                return IsLeafPage() ? 1 : 2;
            }
            return GetMaxSize() / 2;
        }

    protected:
        BPlusTreeOpaqueHeader* OpaqueHeader() noexcept
        {
            if (page_ == nullptr) {
                return nullptr;
            }
            return reinterpret_cast<BPlusTreeOpaqueHeader*>(page_->Header()->opaque);
        }

        const BPlusTreeOpaqueHeader* OpaqueHeader() const noexcept
        {
            if (page_ == nullptr) {
                return nullptr;
            }
            return reinterpret_cast<const BPlusTreeOpaqueHeader*>(page_->Header()->opaque);
        }

        Page* page_{nullptr};
    };

} // namespace storage
} // namespace HaruhiDB
