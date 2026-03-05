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
     * English:
     * DBHeader is stored in the first page (page 0) of the database file.
     * It contains global metadata required to manage the database storage.
     *
     * 中文：
     * DBHeader 存储在数据库文件的第 0 页，用于记录数据库文件
     * 的全局元数据信息。
     */
    struct DBHeader
    {
        // English: Magic number used to verify that the file is a valid database file
        // 中文：数据库文件魔数，用于判断文件是否合法
        uint32_t magic_number;

        // English: Database file format version
        // 中文：数据库文件格式版本
        uint32_t version;

        // English: Next page id that can be allocated
        // 中文：下一个可分配的 page_id
        page_id_t next_page_id;

        // English: Head of the free page linked list
        // 中文：空闲页链表头指针
        page_id_t free_list_head;
    };

    // English: Ensure DBHeader can be directly written to disk
    // 中文：确保 DBHeader 可以直接进行二进制写入磁盘
    static_assert(std::is_trivially_copyable_v<DBHeader>);

    // English: Ensure header size is exactly 16 bytes
    // 中文：确保头部大小固定为 16 字节
    static_assert(sizeof(DBHeader) == 16, "DBHeader size must be 16 bytes");


    /**
     * English:
     * IOErr represents disk IO errors. It contains both a human-readable
     * error message and an internal error code.
     *
     * 中文：
     * IOErr 用于表示磁盘 IO 相关错误，
     * 包含错误信息字符串以及内部错误码。
     */
    struct IOErr {
        // English: descriptive error message
        // 中文：错误描述信息
        std::string msg;

        // English: internal error code
        // 中文：内部错误码
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
         * English:
         * Constructor. Opens or creates the database file.
         *
         * 中文：
         * 构造函数。打开或创建数据库文件。
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
        std::expected<void,IOErr> ReadPage(page_id_t page_id , page_data_t& data);

        /**
         * English:
         * Writes a page from memory to disk.
         *
         * 中文：
         * 将内存中的页面数据写回磁盘。
         */
        std::expected<void,IOErr> WritePage(page_id_t page_id , const page_data_t& data);

        /**
         * English:
         * Allocates a new page id.
         * If the free list contains pages, reuse them first.
         *
         * 中文：
         * 分配一个新的 page_id。
         * 如果存在空闲页，则优先复用空闲页。
         */
        std::expected<page_id_t,IOErr> AllocatePage();

        /**
         * English:
         * Deallocates a page and adds it back to the free list.
         *
         * 中文：
         * 释放一个页面，并将其加入空闲页链表。
         */
        std::expected<void,IOErr> DeallocatePage(page_id_t page_id);

        /**
         * English:
         * Flushes file buffers to disk to ensure durability.
         *
         * 中文：
         * 将文件缓冲区刷新到磁盘，保证数据持久化。
         */
        std::expected<void,IOErr> Flush();

    private:

        /**
         * English:
         * Opens the database file.
         *
         * 中文：
         * 打开数据库文件。
         */
        std::expected<void,IOErr> OpenFile();

        /**
         * English:
         * Initializes the header if the file is newly created.
         *
         * 中文：
         * 如果数据库文件是新创建的，则初始化 DBHeader。
         */
        std::expected<void,IOErr> InitHeaderIfNeeded();

        /**
         * English:
         * Loads the header page from disk into memory.
         *
         * 中文：
         * 从磁盘加载数据库头部信息。
         */
        std::expected<void,IOErr> LoadHeader();

        /**
         * English:
         * Persists the in-memory header to disk.
         *
         * 中文：
         * 将内存中的 DBHeader 写回磁盘。
         */
        std::expected<void,IOErr> PersistHeader();

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

        // English: database file magic number
        // 中文：数据库文件魔数
        static constexpr uint32_t DB_MAGIC = 0x48415255;

        // English: database file version
        // 中文：数据库文件版本
        static constexpr uint32_t DB_VERSION = 1;
    };

} // namespace storage
} // namespace HaruhiDB