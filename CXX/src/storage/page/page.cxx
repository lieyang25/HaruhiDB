/**
 * CXX/src/storage/page/page.cxx
 *
 * English:
 * This file implements the Page class, which represents a single fixed-size
 * page in the database storage layer. A page contains a persistent header
 * and an array of slots storing records. It supports operations such as:
 *
 * 1. Initializing a blank page with page_id and page_type.
 * 2. Accessing header and slots.
 * 3. Inserting records with free space management.
 * 4. Pinning/unpinning for buffer pool management.
 * 5. Marking the page dirty and checking dirty status.
 * 6. Thread-safe read/write locks using a shared mutex (latch).
 * 7. Raw access to page data.
 *
 * 中文：
 * 本文件实现 Page 类，代表数据库存储层中的一个固定大小页面。
 * 页面包含持久化头（PersistentHeader）和存储记录的 Slot 数组。
 * Page 提供的功能包括：
 *
 * 1. 初始化空白页面（page_id 与 page_type）。
 * 2. 访问页面头和 Slot 数组。
 * 3. 插入记录并管理页面空闲空间。
 * 4. BufferPool 管理中的 Pin/UnPin。
 * 5. 标记页面脏并检查脏状态。
 * 6. 使用共享互斥量提供线程安全的读/写锁。
 * 7. 原始页面数据访问。
 */

#include "storage/page/page.h"
#include <cstring>
#include <cassert>

namespace HaruhiDB
{
namespace storage
{
    /**
     * Constructor
     *
     * English:
     * Initialize an empty page. Pin count is 0 and page is not dirty.
     *
     * 中文：
     * 构造函数，初始化一个空页面，pin_count 为 0，页面未标记为脏。
     */
    Page::Page() : pin_count_(0),is_dirty_(false)
    {
        std::memset(data_.data(),0,PAGE_SIZE);
        Header()->page_id = INVALID_PAGE_ID;
        Header()->page_type = PageType::INVALID;
        Header()->slot_count = 0;
        Header()->lsn = 0;
        Header()->free_space_offset = PAGE_SIZE;
    }

    // Page::~Page() is default, no special cleanup needed
    // 析构函数使用默认，无特殊资源释放需求

    /**
     * InitBlank
     *
     * English:
     * Initialize a blank page with a given page_id and page_type.
     * Sets header fields to default values and resets pin count and dirty flag.
     *
     * 中文：
     * 初始化空白页面，设置 page_id 和 page_type。
     * 初始化 header 各字段，并重置 pin_count 与脏标志。
     */
    void Page::InitBlank(page_id_t page_id,PageType page_type)
    {
        PersistentHeader* header = Header();
        header->page_id = page_id;
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
    /**
     * Header accessors
     *
     * English:
     * Return pointer to persistent header in page data.
     *
     * 中文：
     * 返回页面中 PersistentHeader 的指针。
     */
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
     * SlotArray access
     *
     * English:
     * Return pointer to the start of slot array after header.
     *
     * 中文：
     * 返回页面中 slot 数组的起始指针（紧跟 header 后）。
     */
    Slot* Page::SlotArray()noexcept
    {
        return reinterpret_cast<Slot*>(data_.data() + sizeof(PersistentHeader));
    }
    const Slot* Page::SlotArray() const noexcept
    {
        return reinterpret_cast<const Slot*>(data_.data() + sizeof(PersistentHeader));
    }

    /**
     * GetSlot
     *
     * English:
     * Return pointer to slot at given slot_id. Returns error if out-of-range.
     *
     * 中文：
     * 获取指定 slot_id 的 Slot 指针，如果 slot_id 超出范围返回错误。
     */
    std::expected<Slot*,bool> Page::GetSlot(slot_id_t slot_id)
    {
        if (slot_id >= Header()->slot_count) {
            return std::unexpected(false);
        }
        return &SlotArray()[slot_id];
    }

    /**
     * FreeSpace
     *
     * English:
     * Return number of free bytes available for record insertion.
     *
     * 中文：
     * 返回页面中可插入记录的剩余空闲空间。
     */
    size_t Page::FreeSpace() const noexcept
    {
        size_t slot_area_end = sizeof(PersistentHeader)
            + Header()->slot_count * sizeof(Slot);
        return Header()->free_space_offset - slot_area_end;
    }

    /**
     * InsertRecord
     *
     * English:
     * Insert a record into the page. Update free_space_offset, add a Slot,
     * mark page dirty. Returns false if insufficient free space.
     *
     * 中文：
     * 向页面插入记录，更新 free_space_offset，添加 Slot，并标记页面为脏。
     * 如果空闲空间不足，返回 false。
     */
    bool Page::InsertRecord(std::span<const std::byte> record)
    {
        size_t needed = record.size() + sizeof(Slot);
        if (FreeSpace() < needed) {
            return false;
        }

        PersistentHeader* header = Header();
        header->free_space_offset -= record.size();

        std::memcpy(data_.data() + header->free_space_offset,record.data(),record.size());

        SlotArray()[header->slot_count] = {
            static_cast<uint16_t>(header->free_space_offset),
            static_cast<uint16_t>(record.size())
        };

        header->slot_count ++;

        MarkDirty();
        return true;
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
        pin_count_.fetch_add(1,std::memory_order_relaxed);
    }
    void Page::UnPin() noexcept
    {
        int old = pin_count_.fetch_sub(1, std::memory_order_relaxed);
        assert(old > 0 && "UnPin called when pin_count == 0");
    }
    int Page::PinCount() const noexcept
    {
        return pin_count_.load(std::memory_order_relaxed);
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
        is_dirty_.store(true,std::memory_order_relaxed);
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
        is_dirty_.store(false,std::memory_order_relaxed);
    }
    bool Page::IsDirty() const noexcept
    {
        return is_dirty_.load(std::memory_order_relaxed);
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
