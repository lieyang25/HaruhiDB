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

    const uint32_t need = static_cast<uint32_t>(tuple_size + sizeof(storage::Slot));

    auto try_insert = [&](page_id_t pid) -> std::expected<slot_id_t, storage::TablePageErr> {
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return std::unexpected(storage::TablePageErr{
                "TableHeap::InsertTuple: fetch page failed",
                storage::TablePageErrCode::NullPage});
        }

        storage::Page *page = page_exp.value();
        storage::TablePage table_page(page);
        auto res = table_page.InsertTuple(tuple);
        UpdateFreeSpaceMap(pid, table_page.FreeSpace());
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

    auto page_exp = bpm_->FetchPage(pid);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    storage::TablePage table_page(page);
    auto res = table_page.GetTuple(rid.GetSlotId(), *out_tuple);
    bpm_->UnpinPage(pid, false);
    return res.has_value();
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

    auto page_exp = bpm_->FetchPage(pid);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    storage::TablePage table_page(page);
    auto res = table_page.MarkDelTuple(rid.GetSlotId());
    UpdateFreeSpaceMap(pid, table_page.FreeSpace());
    bpm_->UnpinPage(pid, res.has_value());

    if (res.has_value()) {
        ReclaimPageIfEmpty(pid);
    }
    return res.has_value();
}

bool TableHeap::UpdateTuple(const record::RID &rid, const record::Tuple &new_tuple)
{
    if (bpm_ == nullptr) {
        return false;
    }

    const page_id_t pid = rid.GetPageId();
    if (pid == INVALID_PAGE_ID) {
        return false;
    }

    auto page_exp = bpm_->FetchPage(pid);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    storage::TablePage table_page(page);
    auto res = table_page.UpdateTuple(rid.GetSlotId(), new_tuple);
    UpdateFreeSpaceMap(pid, table_page.FreeSpace());
    bpm_->UnpinPage(pid, res.has_value());

    if (!res.has_value()) {
        return false;
    }
    return true;
}

TableIterator TableHeap::Begin()
{
    if (first_page_id_ == INVALID_PAGE_ID) {
        return End();
    }
    return TableIterator(this, first_page_id_, 0);
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
        std::scoped_lock lock(meta_mutex_);
        auto it = std::ranges::find_if(
            free_space_map_,
            [need](const auto &entry) { return entry.second >= need; });
        if (it != free_space_map_.end()) {
            return it->first;
        }
    }

    page_id_t pid = first_page_id_;
    while (pid != INVALID_PAGE_ID) {
        auto page_exp = bpm_->FetchPage(pid);
        if (!page_exp.has_value()) {
            return std::nullopt;
        }

<<<<<<< HEAD
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
        std::scoped_lock lock(meta_mutex_);
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
    UpdateFreeSpaceMap(new_page_id, table_page.FreeSpace());
    bpm_->UnpinPage(new_page_id, true);
    return new_page_id;
}

void TableHeap::UpdateFreeSpaceMap(page_id_t page_id, uint32_t free_space)
{
    if (page_id == INVALID_PAGE_ID) {
        return;
    }
    std::scoped_lock lock(meta_mutex_);
    free_space_map_[page_id] = free_space;
}

bool TableHeap::ReclaimPageIfEmpty(page_id_t page_id)
{
    if (bpm_ == nullptr || page_id == INVALID_PAGE_ID) {
        return false;
    }

    auto page_exp = bpm_->FetchPage(page_id);
    if (!page_exp.has_value()) {
        return false;
    }

    storage::Page *page = page_exp.value();
    bool all_deleted = true;
    page_id_t next_pid = INVALID_PAGE_ID;

    {
        page->RLock();
        auto guard = MakeScopeGuard([&]() {
            page->RUnLock();
            bpm_->UnpinPage(page_id, false);
        });

        const auto *header = page->Header();
        next_pid = header->next_page_id;
        storage::TablePage table_page(page);
        for (slot_id_t slot = 0; slot < header->slot_count; ++slot) {
            if (!table_page.GetSlot(slot)->IsDeleted()) {
                all_deleted = false;
                break;
            }
        }
    }

    if (!all_deleted) {
        return false;
    }

    {
        std::scoped_lock lock(meta_mutex_);
        free_space_map_.erase(page_id);
=======
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
>>>>>>> master

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
<<<<<<< HEAD
    }

    return bpm_->DeletePage(page_id);
}
=======

        return bpm_->DeletePage(page_id);
    }
>>>>>>> master

} // namespace table
} // namespace HaruhiDB
