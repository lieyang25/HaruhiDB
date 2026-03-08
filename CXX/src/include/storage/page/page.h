/**
 * CXX/src/include/storage/page/page.h
 *
 * English:
 * Page represents the in-memory abstraction of a database page.
 * It is the fundamental unit of storage and memory management in the database system.
 *
 * Each page corresponds to a fixed-size block (PAGE_SIZE) that can be written
 * to or read from disk by DiskManager. When loaded into memory, the page is
 * wrapped by this Page class, which provides:
 *
 * 1. Metadata management (PersistentHeader)
 * 2. Record storage via slot directory
 * 3. Concurrency control (shared_mutex latch)
 * 4. Buffer management information (pin count / dirty flag)
 *
 * Page Layout (Slotted Page Structure):
 *
 * +-----------------------------------------------------------+
 * | PersistentHeader                                          |
 * +-----------------------------------------------------------+
 * | Slot Array (grows forward)                                |
 * +-----------------------------------------------------------+
 * | Free Space                                                |
 * +-----------------------------------------------------------+
 * | Records (grows backward)                                  |
 * +-----------------------------------------------------------+
 *
 * The slot directory stores the offset and length of each record.
 * Records are inserted from the end of the page backward, allowing
 * variable-length record storage.
 *
 *
 * 中文：
 * Page 是数据库系统中“内存中的页面抽象”，
 * 是数据库存储和缓存管理的最基本单位。
 *
 * 每个 Page 对应磁盘上的一个固定大小块（PAGE_SIZE），
 * 由 DiskManager 负责读写，而 Page 类负责在内存中的
 * 页面结构管理，包括：
 *
 * 1. 页面元数据管理（PersistentHeader）
 * 2. 基于 Slot Directory 的记录存储
 * 3. 并发控制（shared_mutex 锁）
 * 4. BufferPool 管理信息（pin count / dirty 标志）
 *
 * 页面结构（Slotted Page）：
 *
 * +-----------------------------------------------------------+
 * | PersistentHeader                                          |
 * +-----------------------------------------------------------+
 * | Slot 数组（向前增长）                                     |
 * +-----------------------------------------------------------+
 * | 空闲空间                                                  |
 * +-----------------------------------------------------------+
 * | 记录数据（从页面尾部向前增长）                            |
 * +-----------------------------------------------------------+
 *
 * Slot 数组记录每条记录在页面中的 offset 和 length，
 * 从而支持变长记录存储。
 */

#pragma once

#include "common/config.h"

#include <atomic>
#include <shared_mutex>
#include <span>
#include <expected>

namespace HaruhiDB
{
namespace storage
{

    /**
     * English:
     * PageType describes the logical type of the page.
     *
     * 中文：
     * PageType 描述页面的逻辑类型。
     */
    enum class PageType : uint8_t {
        INVALID = 0,   // Invalid or uninitialized page / 无效页面
        HEAP,          // Heap table page / 表数据页
        INTERNAL,      // B+Tree internal node / B+树内部节点
        LEAF,          // B+Tree leaf node / B+树叶节点
        HEADER,        // Database header page / 数据库头页
        FREELIST       // Free list page / 空闲页链表
    };


    /**
     * English:
     * PersistentHeader stores metadata that must be persisted to disk.
     * It is stored at the beginning of the page.
     *
     * 中文：
     * PersistentHeader 存储需要持久化到磁盘的页面元数据，
     * 位于页面开头。
     */
    struct PersistentHeader
    {
        // English: log sequence number for WAL recovery
        // 中文：WAL 日志序列号
        lsn_t lsn;

        // English: page identifier
        // 中文：页面 ID
        page_id_t page_id;

        // English: number of slots currently in the slot directory
        // 中文：slot 数量
        slot_id_t slot_count;

        // English: offset indicating start of free space
        // 中文：空闲空间的起始偏移
        uint16_t free_space_offset;

        uint16_t free_list_head;

        // English: logical page type
        // 中文：页面逻辑类型
        PageType page_type;

        // English: reserved space for future extension
        // 中文：预留字段用于未来扩展
        uint8_t reserved[13];
    };

    // Ensure the structure can be directly copied to disk
    // 确保该结构可以直接写入磁盘
    static_assert(std::is_trivially_copyable_v<PersistentHeader>);

    // Ensure header size does not exceed configured header area
    // 确保 header 不超过 HEADER_SIZE
    static_assert(sizeof(PersistentHeader) <= HEADER_SIZE,"PersistentHeader size must lower HEADER_SIZE");

    /**
     * English:
     * Page is the in-memory representation of a database page.
     *
     * 中文：
     * Page 表示数据库页面在内存中的对象。
     */
    class Page
    {
    public:

        /**
         * English:
         * Constructor initializes page state.
         *
         * 中文：
         * 构造函数，初始化页面状态。
         */
        Page();

        ~Page() = default;

        /**
         * English:
         * Initializes the page as a blank page with given id and type.
         *
         * 中文：
         * 将页面初始化为一个空页面，并指定 page_id 与类型。
         */
        void InitBlank(page_id_t page_id,PageType page_type);

        void ResetMetaData(page_id_t page_id);
        /**
         * English:
         * Returns a pointer to the persistent header.
         *
         * 中文：
         * 返回页面 PersistentHeader 指针。
         */
        PersistentHeader* Header() noexcept;

        const PersistentHeader* Header() const noexcept;


        /**
         * English:
         * Returns page id.
         *
         * 中文：
         * 返回页面 ID。
         */
        page_id_t PageId() noexcept;


        /**
         * English:
         * Returns page type.
         *
         * 中文：
         * 返回页面类型。
         */
        PageType Type() noexcept;


        /**
         * English:
         * Increases pin count of this page.
         *
         * 中文：
         * 增加页面 pin_count。
         */
        void Pin() noexcept;


        /**
         * English:
         * Decreases pin count of this page.
         *
         * 中文：
         * 减少页面 pin_count。
         */
        void UnPin() noexcept;


        /**
         * English:
         * Returns current pin count.
         *
         * 中文：
         * 返回当前 pin_count。
         */
        int PinCount() const noexcept;


        /**
         * English:
         * Marks the page as dirty (modified).
         *
         * 中文：
         * 标记页面为 dirty（已修改）。
         */
        void MarkDirty() noexcept;

        /**     
         * English:
         * Marks the page not as dirty (modified).
         *
         * 中文：
         * 标记页面不为 dirty（已修改）。
         */
        void ClearDirty() noexcept;

        /**
         * English:
         * Returns whether page is dirty.
         *
         * 中文：
         * 判断页面是否为 dirty。
         */
        bool IsDirty() const noexcept;


        /**
         * English:
         * Acquire read lock.
         *
         * 中文：
         * 获取读锁。
         */
        void RLock() ;


        /**
         * English:
         * Release read lock.
         *
         * 中文：
         * 释放读锁。
         */
        void RUnLock() ;


        /**
         * English:
         * Acquire write lock.
         *
         * 中文：
         * 获取写锁。
         */
        void WLock() ;


        /**
         * English:
         * Release write lock.
         *
         * 中文：
         * 释放写锁。
         */
        void WUnLock() ;

        /**
         * English:
         * Returns raw page memory pointer.
         *
         * 中文：
         * 返回页面原始内存数据指针。
         */
        std::byte* RawData() noexcept;


        const std::byte* RawData() const noexcept;

        /**
         * English:
         * Returns raw page memory page_data_t.
         *
         * 中文：
         * 返回页面page_data_t。
         */
        page_data_t& Data() noexcept;

        const page_data_t& Data() const noexcept;
        
    private:

        // English: number of active pins on the page
        // 中文：当前页面被 pin 的次数
        std::atomic<int> pin_count_;

        // English: dirty flag indicating page modification
        // 中文：页面是否被修改
        std::atomic<bool> is_dirty_;

        // English: raw page data buffer
        // 中文：页面原始数据缓冲区
        page_data_t data_;

        // English: shared mutex used for page-level concurrency control
        // 中文：页面级并发控制锁
        std::shared_mutex latch_;
    };

} // namespace storage
} // namespace HaruhiDB