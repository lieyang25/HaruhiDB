/**
 * CXX/src/storage/page/b_plus_tree_page.cxx
 */

#include "storage/page/b_plus_tree_page.h"

#include <algorithm>

namespace HaruhiDB
{
namespace storage
{

    bool BPlusTreePage::InitForNewPage(
        page_id_t page_id,
        PageType page_type,
        uint16_t max_size,
        page_id_t parent_page_id) noexcept
    {
        if (page_ == nullptr) {
            return false;
        }
        if (page_type != PageType::LEAF && page_type != PageType::INTERNAL) {
            return false;
        }

        page_->WLock();
        auto* base_header = page_->Header();
        base_header->lsn = 0;
        base_header->page_id = page_id;
        base_header->page_type = page_type;
        std::fill(std::begin(base_header->reserved0), std::end(base_header->reserved0), 0);

        auto* opaque = OpaqueHeader();
        opaque->parent_page_id = parent_page_id;
        opaque->size = 0;
        opaque->max_size = max_size;
        std::fill(std::begin(opaque->reserved), std::end(opaque->reserved), 0);

        page_->MarkDirty();
        page_->WUnLock();
        return true;
    }

    page_id_t BPlusTreePage::GetParentPageId() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? INVALID_PAGE_ID : header->parent_page_id;
    }

    void BPlusTreePage::SetParentPageId(page_id_t parent_page_id) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->parent_page_id = parent_page_id;
        page_->MarkDirty();
    }

    uint16_t BPlusTreePage::GetSize() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? 0 : header->size;
    }

    void BPlusTreePage::SetSize(uint16_t size) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->size = size;
        page_->MarkDirty();
    }

    void BPlusTreePage::IncreaseSize(int delta) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        const int next = static_cast<int>(header->size) + delta;
        if (next < 0) {
            header->size = 0;
        } else if (next > static_cast<int>(std::numeric_limits<uint16_t>::max())) {
            header->size = std::numeric_limits<uint16_t>::max();
        } else {
            header->size = static_cast<uint16_t>(next);
        }
        page_->MarkDirty();
    }

    uint16_t BPlusTreePage::GetMaxSize() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? 0 : header->max_size;
    }

    void BPlusTreePage::SetMaxSize(uint16_t max_size) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->max_size = max_size;
        page_->MarkDirty();
    }

} // namespace storage
} // namespace HaruhiDB
