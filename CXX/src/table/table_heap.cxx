/**
 * CXX/src/table/table_heap.cxx
 */

#include "table/table_heap.h"
#include "table/table_iterator.h"

#include "storage/page/table_page.h"

#include <ranges>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace HaruhiDB {
namespace table {

namespace {
    constexpr uint32_t MaxTupleSize()
    {
        return static_cast<uint32_t>(
            PAGE_SIZE - sizeof(storage::PersistentHeader) - sizeof(storage::Slot));
    }

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

TableHeap::TableHeap(buffer::BufferPoolManager *bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id)
{
}

bool TableHeap::InsertTuple(const record::Tuple &tuple, record::RID *out_rid)
{
    if (bpm_ == nullptr) {
        return false;
    }

    const uint32_t tuple_size = tuple.Size();
    if (tuple_size == 0 || tuple_size > MaxTupleSize()) {
        return false;
    }

    const uint32_t need = tuple_size;

    auto try_insert = [&](page_id_t pid) -> std::expected<slot_id_t, storage::TablePageErr> {
        std::shared_lock lock(table_latch_);
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return std::unexpected(storage::TablePageErr{
                "TableHeap::InsertTuple: fetch page failed",
                storage::TablePageErrCode::NullPage});
        }

        storage::Page *page = page_exp.value();
        storage::TablePage table_page(page);
        auto res = table_page.InsertTuple(tuple);

        uint32_t free_space = 0;
        page->RLock();
        free_space = table_page.FreeSpace();
        page->RUnLock();
        UpdateFreeSpaceMap(pid, free_space);
        bpm_->UnpinPage(pid, res.has_value());
        return res;
    };

    std::optional<page_id_t> target = FindPageWithFreeSpace(need);
    if (!target.has_value()) {
        target = AllocateNewPage();
    }
    if (!target.has_value()) {
        return false;
    }

    auto res = try_insert(target.value());
    if (!res.has_value() &&
        res.error().err_code == storage::TablePageErrCode::InsufficientSpace) {
        target = AllocateNewPage();
        if (!target.has_value()) {
            return false;
        }
        res = try_insert(target.value());
    }

    if (!res.has_value()) {
        return false;
    }

    if (out_rid != nullptr) {
        out_rid->SetRID(target.value(), res.value());
    }
    return true;
}

bool TableHeap::GetTuple(const record::RID &rid, record::Tuple *out_tuple)
{
    if (bpm_ == nullptr || out_tuple == nullptr) {
        return false;
    }

    const page_id_t pid = rid.GetPageId();
    if (pid == INVALID_PAGE_ID) {
        return false;
    }

    std::shared_lock lock(table_latch_);
    auto page_exp = bpm_->FetchPage(pid);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    page->RLock();
    auto guard = MakeScopeGuard([&]() {
        page->RUnLock();
        bpm_->UnpinPage(pid, false);
    });

    const auto *header = page->Header();
    if (rid.GetSlotId() >= header->slot_count) {
        return false;
    }

    storage::TablePage table_page(page);
    const storage::Slot *slot = table_page.GetSlot(rid.GetSlotId());
    if (slot == nullptr || slot->IsDeleted()) {
        return false;
    }

    const uint16_t offset = slot->GetOffset();
    const uint16_t length = slot->GetLength();
    if (length == 0 || static_cast<size_t>(offset) + length > PAGE_SIZE) {
        return false;
    }

    const std::byte *begin = page->RawData() + offset;
    std::vector<std::byte> data(begin, begin + length);
    *out_tuple = record::Tuple(std::move(data));
    return true;
}

bool TableHeap::DeleteTuple(const record::RID &rid)
{
    if (bpm_ == nullptr) {
        return false;
    }

    const page_id_t pid = rid.GetPageId();
    if (pid == INVALID_PAGE_ID) {
        return false;
    }

    bool ok = false;
    {
        std::shared_lock lock(table_latch_);
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return false;
        }

        storage::Page *page = page_exp.value();
        storage::TablePage table_page(page);
        auto res = table_page.MarkDelTuple(rid.GetSlotId());

        uint32_t free_space = 0;
        page->RLock();
        free_space = table_page.FreeSpace();
        page->RUnLock();
        UpdateFreeSpaceMap(pid, free_space);

        bpm_->UnpinPage(pid, res.has_value());
        ok = res.has_value();
    }

    if (ok) {
        ReclaimPageIfEmpty(pid);
    }
    return ok;
}

bool TableHeap::UpdateTuple(
    const record::RID &rid, const record::Tuple &new_tuple, record::RID *out_rid)
{
    if (bpm_ == nullptr) {
        return false;
    }

    const page_id_t pid = rid.GetPageId();
    if (pid == INVALID_PAGE_ID) {
        return false;
    }

    bool updated_in_place = false;
    bool need_move = false;

    {
        std::shared_lock lock(table_latch_);
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return false;
        }

        storage::Page *page = page_exp.value();
        storage::TablePage table_page(page);
        auto res = table_page.UpdateTuple(rid.GetSlotId(), new_tuple);

        uint32_t free_space = 0;
        page->RLock();
        free_space = table_page.FreeSpace();
        page->RUnLock();
        UpdateFreeSpaceMap(pid, free_space);
        bpm_->UnpinPage(pid, res.has_value());

        if (res.has_value()) {
            updated_in_place = true;
        } else if (res.error().err_code == storage::TablePageErrCode::InsufficientSpace) {
            need_move = true;
        } else {
            return false;
        }
    }

    if (updated_in_place) {
        if (out_rid != nullptr) {
            out_rid->SetRID(rid.GetPageId(), rid.GetSlotId());
        }
        return true;
    }

    if (need_move) {
        if (out_rid == nullptr) {
            return false;
        }
        record::RID new_rid;
        if (!InsertTuple(new_tuple, &new_rid)) {
            return false;
        }
        if (!DeleteTuple(rid)) {
            DeleteTuple(new_rid);
            return false;
        }
        *out_rid = new_rid;
        return true;
    }

    return false;
}

TableIterator TableHeap::Begin()
{
    page_id_t start = INVALID_PAGE_ID;
    {
        std::shared_lock lock(table_latch_);
        start = first_page_id_;
    }
    if (start == INVALID_PAGE_ID) {
        return End();
    }
    return TableIterator(this, start, 0);
}

TableIterator TableHeap::End()
{
    return TableIterator();
}

std::optional<page_id_t> TableHeap::FindPageWithFreeSpace(uint32_t need)
{
    if (bpm_ == nullptr || need == 0) {
        return std::nullopt;
    }

    {
        std::scoped_lock lock(free_space_mutex_);
        auto it = std::ranges::find_if(
            free_space_map_,
            [need](const auto &entry) { return entry.second >= need; });
        if (it != free_space_map_.end()) {
            return it->first;
        }
    }

    std::shared_lock lock(table_latch_);
    page_id_t pid = first_page_id_;
    while (pid != INVALID_PAGE_ID) {
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return std::nullopt;
        }

        storage::Page *page = page_exp.value();
        uint32_t free_space = 0;
        page_id_t next_pid = INVALID_PAGE_ID;

        {
            page->RLock();
            auto guard = MakeScopeGuard([&]() {
                page->RUnLock();
                bpm_->UnpinPage(pid, false);
            });

            storage::TablePage table_page(page);
            free_space = table_page.FreeSpace();
            next_pid = page->Header()->next_page_id;
        }

        UpdateFreeSpaceMap(pid, free_space);
        if (free_space >= need) {
            return pid;
        }
        pid = next_pid;
    }

    return std::nullopt;
}

std::optional<page_id_t> TableHeap::AllocateNewPage()
{
    if (bpm_ == nullptr) {
        return std::nullopt;
    }

    page_id_t new_page_id = INVALID_PAGE_ID;
    auto page_exp = bpm_->NewPage(&new_page_id);
    if (!page_exp.has_value()) {
        return std::nullopt;
    }

    storage::Page *new_page = page_exp.value();
    {
        new_page->WLock();
        auto guard = MakeScopeGuard([&]() { new_page->WUnLock(); });
        new_page->Header()->next_page_id = INVALID_PAGE_ID;
        new_page->MarkDirty();
    }

    bool linked = false;
    {
        std::unique_lock lock(table_latch_);
        if (first_page_id_ == INVALID_PAGE_ID) {
            first_page_id_ = new_page_id;
            linked = true;
        } else {
            page_id_t pid = first_page_id_;
            while (pid != INVALID_PAGE_ID) {
                auto page_cur = bpm_->FetchPage(pid);
                if (!page_cur.has_value()) {
                    break;
                }

                storage::Page *page = page_cur.value();
                page->WLock();
                auto *header = page->Header();
                if (header->next_page_id == INVALID_PAGE_ID) {
                    header->next_page_id = new_page_id;
                    page->MarkDirty();
                    page->WUnLock();
                    bpm_->UnpinPage(pid, true);
                    linked = true;
                    break;
                }

                page_id_t next_pid = header->next_page_id;
                page->WUnLock();
                bpm_->UnpinPage(pid, false);
                pid = next_pid;
            }
        }
    }

    if (!linked) {
        bpm_->UnpinPage(new_page_id, true);
        bpm_->DeletePage(new_page_id);
        return std::nullopt;
    }

    storage::TablePage table_page(new_page);
    uint32_t free_space = 0;
    new_page->RLock();
    free_space = table_page.FreeSpace();
    new_page->RUnLock();
    UpdateFreeSpaceMap(new_page_id, free_space);
    bpm_->UnpinPage(new_page_id, true);
    return new_page_id;
}

void TableHeap::UpdateFreeSpaceMap(page_id_t page_id, uint32_t free_space)
{
    if (page_id == INVALID_PAGE_ID) {
        return;
    }
    std::scoped_lock lock(free_space_mutex_);
    free_space_map_[page_id] = free_space;
}

bool TableHeap::ReclaimPageIfEmpty(page_id_t page_id)
{
    if (bpm_ == nullptr || page_id == INVALID_PAGE_ID) {
        return false;
    }

    std::unique_lock lock(table_latch_);
    auto page_exp = bpm_->FetchPage(page_id);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    bool all_deleted = true;
    page_id_t next_pid = INVALID_PAGE_ID;
    bool pin_ok = false;

    {
        page->WLock();
        auto guard = MakeScopeGuard([&]() {
            page->WUnLock();
            bpm_->UnpinPage(page_id, false);
        });

        pin_ok = (page->PinCount() == 1);
        const auto *header = page->Header();
        next_pid = header->next_page_id;
        if (pin_ok) {
            storage::TablePage table_page(page);
            for (slot_id_t slot = 0; slot < header->slot_count; ++slot) {
                if (!table_page.GetSlot(slot)->IsDeleted()) {
                    all_deleted = false;
                    break;
                }
            }
        } else {
            all_deleted = false;
        }
    }

    if (!all_deleted) {
        return false;
    }

    {
        std::scoped_lock map_lock(free_space_mutex_);
        free_space_map_.erase(page_id);
    }

    if (first_page_id_ == page_id) {
        first_page_id_ = next_pid;
    } else {
        page_id_t pid = first_page_id_;
        while (pid != INVALID_PAGE_ID) {
            auto page_cur = bpm_->FetchPage(pid);
            if (!page_cur.has_value()) {
                break;
            }

            storage::Page *page = page_cur.value();
            page->WLock();
            auto *header = page->Header();
            if (header->next_page_id == page_id) {
                header->next_page_id = next_pid;
                page->MarkDirty();
                page->WUnLock();
                bpm_->UnpinPage(pid, true);
                break;
            }
            page_id_t next = header->next_page_id;
            page->WUnLock();
            bpm_->UnpinPage(pid, false);
            pid = next;
        }
    }

    return bpm_->DeletePage(page_id);
}

} // namespace table
} // namespace HaruhiDB
