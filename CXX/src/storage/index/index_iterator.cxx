/**
 * CXX/src/storage/index/index_iterator.cxx
 */

#include "storage/index/index_iterator.h"

#include "storage/page/b_plus_tree_leaf_page.h"

namespace HaruhiDB
{
namespace storage
{

    IndexIterator::IndexIterator()
    {
    }

    IndexIterator::IndexIterator(
        buffer::BufferPoolManager* bpm,
        page_id_t leaf_page_id,
        uint16_t index)
        : bpm_(bpm),
          leaf_page_id_(leaf_page_id),
          index_(index),
          at_end_(false)
    {
        if (bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            at_end_ = true;
            return;
        }

        if (!AdvanceToValid()) {
            at_end_ = true;
        }
    }

    IndexIterator::MappingType IndexIterator::operator*() const
    {
        if (at_end_ || bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            return {0, record::RID{}};
        }

        auto page_exp = bpm_->FetchPage(leaf_page_id_);
        if (!page_exp.has_value()) {
            return {0, record::RID{}};
        }

        Page* page = page_exp.value();
        page->RLock();
        BPlusTreeLeafPage leaf(page);
        if (leaf.GetPageType() != PageType::LEAF || index_ >= leaf.GetSize()) {
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            return {0, record::RID{}};
        }

        const auto item = leaf.ItemAt(index_);
        page->RUnLock();
        bpm_->UnpinPage(leaf_page_id_, false);
        return {item.key, item.value};
    }

    IndexIterator& IndexIterator::operator++()
    {
        if (at_end_ || bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            return *this;
        }

        auto page_exp = bpm_->FetchPage(leaf_page_id_);
        if (!page_exp.has_value()) {
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
            return *this;
        }

        Page* page = page_exp.value();
        page->RLock();
        BPlusTreeLeafPage leaf(page);
        if (leaf.GetPageType() != PageType::LEAF) {
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
            return *this;
        }

        if (index_ + 1 < leaf.GetSize()) {
            ++index_;
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            return *this;
        }

        leaf_page_id_ = leaf.GetNextPageId();
        index_ = 0;
        page->RUnLock();
        bpm_->UnpinPage(page->PageId(), false);

        if (leaf_page_id_ == INVALID_PAGE_ID || !AdvanceToValid()) {
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
        }
        return *this;
    }

    bool IndexIterator::operator==(const IndexIterator& other) const noexcept
    {
        if (at_end_ && other.at_end_) {
            return true;
        }

        return bpm_ == other.bpm_ &&
            leaf_page_id_ == other.leaf_page_id_ &&
            index_ == other.index_ &&
            at_end_ == other.at_end_;
    }

    bool IndexIterator::operator!=(const IndexIterator& other) const noexcept
    {
        return !(*this == other);
    }

    bool IndexIterator::AdvanceToValid()
    {
        if (bpm_ == nullptr) {
            return false;
        }

        while (leaf_page_id_ != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(leaf_page_id_);
            if (!page_exp.has_value()) {
                return false;
            }

            Page* page = page_exp.value();
            page->RLock();
            BPlusTreeLeafPage leaf(page);
            if (leaf.GetPageType() != PageType::LEAF) {
                page->RUnLock();
                bpm_->UnpinPage(leaf_page_id_, false);
                return false;
            }

            if (index_ < leaf.GetSize()) {
                page->RUnLock();
                bpm_->UnpinPage(leaf_page_id_, false);
                return true;
            }

            leaf_page_id_ = leaf.GetNextPageId();
            index_ = 0;
            page->RUnLock();
            bpm_->UnpinPage(page->PageId(), false);
        }

        return false;
    }

} // namespace storage
} // namespace HaruhiDB
