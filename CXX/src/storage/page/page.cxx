/**
 * CXX/src/storage/page/page.cxx
 */

#include "storage/page/page.h"
#include <cstring>
#include <cassert>

namespace HaruhiDB
{
namespace storage
{
    Page::Page() : pin_count_(0),is_dirty_(false)
    {
        std::memset(data_.data(),0,PAGE_SIZE);
        Header()->free_list_head = INVALID_SLOT_ID;
        Header()->page_id = INVALID_PAGE_ID;
        Header()->next_page_id = INVALID_PAGE_ID;
        Header()->page_type = PageType::INVALID;
        Header()->slot_count = 0;
        Header()->lsn = 0;
        Header()->free_space_offset = PAGE_SIZE;
    }

    void Page::InitBlank(page_id_t page_id,PageType page_type)
    {
        PersistentHeader* header = Header();
        header->free_list_head = INVALID_SLOT_ID;
        header->page_id = page_id;
        /**
         * TODO
         * 注意检查next_page_id的初始化
         */
        header->next_page_id = INVALID_PAGE_ID;
        header->page_type = page_type;
        header->slot_count = 0;
        header->lsn = 0;
        header->free_space_offset = PAGE_SIZE;

        pin_count_.store(0);
        is_dirty_.store(false);
    }

    void Page::ResetMetaData(page_id_t page_id)
    {
        Header()->page_id = page_id;
        pin_count_.store(0);
        is_dirty_.store(false);
    }

    PersistentHeader* Page::Header() noexcept
    {
        return reinterpret_cast<PersistentHeader*>(data_.data());
    }
    const PersistentHeader* Page::Header() const noexcept
    {
        return reinterpret_cast<const PersistentHeader*>(data_.data());
    }

    /**
     * PageId and Type
     *
     * English:
     * Return the page id and page type from header.
     *
     * 中文：
     * 返回页面的 page_id 和 page_type。
     */
    page_id_t Page::PageId() noexcept
    {
        return Header()->page_id;
    }
    PageType Page::Type() noexcept
    {
        return Header()->page_type;
    }

    /**
     * Pin/UnPin
     *
     * English:
     * Increment or decrement the pin count. Used by buffer pool to track
     * page usage. UnPin asserts pin_count > 0.
     *
     * 中文：
     * Pin/UnPin 用于管理页面引用计数，辅助 BufferPool 判断页面是否可替换。
     * UnPin 时断言 pin_count 大于 0。
     */
    void Page::Pin() noexcept
    {
        pin_count_.fetch_add(1,std::memory_order_acq_rel);
    }
    void Page::UnPin() noexcept
    {
        int old = pin_count_.fetch_sub(1, std::memory_order_acq_rel);
        assert(old > 0 && "UnPin called when pin_count == 0");
    }
    int Page::PinCount() const noexcept
    {
        return pin_count_.load(std::memory_order_acquire);
    }

    /**
     * Dirty flag
     *
     * English:
     * Mark the page as dirty or check dirty status.
     *
     * 中文：
     * 标记页面为脏，或者检查页面是否为脏。
     */
    void Page::MarkDirty() noexcept
    {
        is_dirty_.store(true,std::memory_order_release);
    }
     /**     
     * English:
     * Marks the page not as dirty (modified).
     *
     * 中文：
     * 标记页面不为 dirty（已修改）。
     */
    void Page::ClearDirty() noexcept
    {
        is_dirty_.store(false,std::memory_order_release);
    }
    bool Page::IsDirty() const noexcept
    {
        return is_dirty_.load(std::memory_order_acquire);
    }

    /**
     * Thread-safe locks
     *
     * English:
     * Provide shared (read) and exclusive (write) locks for page access.
     *
     * 中文：
     * 提供共享（读）锁和独占（写）锁，用于线程安全访问页面。
     */
    void Page::RLock() 
    {
        latch_.lock_shared();
    }
    void Page::RUnLock() 
    {
        latch_.unlock_shared();
    }
    void Page::WLock() 
    {
        latch_.lock();
    }
    void Page::WUnLock() 
    {
        latch_.unlock();
    }

    /**
     * RawData
     *
     * English:
     * Return pointer to the raw page data.
     *
     * 中文：
     * 返回页面原始数据指针。
     */
    std::byte* Page::RawData() noexcept
    {
        return data_.data();
    }
    const std::byte* Page::RawData() const noexcept
    {
        return data_.data();
    }

    page_data_t& Page::Data() noexcept
    {
        return data_;
    }

    const page_data_t& Page::Data() const noexcept
    {
        return data_;
    }

} // namespace storage
} // namespace HaruhiDB
