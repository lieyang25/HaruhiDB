/**
 * CXX/src/include/storage/disk/disk_manager.h
 *
 * English:
 * DiskManager is the lowest level component of the database storage system.
 * It is responsible for managing the physical database file on disk and
 * providing page-based read and write operations for upper layers.
 *
 * In this database design, the disk file is treated as a sequence of fixed-size
 * pages (PAGE_SIZE). DiskManager provides the following core responsibilities:
 *
 * 1. Page IO
 *    - Read a page from disk into memory
 *    - Write a page from memory back to disk
 *
 * 2. Page allocation
 *    - Allocate a new page id
 *    - Reuse pages from the free list if available
 *
 * 3. File metadata management
 *    - Maintain a database header stored in page 0
 *    - Track next_page_id and free_list_head
 *
 * Disk layout:
 *
 * +-----------+-------------------+-------------------+-----
 * | Page 0    | Page 1            | Page 2            | ...
 * | DBHeader  | Data Page         | Data Page         |
 * +-----------+-------------------+-------------------+-----
 *
 * Page 0 always stores the DBHeader structure which contains:
 * - magic number (file identity)
 * - version
 * - next allocatable page id
 * - free list head
 *
 * Thread model:
 * DiskManager itself usually does not enforce heavy synchronization.
 * Concurrency is typically handled by BufferPoolManager.
 *
 *
 * 中文：
 * DiskManager 是数据库存储系统的最底层组件，
 * 负责管理数据库文件在磁盘上的物理存储，并向上层提供基于
 * “固定大小页面（page）” 的读写接口。
 *
 * 在该数据库设计中，整个数据库文件被视为由固定大小 PAGE_SIZE
 * 的页面序列组成。DiskManager 主要承担以下职责：
 *
 * 1. 页面 IO
 *    - 从磁盘读取一个 page
 *    - 将内存中的 page 写回磁盘
 *
 * 2. 页面分配
 *    - 分配新的 page_id
 *    - 如果存在空闲页，则从 free list 复用
 *
 * 3. 文件元数据管理
 *    - 管理数据库头部信息（DBHeader）
 *    - 维护 next_page_id 与 free_list_head
 *
 * 磁盘文件布局：
 *
 * +-----------+-------------------+-------------------+-----
 * | Page 0    | Page 1            | Page 2            | ...
 * | DBHeader  | 数据页            | 数据页            |
 * +-----------+-------------------+-------------------+-----
 *
 * 第 0 页始终存储 DBHeader 结构，其中包含：
 * - magic number（文件标识）
 * - 数据库版本
 * - 下一个可分配 page id
 * - 空闲页链表头
 */

#pragma once

#include "common/error_code.h"
#include "common/config.h"
#include <string>
#include <expected>
#include <filesystem>
#include <fstream>

namespace HaruhiDB
{
namespace storage
{

    /**
     * 作为文件头页使用
     * @param magic_number 是表示本数据库文件的标识
     * @param version 是数据库版本
     * @param next_page_id 指向下一个可分配的页
     * @param free_list_head 是隐式链表头
     */
    struct DBHeader
    {
        uint32_t magic_number;

        uint32_t version;

        page_id_t next_page_id;

        page_id_t free_list_head;
    };

    static_assert(std::is_trivially_copyable_v<DBHeader>);
    static_assert(sizeof(DBHeader) == 16, "DBHeader size must be 16 bytes");

    /**
     * 
     */
    struct IOErr {
        std::string msg;

        HaruhiDB::ErrorCode err_code;
    };


    /**
     * English:
     * DiskManager manages the database file on disk.
     *
     * Responsibilities include:
     * - Opening/initializing the database file
     * - Reading and writing pages
     * - Allocating and deallocating pages
     * - Maintaining database header metadata
     *
     * It serves as the persistent storage layer beneath the BufferPoolManager.
     *
     * 中文：
     * DiskManager 负责管理数据库磁盘文件，
     * 主要功能包括：
     * - 打开和初始化数据库文件
     * - 读取和写入页面
     * - 分配与释放页面
     * - 维护数据库头部元数据
     *
     * 该组件位于 BufferPoolManager 之下，提供持久化存储能力。
     */
    class DiskManager
    {
    public:

        /**
         * @param path
         */
        explicit DiskManager(const std::filesystem::path& path);

        /**
         * English:
         * Destructor. Ensures file resources are properly released.
         *
         * 中文：
         * 析构函数，负责释放文件资源。
         */
        ~DiskManager();

        /**
         * English:
         * Reads a page from disk into memory.
         *
         * @param page_id page identifier
         * @param data memory buffer to store page data
         *
         * 中文：
         * 从磁盘读取一个页面到内存缓冲区。
         *
         * @param page_id 页面编号
         * @param data 用于存储页面数据的内存缓冲区
         */
        auto ReadPage(page_id_t page_id , page_data_t& data) -> std::expected<void,IOErr>;

        /**
         * English:
         * Writes a page from memory to disk.
         *
         * 中文：
         * 将内存中的页面数据写回磁盘。
         */
        auto WritePage(page_id_t page_id , const page_data_t& data) -> std::expected<void,IOErr>;

        /**
         * English:
         * Allocates a new page id.
         * If the free list contains pages, reuse them first.
         *
         * 中文：
         * 分配一个新的 page_id。
         * 如果存在空闲页，则优先复用空闲页。
         */
        auto AllocatePage() -> std::expected<page_id_t,IOErr>;

        /**
         * English:
         * Deallocates a page and adds it back to the free list.
         *
         * 中文：
         * 释放一个页面，并将其加入空闲页链表。
         */
        auto DeallocatePage(page_id_t page_id) -> std::expected<void,IOErr>;

        /**
         * English:
         * Flushes file buffers to disk to ensure durability.
         *
         * 中文：
         * 将文件缓冲区刷新到磁盘，保证数据持久化。
         */
        auto Flush() -> std::expected<void,IOErr>;

    private:

        /**
         * English:
         * Opens the database file.
         *
         * 中文：
         * 打开数据库文件。
         */
        auto OpenFile() -> std::expected<void,IOErr>;

        /**
         * English:
         * Initializes the header if the file is newly created.
         *
         * 中文：
         * 如果数据库文件是新创建的，则初始化 DBHeader。
         */
        auto InitHeaderIfNeeded() -> std::expected<void,IOErr>;

        /**
         * English:
         * Loads the header page from disk into memory.
         *
         * 中文：
         * 从磁盘加载数据库头部信息。
         */
        auto LoadHeader() -> std::expected<void,IOErr>;

        /**
         * English:
         * Persists the in-memory header to disk.
         *
         * 中文：
         * 将内存中的 DBHeader 写回磁盘。
         */
        auto PersistHeader() -> std::expected<void,IOErr>;

    private:

        // English: database file path
        // 中文：数据库文件路径
        std::filesystem::path path_;

        // English: file stream used for disk IO
        // 中文：用于磁盘 IO 的文件流
        std::fstream file_;

        // English: next page id to allocate
        // 中文：下一个可分配的 page_id
        page_id_t next_page_id_{1};

        // English: head of the free page list
        // 中文：空闲页链表头
        page_id_t free_list_head_{INVALID_PAGE_ID};
    };

} // namespace storage
} // namespace HaruhiDB