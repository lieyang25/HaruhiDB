/**
 * CXX/src/include/storage/page/page.h
 *
 * ========================= 设计目标 =========================
 *
 * Page 表示缓冲池中的一个内存页对象。
 *
 * 它对应数据库中的一个固定大小页面，
 * 既保存页面原始字节数据，也保存运行时状态。
 *
 * 核心职责：
 *
 * 1. 持有完整页面字节数组
 * 2. 暴露页头 PersistentHeader 的访问接口
 * 3. 维护 pin_count 与 dirty 状态
 * 4. 提供页级读写锁
 *
 *
 * ========================= 为什么需要 Page =========================
 *
 * DiskManager 只负责“按 page_id 读写字节页”。
 * BufferPoolManager 需要的是“可管理的内存页对象”。
 *
 * Page 就是这层内存抽象：
 *
 * - data_ 保存页面原始内容
 * - PersistentHeader 位于 data_ 起始处
 * - pin_count_ / is_dirty_ / latch_ 负责运行时管理
 *
 *
 * ========================= Page 在系统中的位置 =========================
 *
 * DiskManager
 *   └── 读写 page_data_t
 *
 * BufferPoolManager
 *   └── 管理多个 Page
 *
 * TablePage / B+TreePage
 *   └── 基于 Page::Data() 解释页面内部结构
 *
 *
 * ========================= 页面组织 =========================
 *
 *   +------------------------------+
 *   | PersistentHeader             |
 *   +------------------------------+
 *   | opaque / page body / payload |
 *   | ...                          |
 *   +------------------------------+
 *
 * 其中：
 *
 * - data_ 的前 HEADER_SIZE 字节解释为 PersistentHeader
 * - 剩余区域由具体页面类型自行解释
 */

#pragma once

#include "common/config.h"

#include <atomic>
#include <expected>
#include <shared_mutex>
#include <span>

namespace HaruhiDB
{
namespace storage
{

    enum class PageType : uint8_t {
        INVALID = 0,
        HEAP,
        INTERNAL,
        LEAF,
        HEADER,
        FREELIST
    };

    constexpr size_t PAGE_HEADER_OPAQUE_SIZE = 16;

    /**
     * 页面头部的持久化区域。
     *
     * 该结构直接存放在页面起始位置，
     * 会随整页一起写回磁盘。
     */
    struct PersistentHeader
    {
        lsn_t lsn;
        page_id_t page_id;
        PageType page_type;
        uint8_t reserved0[3];
        uint8_t opaque[PAGE_HEADER_OPAQUE_SIZE];
    };

    static_assert(std::is_trivially_copyable_v<PersistentHeader>);
    static_assert(sizeof(PersistentHeader) == HEADER_SIZE,
                  "PersistentHeader size must equal HEADER_SIZE");

    class Page
    {
    public:
        /**
         * 构造一个未绑定实际 page_id 的空页面对象。
         */
        Page();

        ~Page() = default;

        /**
         * 将页面初始化为指定页号和类型的空白页。
         *
         * @param page_id   新页号
         * @param page_type 页面类型
         */
        void InitBlank(page_id_t page_id, PageType page_type);

        /**
         * 重置运行时元数据，并更新 page_id。
         *
         * @param page_id 新页号
         * @note 该函数不负责重建完整页头内容
         */
        void ResetMetaData(page_id_t page_id);

        /**
         * 返回页面头部指针。
         */
        PersistentHeader* Header() noexcept;

        /**
         * 返回页面头部常量指针。
         */
        const PersistentHeader* Header() const noexcept;

        /**
         * 返回当前页号。
         */
        page_id_t PageId() noexcept;

        /**
         * 返回当前页面类型。
         */
        PageType Type() noexcept;

        /**
         * 增加 pin_count。
         */
        void Pin() noexcept;

        /**
         * 减少 pin_count。
         */
        void UnPin() noexcept;

        /**
         * 返回当前 pin_count。
         */
        int PinCount() const noexcept;

        /**
         * 标记页面为脏页。
         */
        void MarkDirty() noexcept;

        /**
         * 清除脏页标记。
         */
        void ClearDirty() noexcept;

        /**
         * 返回页面是否为脏页。
         */
        bool IsDirty() const noexcept;

        /**
         * 获取共享读锁。
         */
        void RLock();

        /**
         * 释放共享读锁。
         */
        void RUnLock();

        /**
         * 获取独占写锁。
         */
        void WLock();

        /**
         * 释放独占写锁。
         */
        void WUnLock();

        /**
         * 返回页面原始字节首地址。
         */
        std::byte* RawData() noexcept;

        /**
         * 返回页面原始字节首地址。
         */
        const std::byte* RawData() const noexcept;

        /**
         * 返回整页字节数组。
         */
        page_data_t& Data() noexcept;

        /**
         * 返回整页字节数组。
         */
        const page_data_t& Data() const noexcept;

    private:
        /// 当前页面 pin 引用计数
        std::atomic<int> pin_count_;

        /// 当前页面是否为脏页
        std::atomic<bool> is_dirty_;

        /// 页面完整原始字节数据
        page_data_t data_;

        /// 页级读写锁
        std::shared_mutex latch_;
    };

} // namespace storage
} // namespace HaruhiDB