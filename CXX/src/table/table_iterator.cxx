/**
 * CXX/src/table/table_iterator.cxx
 */

#include "table/table_iterator.h"
#include "table/table_heap.h"

#include "storage/page/table_page.h"

#include <utility>

namespace HaruhiDB {
namespace table {

namespace {
    template <typename F>
    class ScopeGuard {
    public:
        explicit ScopeGuard(F&& fn)
            : fn_(std::forward<F>(fn)), active_(true)
        {
        }

        ScopeGuard(ScopeGuard&& other) noexcept
            : fn_(std::move(other.fn_)), active_(std::exchange(other.active_, false))
        {
        }

        ~ScopeGuard()
        {
            if (active_) {
                fn_();
            }
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        F fn_;
        bool active_;
    };

    template <typename F>
    ScopeGuard<F> MakeScopeGuard(F&& fn)
    {
        return ScopeGuard<F>(std::forward<F>(fn));
    }
} // namespace

TableIterator::TableIterator()
    : heap_(nullptr),
      cur_page_id_(INVALID_PAGE_ID),
      cur_slot_(INVALID_SLOT_ID),
      at_end_(true)
{
}

TableIterator::TableIterator(TableHeap *heap, page_id_t start_page, slot_id_t start_slot)
    : heap_(heap),
      cur_page_id_(start_page),
      cur_slot_(start_slot),
      at_end_(false)
{
    if (heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
        at_end_ = true;
        return;
    }
    if (!AdvanceToNextValid()) {
        at_end_ = true;
    }
}

record::Tuple TableIterator::operator*() const
{
    if (at_end_ || heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
        return {};
    }

    auto page_exp = heap_->bpm_->FetchPage(cur_page_id_);
    if (!page_exp.has_value()) {
        return {};
    }

    storage::Page *page = page_exp.value();
    page->RLock();
    auto guard = MakeScopeGuard([&]() {
        page->RUnLock();
        heap_->bpm_->UnpinPage(cur_page_id_, false);
    });

    const auto *header = page->Header();
    if (cur_slot_ >= header->slot_count) {
        return {};
    }

    storage::TablePage table_page(page);
    const storage::Slot *slot = table_page.GetSlot(cur_slot_);
    if (slot == nullptr || slot->IsDeleted()) {
        return {};
    }

    const uint16_t offset = slot->GetOffset();
    const uint16_t length = slot->GetLength();
    if (length == 0 || static_cast<size_t>(offset) + length > PAGE_SIZE) {
        return {};
    }

    const std::byte *begin = page->RawData() + offset;
    tuple_buffer_.assign(begin, begin + length);
    return record::Tuple(std::span<std::byte>(tuple_buffer_.data(), tuple_buffer_.size()));
}

TableIterator &TableIterator::operator++()
{
    if (at_end_ || heap_ == nullptr) {
        return *this;
    }

    if (cur_slot_ != INVALID_SLOT_ID) {
        cur_slot_ = static_cast<slot_id_t>(cur_slot_ + 1);
    }

    if (!AdvanceToNextValid()) {
        at_end_ = true;
    }

    return *this;
}

bool TableIterator::operator==(const TableIterator &other) const noexcept
{
    if (at_end_ && other.at_end_) {
        return true;
    }
    return heap_ == other.heap_ &&
        cur_page_id_ == other.cur_page_id_ &&
        cur_slot_ == other.cur_slot_ &&
        at_end_ == other.at_end_;
}

bool TableIterator::operator!=(const TableIterator &other) const noexcept
{
    return !(*this == other);
}

void TableIterator::EnsurePageLoaded() const
{
    if (heap_ == nullptr || at_end_ || cur_page_id_ == INVALID_PAGE_ID) {
        return;
    }

    if (cur_handle_.page != nullptr && cur_handle_.pid == cur_page_id_) {
        return;
    }

    if (cur_handle_.page != nullptr) {
        if (cur_handle_.locked) {
            cur_handle_.page->RUnLock();
        }
        heap_->bpm_->UnpinPage(cur_handle_.pid, false);
        cur_handle_ = {};
    }

    auto page_exp = heap_->bpm_->FetchPage(cur_page_id_);
    if (!page_exp.has_value()) {
        return;
    }
    cur_handle_.page = page_exp.value();
    cur_handle_.pid = cur_page_id_;
    cur_handle_.page->RLock();
    cur_handle_.locked = true;
}

bool TableIterator::AdvanceToNextValid()
{
    if (heap_ == nullptr) {
        return false;
    }

    page_id_t pid = cur_page_id_;
    slot_id_t slot = cur_slot_;

    while (pid != INVALID_PAGE_ID) {
        cur_page_id_ = pid;
        cur_slot_ = slot;

        EnsurePageLoaded();
        if (cur_handle_.page == nullptr) {
            return false;
        }

        auto release = MakeScopeGuard([&]() {
            if (cur_handle_.page != nullptr) {
                if (cur_handle_.locked) {
                    cur_handle_.page->RUnLock();
                }
                heap_->bpm_->UnpinPage(cur_handle_.pid, false);
                cur_handle_ = {};
            }
        });

        const auto *header = cur_handle_.page->Header();
        if (cur_slot_ >= header->slot_count) {
            pid = header->next_page_id;
            slot = 0;
            continue;
        }

        storage::TablePage table_page(cur_handle_.page);
        for (slot_id_t s = cur_slot_; s < header->slot_count; ++s) {
            if (!table_page.GetSlot(s)->IsDeleted()) {
                cur_page_id_ = pid;
                cur_slot_ = s;
                return true;
            }
        }

        pid = header->next_page_id;
        slot = 0;
    }

    return false;
}

} // namespace table
} // namespace HaruhiDB
