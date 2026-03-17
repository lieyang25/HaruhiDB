/**
 * CXX/src/storage/page/page.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 Page 的基础页对象行为。
 *
 * 主要完成：
 *
 * 1. 页面初始构造
 * 2. 空白页初始化
 * 3. 页头访问
 * 4. pin / dirty 状态管理
 * 5. 页级读写锁封装
 * 6. 原始字节数据访问
 *
 *
 * ========================= 实现说明 =========================
 *
 * Page 不解释具体业务结构，
 * 只提供“页对象”这一层的通用能力。
 *
 * 真正的数据页、索引页、头页等语义，
 * 由上层结合 PageType 与页面布局继续解释。
 */

#include "storage/page/page.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace HaruhiDB
{
namespace storage
{

    Page::Page() : pin_count_(0), is_dirty_(false)
    {
        // 对齐约束：后续会把 RawData 解释为 PersistentHeader/索引页结构体。
        const auto raw_addr = reinterpret_cast<std::uintptr_t>(data_.data());
        assert((raw_addr % alignof(PersistentHeader)) == 0 &&
               "Page::data_ must satisfy PersistentHeader alignment");

        // step 1: 清空整页原始字节。
        std::memset(data_.data(), 0, PAGE_SIZE);

        // step 2: 初始化默认页头，使该对象处于未绑定页面状态。
        Header()->page_id = INVALID_PAGE_ID;
        Header()->page_type = PageType::INVALID;
        Header()->lsn = 0;
        std::memset(Header()->reserved0, 0, sizeof(Header()->reserved0));
        std::memset(Header()->opaque, 0, sizeof(Header()->opaque));
    }

    /**
     * @param page_id   新页号
     * @param page_type 页面类型
     */
    void Page::InitBlank(page_id_t page_id, PageType page_type)
    {
        // step 1: 清空整页内容。
        std::memset(data_.data(), 0, PAGE_SIZE);

        // step 2: 重建页头的基础持久化字段。
        PersistentHeader* header = Header();
        header->page_id = page_id;
        header->page_type = page_type;
        header->lsn = 0;
        std::memset(header->reserved0, 0, sizeof(header->reserved0));
        std::memset(header->opaque, 0, sizeof(header->opaque));

        // step 3: 重置运行时状态。
        pin_count_.store(0);
        is_dirty_.store(false);
    }

    /**
     * @param page_id 新页号
     * @note 仅重置运行时状态与 page_id，不清空整页内容
     */
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

    page_id_t Page::PageId() noexcept
    {
        return Header()->page_id;
    }

    PageType Page::Type() noexcept
    {
        return Header()->page_type;
    }

    void Page::Pin() noexcept
    {
        pin_count_.fetch_add(1, std::memory_order_acq_rel);
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

    void Page::MarkDirty() noexcept
    {
        is_dirty_.store(true, std::memory_order_release);
    }

    void Page::ClearDirty() noexcept
    {
        is_dirty_.store(false, std::memory_order_release);
    }

    bool Page::IsDirty() const noexcept
    {
        return is_dirty_.load(std::memory_order_acquire);
    }

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
