/**
 * CXX/src/table/table_iterator.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 TableIterator 的顺序扫描逻辑。
 *
 * 主要完成：
 *
 * 1. end / 起始迭代器构造
 * 2. 当前 tuple 读取
 * 3. 迭代前进
 * 4. 跨页跳转
 * 5. RID 获取与比较
 *
 *
 * ========================= 遍历规则 =========================
 *
 * - 仅返回未删除 slot
 * - 空页或无存活 tuple 的页直接跳过
 * - 到达页尾后自动跳转到 next_page_id
 * - 读取 tuple 时返回独立拷贝
 */

#include "table/table_iterator.h"
#include "table/table_heap.h"

#include "storage/page/table_page.h"

#include <shared_mutex>
#include <utility>
#include <vector>

namespace HaruhiDB
{
namespace table
{

namespace
{
    template <typename F>
    class ScopeGuard
    {
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
    {
    }

    /**
     * @param heap       关联表堆
     * @param start_page 起始页号
     * @param start_slot 起始槽位
     */
    TableIterator::TableIterator(TableHeap* heap, page_id_t start_page, slot_id_t start_slot)
        : heap_(heap),
          cur_page_id_(start_page),
          cur_slot_(start_slot),
          at_end_(false)
    {
        // step 1: 检查初始状态是否合法。
        if (heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
            at_end_ = true;
            return;
        }

        // step 2: 从给定起点定位第一条有效记录。
        std::shared_lock lock(heap_->table_latch_);
        if (!AdvanceToNextValid()) {
            at_end_ = true;
        }
    }

    record::Tuple TableIterator::operator*() const
    {
        // step 1: end 迭代器或无效状态直接返回空 tuple。
        if (at_end_ || heap_ == nullptr || cur_page_id_ == INVALID_PAGE_ID) {
            return {};
        }

        // step 2: 若缓存有效，则直接返回缓存拷贝。
        if (tuple_cached_) {
            return record::Tuple(std::span<const std::byte>(tuple_buffer_.data(), tuple_buffer_.size()));
        }

        // step 3: 取页并读取当前 slot 对应 tuple。
        std::shared_lock lock(heap_->table_latch_);
        auto page_exp = heap_->bpm_->FetchPage(cur_page_id_);
        if (!page_exp.has_value()) {
            return {};
        }

        storage::Page* page = page_exp.value();
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

        const storage::Slot* slot = table_page.GetSlot(cur_slot_);
        if (slot == nullptr || slot->IsDeleted()) {
            return {};
        }

        const uint16_t offset = slot->GetOffset();
        const uint16_t length = slot->GetLength();
        if (length == 0 || static_cast<size_t>(offset) + length > PAGE_SIZE) {
            return {};
        }

        // step 4: 拷贝当前 tuple 到本地缓存。
        const std::byte* begin = page->RawData() + offset;
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

    TableIterator& TableIterator::operator++()
    {
        // step 1: end 迭代器递增保持不变。
        if (at_end_ || heap_ == nullptr) {
            return *this;
        }

        // step 2: 清除 tuple 缓存，并把游标推进到下一个 slot。
        tuple_cached_ = false;

        std::shared_lock lock(heap_->table_latch_);
        if (cur_slot_ != INVALID_SLOT_ID) {
            cur_slot_ = static_cast<slot_id_t>(cur_slot_ + 1);
        }

        // step 3: 寻找下一条有效记录；失败则变为 end。
        if (!AdvanceToNextValid()) {
            at_end_ = true;
        }

        return *this;
    }

    bool TableIterator::operator==(const TableIterator& other) const noexcept
    {
        if (at_end_ && other.at_end_) {
            return true;
        }

        return heap_ == other.heap_ &&
               cur_page_id_ == other.cur_page_id_ &&
               cur_slot_ == other.cur_slot_ &&
               at_end_ == other.at_end_;
    }

    bool TableIterator::operator!=(const TableIterator& other) const noexcept
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

        // step 1: 沿页链顺序扫描，直到找到下一条有效记录。
        while (pid != INVALID_PAGE_ID) {
            cur_page_id_ = pid;
            cur_slot_ = slot;

            auto page_exp = heap_->bpm_->FetchPage(pid);
            if (!page_exp.has_value()) {
                return false;
            }

            storage::Page* page = page_exp.value();
            const page_id_t pid_snapshot = pid;
            page->RLock();
            auto release = MakeScopeGuard([&]() {
                page->RUnLock();
                heap_->bpm_->UnpinPage(pid_snapshot, false);
            });

            storage::TablePage table_page(page);

            // step 2: 空页或无存活 tuple 的页直接跳到下一页。
            if (table_page.SlotCount() == 0 || table_page.AliveTupleCount() == 0) {
                pid = table_page.NextPageId();
                slot = 0;
                continue;
            }

            // step 3: 若当前 slot 已越界，则跳转到下一页。
            if (cur_slot_ >= table_page.SlotCount()) {
                pid = table_page.NextPageId();
                slot = 0;
                continue;
            }

            // step 4: 在当前页从当前位置起寻找第一个未删除 slot。
            for (slot_id_t s = cur_slot_; s < table_page.SlotCount(); ++s) {
                if (!table_page.GetSlot(s)->IsDeleted()) {
                    cur_page_id_ = pid;
                    cur_slot_ = s;
                    tuple_cached_ = false;
                    return true;
                }
            }

            // step 5: 当前页无更多有效记录，则跳到下一页。
            pid = table_page.NextPageId();
            slot = 0;
        }

        tuple_cached_ = false;
        return false;
    }

} // namespace table
} // namespace HaruhiDB