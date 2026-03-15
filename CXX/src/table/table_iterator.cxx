/**
 * CXX/src/table/table_iterator.cxx
 */

#include "table/table_iterator.h"
#include "table/table_heap.h"

#include "storage/page/table_page.h"

#include <utility>
#include <shared_mutex>
#include <vector>

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
      at_end_(true),
      tuple_cached_(false)
{
}

TableIterator::TableIterator(TableHeap *heap, page_id_t start_page, slot_id_t start_slot)
    : heap_(heap),
      cur_page_id_(start_page),
      cur_slot_(start_slot),
      at_end_(false),
      tuple_cached_(false)
{
    if (heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
        at_end_ = true;
        return;
    }
    std::shared_lock lock(heap_->table_latch_);
    if (!AdvanceToNextValid()) {
        at_end_ = true;
    }
}

record::Tuple TableIterator::operator*() const
{
    if (at_end_ || heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
        return {};
    }

    if (tuple_cached_) {
        return record::Tuple(std::span<const std::byte>(tuple_buffer_.data(), tuple_buffer_.size()));
    }

    std::shared_lock lock(heap_->table_latch_);
    auto page_exp = heap_->bpm_->FetchPage(cur_page_id_);
    if (!page_exp.has_value()) {
        return {};
    }

    storage::Page *page = page_exp.value();
    const page_id_t pid_snapshot = cur_page_id_;
    page->RLock();
    auto guard = MakeScopeGuard([&]() {
        page->RUnLock();
        heap_->bpm_->UnpinPage(pid_snapshot, false);
    });

    storage::TablePage table_page(page);
    if (cur_slot_ >= table_page.SlotCount()) {
        return {};
    }
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
    tuple_cached_ = true;
    return record::Tuple(std::span<const std::byte>(tuple_buffer_.data(), tuple_buffer_.size()));
}

record::RID TableIterator::GetRID() const noexcept
{
    if (at_end_ || heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID || cur_slot_ == INVALID_SLOT_ID) {
        return {};
    }
    return record::RID(cur_page_id_, cur_slot_);
}

TableIterator &TableIterator::operator++()
{
    if (at_end_ || heap_ == nullptr) {
        return *this;
    }

    tuple_cached_ = false;
    std::shared_lock lock(heap_->table_latch_);
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

        auto page_exp = heap_->bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return false;
        }

        storage::Page *page = page_exp.value();
        const page_id_t pid_snapshot = pid;
        page->RLock();
        auto release = MakeScopeGuard([&]() {
            page->RUnLock();
            heap_->bpm_->UnpinPage(pid_snapshot, false);
        });

        storage::TablePage table_page(page);
        if (table_page.SlotCount() == 0 || table_page.AliveTupleCount() == 0) {
            pid = table_page.NextPageId();
            slot = 0;
            continue;
        }

        if (cur_slot_ >= table_page.SlotCount()) {
            pid = table_page.NextPageId();
            slot = 0;
            continue;
        }

        for (slot_id_t s = cur_slot_; s < table_page.SlotCount(); ++s) {
            if (!table_page.GetSlot(s)->IsDeleted()) {
                cur_page_id_ = pid;
                cur_slot_ = s;
                tuple_cached_ = false;
                return true;
            }
        }

        pid = table_page.NextPageId();
        slot = 0;
    }

    tuple_cached_ = false;
    return false;
}

} // namespace table
} // namespace HaruhiDB
