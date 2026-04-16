/**
 * CXX/src/table/table_heap.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 TableHeap 的表级堆组织逻辑。
 *
 * 主要完成：
 *
 * 1. 堆表创建
 * 2. 跨页插入 tuple
 * 3. 按 RID 读取 tuple
 * 4. 删除 tuple
 * 5. 更新 tuple 与迁移
 * 6. 新页分配与页链维护
 * 7. 空页回收
 * 8. WAL after-image 追加
 *
 *
 * ========================= 核心流程 =========================
 *
 * InsertTuple:
 *   先尝试在已有页中查找空闲空间
 *   若失败则分配新页
 *   再调用 TablePage::InsertTuple
 *
 * UpdateTuple:
 *   先尝试原页更新
 *   若空间不足则插入新位置，再删除旧位置
 *
 * DeleteTuple:
 *   先做页内逻辑删除
 *   再根据情况尝试整页回收
 *
 *
 * ========================= 锁与组织说明 =========================
 *
 * table_latch_
 *   - 保护页链结构、first_page_id_、tail_page_id_
 *
 * free_space_mutex_
 *   - 保护 free_space_map_
 *
 * Page latch
 *   - 保护单页内部读写
 */

#include "table/table_heap.h"
#include "table/table_iterator.h"
#include "scope_guard.h"

#include "storage/page/table_page.h"
#include "storage/wal/wal_manager.h"

#include <cstdio>
#include <cstdlib>
#include <shared_mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace table
{

namespace
{
    constexpr uint32_t MaxTupleSize()
    {
        return static_cast<uint32_t>(
            PAGE_SIZE - sizeof(storage::PersistentHeader) - sizeof(storage::Slot));
    }
} // namespace

    /**
     * @param bpm            缓冲池管理器
     * @param first_page_id  首页页号
     */
    TableHeap::TableHeap(buffer::BufferPoolManager* bpm, page_id_t first_page_id)
        : bpm_(bpm),
          first_page_id_(first_page_id),
          tail_page_id_(INVALID_PAGE_ID),
          wal_manager_(nullptr)
    {
    }

    /**
     * @param bpm 缓冲池管理器
     */
    std::expected<std::unique_ptr<TableHeap>, std::string> TableHeap::Create(buffer::BufferPoolManager* bpm)
    {
        // step 1: 检查基础依赖。
        if (bpm == nullptr) {
            return std::unexpected("TableHeap::Create: buffer pool manager is null");
        }

        // step 2: 申请首页。
        page_id_t first_page_id = INVALID_PAGE_ID;
        auto first_page_exp = bpm->NewPage(&first_page_id, storage::PageType::HEAP);
        if (!first_page_exp.has_value()) {
            return std::unexpected(
                "TableHeap::Create: failed to create first table page: " + first_page_exp.error().msg);
        }

        // step 3: 把首页初始化为合法的 TablePage。
        storage::Page* first_page = first_page_exp.value();
        storage::TablePage first_table_page(first_page);
        first_table_page.InitForNewPage(first_page_id);

        // step 4: 解除首页 pin；失败则回滚删除。
        if (!bpm->UnpinPage(first_page_id, true)) {
            bpm->DeletePage(first_page_id);
            return std::unexpected("TableHeap::Create: failed to unpin first table page");
        }

        return std::make_unique<TableHeap>(bpm, first_page_id);
    }

    /**
     * @param tuple   待插入记录
     * @param out_rid 成功时返回新 RID
     */
    bool TableHeap::InsertTuple(const record::Tuple& tuple, record::RID* out_rid)
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

            storage::Page* page = page_exp.value();
            storage::TablePage table_page(page);
            auto res = table_page.InsertTuple(tuple);

            if (res.has_value()) {
                AppendPageAfterImageLogOrDie(page, storage::wal::LogRecordType::PUT);
            }

            uint32_t free_space = 0;
            page->RLock();
            free_space = table_page.FreeSpace();
            page->RUnLock();

            UpdateFreeSpaceMap(pid, free_space);
            bpm_->UnpinPage(pid, res.has_value());
            return res;
        };

        // step 1: 先在已有页中尝试插入。
        std::unordered_set<page_id_t> tried_pages;
        while (true) {
            auto target = FindPageWithFreeSpace(need);
            if (!target.has_value()) {
                break;
            }
            if (tried_pages.contains(target.value())) {
                break;
            }
            tried_pages.insert(target.value());

            auto res = try_insert(target.value());
            if (res.has_value()) {
                if (out_rid != nullptr) {
                    out_rid->SetRID(target.value(), res.value());
                }
                return true;
            }
            if (res.error().err_code != storage::TablePageErrCode::InsufficientSpace) {
                return false;
            }
        }

        // step 2: 若没有合适旧页，则分配新页再插入。
        auto new_target = AllocateNewPage();
        if (!new_target.has_value()) {
            return false;
        }

        auto res = try_insert(new_target.value());
        if (!res.has_value()) {
            return false;
        }

        if (out_rid != nullptr) {
            out_rid->SetRID(new_target.value(), res.value());
        }
        return true;
    }

    /**
     * @param rid       目标记录位置
     * @param out_tuple 输出 tuple
     */
    bool TableHeap::GetTuple(const record::RID& rid, record::Tuple* out_tuple)
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

        storage::Page* page = page_exp.value();
        page->RLock();
        auto guard = MakeScopeGuard([&]() {
            page->RUnLock();
            bpm_->UnpinPage(pid, false);
        });

        // step 1: 检查 slot 是否存在且未被删除。
        storage::TablePage table_page(page);
        if (rid.GetSlotId() >= table_page.SlotCount()) {
            return false;
        }

        const storage::Slot* slot = table_page.GetSlot(rid.GetSlotId());
        if (slot == nullptr || slot->IsDeleted()) {
            return false;
        }

        // step 2: 校验 slot 指向的数据区范围。
        const uint16_t offset = slot->GetOffset();
        const uint16_t length = slot->GetLength();
        if (length == 0 || static_cast<size_t>(offset) + length > PAGE_SIZE) {
            return false;
        }

        // step 3: 拷贝 tuple 数据，避免依赖页生命周期。
        const std::byte* begin = page->RawData() + offset;
        std::vector<std::byte> data(begin, begin + length);
        *out_tuple = record::Tuple(std::move(data));
        return true;
    }

    /**
     * @param rid 目标记录位置
     */
    bool TableHeap::DeleteTuple(const record::RID& rid)
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

            // step 1: 先在目标页内执行逻辑删除。
            storage::Page* page = page_exp.value();
            storage::TablePage table_page(page);
            auto res = table_page.MarkDelTuple(rid.GetSlotId());

            if (res.has_value()) {
                AppendPageAfterImageLogOrDie(page, storage::wal::LogRecordType::DELETE);
            }

            uint32_t free_space = 0;
            page->RLock();
            free_space = table_page.FreeSpace();
            page->RUnLock();
            UpdateFreeSpaceMap(pid, free_space);

            bpm_->UnpinPage(pid, res.has_value());
            ok = res.has_value();
        }

        // step 2: 删除成功且当前无 WAL 时，尝试回收空页。
        if (ok && wal_manager_ == nullptr) {
            ReclaimPageIfEmpty(pid);
        }
        return ok;
    }

    /**
     * @param rid       原记录位置
     * @param new_tuple 新记录内容
     * @param out_rid   成功时返回最终 RID
     */
    bool TableHeap::UpdateTuple(
        const record::RID& rid, const record::Tuple& new_tuple, record::RID* out_rid)
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

            // step 1: 先尝试在原页内更新。
            storage::Page* page = page_exp.value();
            storage::TablePage table_page(page);
            auto res = table_page.UpdateTuple(rid.GetSlotId(), new_tuple);

            if (res.has_value()) {
                AppendPageAfterImageLogOrDie(page, storage::wal::LogRecordType::PUT);
            }

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

        // step 2: 原位更新成功则直接返回。
        if (updated_in_place) {
            if (out_rid != nullptr) {
                out_rid->SetRID(rid.GetPageId(), rid.GetSlotId());
            }
            return true;
        }

        // step 3: 若原页空间不足，则执行“插入新位置 + 删除旧位置”的迁移更新。
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

    /**
     * @param need 需要的字节数
     */
    std::optional<page_id_t> TableHeap::FindPageWithFreeSpace(uint32_t need)
    {
        if (bpm_ == nullptr || need == 0) {
            return std::nullopt;
        }

        // step 1: 先从 free_space_map 中筛选候选页。
        std::vector<page_id_t> candidates;
        {
            std::scoped_lock lock(free_space_mutex_);
            for (const auto& entry : free_space_map_) {
                if (entry.second >= need) {
                    candidates.push_back(entry.first);
                }
            }
        }

        // step 2: 逐个验证候选页的真实空闲空间，并刷新缓存。
        for (page_id_t pid : candidates) {
            auto page_exp = bpm_->FetchPage(pid);
            if (!page_exp.has_value()) {
                std::scoped_lock lock(free_space_mutex_);
                free_space_map_.erase(pid);
                continue;
            }

            storage::Page* page = page_exp.value();
            storage::TablePage table_page(page);

            page->RLock();
            const uint32_t free_space = table_page.FreeSpace();
            page->RUnLock();
            bpm_->UnpinPage(pid, false);

            {
                std::scoped_lock lock(free_space_mutex_);
                free_space_map_[pid] = free_space;
            }

            if (free_space >= need) {
                return pid;
            }
        }

        // step 3: 若缓存找不到，再顺着页链做一次真实扫描。
        std::shared_lock lock(table_latch_);
        page_id_t pid = first_page_id_;
        while (pid != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(pid);
            if (!page_exp.has_value()) {
                return std::nullopt;
            }

            storage::Page* page = page_exp.value();
            uint32_t free_space = 0;
            page_id_t next_pid = INVALID_PAGE_ID;

            {
                const page_id_t pid_snapshot = pid;
                page->RLock();
                auto guard = MakeScopeGuard([&]() {
                    page->RUnLock();
                    bpm_->UnpinPage(pid_snapshot, false);
                });

                storage::TablePage table_page(page);
                free_space = table_page.FreeSpace();
                next_pid = table_page.NextPageId();
            }

            UpdateFreeSpaceMap(pid, free_space);
            if (free_space >= need) {
                return pid;
            }
            pid = next_pid;
        }

        return std::nullopt;
    }

    /**
     * 分配并接入一个新的表页。
     */
    std::optional<page_id_t> TableHeap::AllocateNewPage()
    {
        if (bpm_ == nullptr) {
            return std::nullopt;
        }

        // step 1: 先从缓冲池申请一个新页，并初始化为 HEAP 表页。
        page_id_t new_page_id = INVALID_PAGE_ID;
        auto page_exp = bpm_->NewPage(&new_page_id, storage::PageType::HEAP);
        if (!page_exp.has_value()) {
            return std::nullopt;
        }

        storage::Page* new_page = page_exp.value();
        storage::TablePage new_table_page(new_page);
        new_table_page.InitForNewPage(new_page_id);
        AppendPageAfterImageLogOrDie(new_page, storage::wal::LogRecordType::PUT);

        bool linked = false;
        {
            std::unique_lock lock(table_latch_);

            // step 2: 若当前为空表，则新页直接成为首页和尾页。
            if (first_page_id_ == INVALID_PAGE_ID) {
                first_page_id_ = new_page_id;
                tail_page_id_ = new_page_id;
                linked = true;
            } else {
                // step 3: 否则把新页挂到当前尾页之后。
                if (tail_page_id_ == INVALID_PAGE_ID) {
                    tail_page_id_ = RefreshTailPageIdLocked();
                }

                page_id_t pid = tail_page_id_;
                while (pid != INVALID_PAGE_ID) {
                    auto page_cur = bpm_->FetchPage(pid);
                    if (!page_cur.has_value()) {
                        break;
                    }

                    storage::Page* page = page_cur.value();
                    storage::TablePage table_page(page);

                    page->WLock();
                    if (table_page.NextPageId() == INVALID_PAGE_ID) {
                        table_page.SetNextPageId(new_page_id);
                        page->MarkDirty();
                        page->WUnLock();

                        AppendPageAfterImageLogOrDie(page, storage::wal::LogRecordType::PUT);
                        bpm_->UnpinPage(pid, true);

                        tail_page_id_ = new_page_id;
                        linked = true;
                        break;
                    }

                    page_id_t next_pid = table_page.NextPageId();
                    page->WUnLock();
                    bpm_->UnpinPage(pid, false);
                    pid = next_pid;
                    tail_page_id_ = next_pid;
                }
            }
        }

        // step 4: 若接链失败，则回滚删除新页。
        if (!linked) {
            bpm_->UnpinPage(new_page_id, true);
            bpm_->DeletePage(new_page_id);
            return std::nullopt;
        }

        // step 5: 更新 free_space_map 并解除新页 pin。
        storage::TablePage table_page(new_page);
        uint32_t free_space = 0;
        new_page->RLock();
        free_space = table_page.FreeSpace();
        new_page->RUnLock();
        UpdateFreeSpaceMap(new_page_id, free_space);
        bpm_->UnpinPage(new_page_id, true);

        return new_page_id;
    }

    page_id_t TableHeap::RefreshTailPageIdLocked()
    {
        if (bpm_ == nullptr || first_page_id_ == INVALID_PAGE_ID) {
            tail_page_id_ = INVALID_PAGE_ID;
            return tail_page_id_;
        }

        page_id_t pid = first_page_id_;
        page_id_t last = INVALID_PAGE_ID;

        while (pid != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(pid);
            if (!page_exp.has_value()) {
                break;
            }

            storage::Page* page = page_exp.value();
            page_id_t next_pid = INVALID_PAGE_ID;

            page->RLock();
            storage::TablePage table_page(page);
            next_pid = table_page.NextPageId();
            page->RUnLock();
            bpm_->UnpinPage(pid, false);

            last = pid;
            pid = next_pid;
        }

        tail_page_id_ = last;
        return tail_page_id_;
    }

    void TableHeap::UpdateFreeSpaceMap(page_id_t page_id, uint32_t free_space)
    {
        if (page_id == INVALID_PAGE_ID) {
            return;
        }

        std::scoped_lock lock(free_space_mutex_);
        free_space_map_[page_id] = free_space;
    }

    /**
     * @param page_id 目标页号
     */
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

        storage::Page* page = page_exp.value();
        bool all_deleted = true;
        page_id_t next_pid = INVALID_PAGE_ID;
        bool pin_ok = false;

        {
            // step 1: 检查该页当前是否安全回收。
            page->WLock();
            auto guard = MakeScopeGuard([&]() {
                page->WUnLock();
                bpm_->UnpinPage(page_id, false);
            });

            pin_ok = (page->PinCount() == 1);
            storage::TablePage table_page(page);
            next_pid = table_page.NextPageId();

            if (pin_ok) {
                if (!table_page.TupleCountersConsistent()) {
                    table_page.RepairTupleCounters();
                }
                all_deleted = (table_page.AliveTupleCount() == 0);
            } else {
                all_deleted = false;
            }
        }

        if (!all_deleted) {
            return false;
        }

        bool detached = false;
        bool detached_from_head = false;
        page_id_t prev_pid = INVALID_PAGE_ID;

        // step 2: 先把该页从页链中摘掉。
        if (first_page_id_ == page_id) {
            first_page_id_ = next_pid;
            detached = true;
            detached_from_head = true;
        } else {
            page_id_t pid = first_page_id_;
            while (pid != INVALID_PAGE_ID) {
                auto page_cur = bpm_->FetchPage(pid);
                if (!page_cur.has_value()) {
                    break;
                }

                storage::Page* page = page_cur.value();
                storage::TablePage table_page(page);

                page->WLock();
                if (table_page.NextPageId() == page_id) {
                    table_page.SetNextPageId(next_pid);
                    page->MarkDirty();
                    page->WUnLock();
                    bpm_->UnpinPage(pid, true);

                    detached = true;
                    prev_pid = pid;
                    break;
                }

                page_id_t next = table_page.NextPageId();
                page->WUnLock();
                bpm_->UnpinPage(pid, false);
                pid = next;
            }
        }

        if (!detached) {
            return false;
        }

        // step 3: 请求缓冲池/磁盘层真正删除该页。
        if (bpm_->DeletePage(page_id)) {
            if (first_page_id_ == INVALID_PAGE_ID) {
                tail_page_id_ = INVALID_PAGE_ID;
            } else if (tail_page_id_ == page_id) {
                tail_page_id_ = (prev_pid != INVALID_PAGE_ID) ? prev_pid : RefreshTailPageIdLocked();
            }

            std::scoped_lock map_lock(free_space_mutex_);
            free_space_map_.erase(page_id);
            return true;
        }

        // step 4: 删除失败时尽力恢复页链结构。
        if (detached_from_head) {
            first_page_id_ = page_id;
        } else if (prev_pid != INVALID_PAGE_ID) {
            auto prev_exp = bpm_->FetchPage(prev_pid);
            if (prev_exp.has_value()) {
                storage::Page* prev_page = prev_exp.value();
                storage::TablePage prev_table_page(prev_page);

                prev_page->WLock();
                if (prev_table_page.NextPageId() == next_pid) {
                    prev_table_page.SetNextPageId(page_id);
                    prev_page->MarkDirty();
                    prev_page->WUnLock();
                    bpm_->UnpinPage(prev_pid, true);
                } else {
                    prev_page->WUnLock();
                    bpm_->UnpinPage(prev_pid, false);
                }
            }
        }

        if (tail_page_id_ == INVALID_PAGE_ID && first_page_id_ != INVALID_PAGE_ID) {
            RefreshTailPageIdLocked();
        }

        return false;
    }

    /**
     * @param where 失败位置说明
     */
    [[noreturn]] void TableHeap::HandleWalFailureOrDie(const char* where)
    {
        std::fprintf(stderr, "FATAL: WAL failure at %s\n", where == nullptr ? "unknown" : where);
        std::fflush(stderr);
        std::abort();
    }

    /**
     * @param page 目标页面
     * @param type 日志类型
     */
    void TableHeap::AppendPageAfterImageLogOrDie(storage::Page* page, storage::wal::LogRecordType type)
    {
        if (wal_manager_ == nullptr) {
            return;
        }

        // step 1: 检查 WAL 记录所依赖的页面是否合法。
        if (page == nullptr) {
            HandleWalFailureOrDie("AppendPageAfterImageLogOrDie(null page)");
        }
        if (page->PageId() == INVALID_PAGE_ID || page->PageId() == 0) {
            HandleWalFailureOrDie("AppendPageAfterImageLogOrDie(invalid page id)");
        }

        // step 2: 构造 after-image 日志记录。
        storage::wal::LogRecord log_record{};
        log_record.type = type;
        log_record.page_id = page->PageId();
        log_record.payload_len = WAL_PAYLOAD_LEN;

        page->RLock();
        log_record.after_image = page->Data();
        page->RUnLock();

        // step 3: 先追加日志，再强制刷日志；失败直接致命退出。
        if (!wal_manager_->AppendLog(log_record)) {
            HandleWalFailureOrDie("WalManager::AppendLog");
        }
        if (!wal_manager_->FlushLog()) {
            HandleWalFailureOrDie("WalManager::FlushLog");
        }
    }

} // namespace table
} // namespace HaruhiDB
