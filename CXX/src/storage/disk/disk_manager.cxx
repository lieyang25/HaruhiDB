/**
 * CXX/src/storage/disk/disk_manager.cxx
 *
 * English:
 * This file implements the DiskManager component, which is responsible for
 * managing persistent storage of database pages on disk. It provides page-level
 * I/O abstraction for the database storage layer. DiskManager is responsible for:
 *
 * 1. Opening and managing the database file.
 * 2. Reading and writing fixed-size pages.
 * 3. Managing page allocation and deallocation.
 * 4. Maintaining a persistent database header.
 * 5. Maintaining a free-list for recycled pages.
 *
 * The database file is organized as a sequence of fixed-size pages:
 *
 *   +-----------+-----------+-----------+-----------+
 *   | Header    | Page 1    | Page 2    | Page 3    |
 *   +-----------+-----------+-----------+-----------+
 *
 * Page 0 is reserved as the database header page, which stores metadata such as:
 * - magic number
 * - database version
 * - next available page id
 * - free list head
 *
 * The DiskManager provides the lowest-level storage abstraction used by
 * BufferPoolManager and other higher-level storage components.
 *
 *
 * 中文：
 * 本文件实现数据库存储层中的 DiskManager 组件，其职责是管理数据库文件
 * 与磁盘页（page）的持久化读写。DiskManager 为数据库系统提供页级别的
 * 磁盘 I/O 抽象接口，是 BufferPoolManager 以及其他存储组件的底层基础。
 *
 * DiskManager 主要负责：
 *
 * 1. 打开与管理数据库文件
 * 2. 按固定页大小读取与写入页面
 * 3. 管理页面分配与回收
 * 4. 维护数据库文件头（DBHeader）
 * 5. 维护空闲页链表（Free List）
 *
 * 数据库文件结构如下：
 *
 *   +-----------+-----------+-----------+-----------+
 *   | Header    | Page 1    | Page 2    | Page 3    |
 *   +-----------+-----------+-----------+-----------+
 *
 * Page0 为数据库头页，用于保存数据库元数据，例如：
 * - magic number（数据库标识）
 * - version（版本）
 * - next_page_id（下一个可分配页）
 * - free_list_head（空闲页链表头）
 *
 * DiskManager 是数据库系统中最底层的存储管理模块。
 */

#include "storage/disk/disk_manager.h"
#include <cstring>
#include <format>
#include <system_error>

namespace HaruhiDB
{
namespace storage
{

    /**
     * Constructor
     *
     * English:
     * Initialize DiskManager with the given database file path.
     * It will open the database file and initialize the header if necessary.
     *
     * 中文：
     * DiskManager 构造函数，负责初始化数据库文件。
     * 会尝试打开数据库文件，并在必要时初始化数据库头页。
     */
    DiskManager::DiskManager(const std::filesystem::path& path)
        : path_(path)
    {
        // Open file (create if not exist)
        // 打开数据库文件，如果不存在则创建
        auto open_file = OpenFile();
        if (!open_file) {
            throw std::runtime_error(
                "DiskManager: failed to open file: " + open_file.error().msg);
        }

        // Load header (if file existed) or init header
        // 加载数据库头，如果文件是新文件则初始化
        auto init_header = InitHeaderIfNeeded();
        if (!init_header) {
            throw std::runtime_error(
                "DiskManager: failed to init header: " + init_header.error().msg);
        }
    }

    /**
     * Destructor
     *
     * English:
     * Flush remaining data and close the database file.
     *
     * 中文：
     * 析构函数，负责刷新数据并关闭数据库文件。
     */
    DiskManager::~DiskManager()
    {
        // Flush data to disk and close file
        // 刷新数据并关闭文件
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    /**
     * OpenFile
     *
     * English:
     * Opens the database file. If the file does not exist, it will be created.
     * It also determines the next available page id based on file size.
     *
     * 中文：
     * 打开数据库文件。如果文件不存在则创建。
     * 同时根据文件大小计算当前数据库中的页数量，
     * 从而确定 next_page_id_。
     */
    std::expected<void, IOErr> DiskManager::OpenFile()
    {
        // Ensure parent directory exists
        // 确保数据库文件的父目录存在
        try {
            auto parent = path_.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent);
            }
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{
                std::format("Dir error: {}", e.what()),
                HaruhiDB::ErrorCode::DirError});
        }

        // Try to open existing file (read/write, binary)
        // 尝试以读写模式打开数据库文件
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {

            // If open failed, create file first
            // 如果打开失败，先创建文件再重新打开
            file_.clear();
            file_.open(path_, std::ios::out | std::ios::binary);
            file_.close();

            file_.open(path_, std::ios::binary | std::ios::out | std::ios::in);
        }

        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                std::format("Failed to open file: {}", path_.string()),
                HaruhiDB::ErrorCode::FileOpenFailed});
        }

        // Check file size and set next_page_id_
        // 检查文件大小并计算下一个可分配页ID
        try {

            auto file_size = std::filesystem::file_size(path_);

            // File size must be aligned with PAGE_SIZE
            // 文件大小必须是 PAGE_SIZE 的整数倍
            if (file_size % PAGE_SIZE != 0) {
                return std::unexpected(IOErr{
                    "Database file is corrupted (file_size mismatch)",
                    HaruhiDB::ErrorCode::FileCorruptedSizeMismatch});
            }

            page_id_t pages = static_cast<page_id_t>(file_size / PAGE_SIZE);

            // Page0 is header page
            // Page0 为数据库头页
            next_page_id_ = (pages == 0) ? 1 : pages;

        } catch (const std::exception& e) {
            return std::unexpected(IOErr{
                std::format("File size error: {}", e.what()),
                HaruhiDB::ErrorCode::FileSizeError});
        }

        return {};
    }

    /**
     * InitHeaderIfNeeded
     *
     * English:
     * Initialize database header page if the file is new.
     *
     * 中文：
     * 如果数据库文件是新创建的，则初始化数据库头页。
     */
    std::expected<void, IOErr> DiskManager::InitHeaderIfNeeded()
    {
        uint64_t file_size = 0;

        try {
            file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{
                std::format("File size error: {}", e.what()),
                HaruhiDB::ErrorCode::FileSizeError});
        }

        // If file size smaller than a page, initialize header
        // 如果文件小于一个页，说明是新数据库，需要初始化 header
        if (file_size < PAGE_SIZE) {

            DBHeader dbheader;

            dbheader.magic_number = DB_MAGIC;
            dbheader.version = DB_VERSION;
            dbheader.next_page_id = next_page_id_;
            dbheader.free_list_head = INVALID_PAGE_ID;

            std::array<std::byte, PAGE_SIZE> buffer{};

            // Serialize header into page buffer
            // 将 DBHeader 序列化到 page buffer
            std::memcpy(buffer.data(), &dbheader, sizeof(DBHeader));

            // Write header page to disk
            // 将 header 写入 Page0
            file_.seekp(0, std::ios::beg);
            file_.write(reinterpret_cast<const char*>(buffer.data()), PAGE_SIZE);

            if (!file_) {
                return std::unexpected(IOErr{
                    "Failed to write header page",
                    HaruhiDB::ErrorCode::HeaderWriteFailed});
            }

            file_.flush();
            return {};
        }

        // If header exists, load it
        // 如果 header 已存在，则加载
        return LoadHeader();
    }

    /**
     * LoadHeader
     *
     * English:
     * Read database header from disk and restore metadata.
     *
     * 中文：
     * 从磁盘读取数据库头页，并恢复数据库元数据。
     */
    std::expected<void, IOErr> DiskManager::LoadHeader()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        file_.clear();
        file_.seekg(0, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buffer.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "Failed to read header page",
                HaruhiDB::ErrorCode::HeaderReadFailed});
        }

        DBHeader dbheader;

        // Deserialize header
        // 反序列化 DBHeader
        std::memcpy(&dbheader, buffer.data(), sizeof(DBHeader));

        // Validate magic number
        // 校验数据库 magic number
        if (dbheader.magic_number != DB_MAGIC) {
            return std::unexpected(IOErr{
                "Header magic mismatch",
                HaruhiDB::ErrorCode::HeaderMagicMismatch});
        }

        next_page_id_ = static_cast<page_id_t>(dbheader.next_page_id);
        free_list_head_ = static_cast<page_id_t>(dbheader.free_list_head);

        if (next_page_id_ < 1) next_page_id_ = 1;

        return {};
    }

    /**
     * PersistHeader
     *
     * English:
     * Write updated database header to disk.
     *
     * 中文：
     * 将当前数据库头信息写回磁盘。
     */
    std::expected<void, IOErr> DiskManager::PersistHeader()
    {        
        DBHeader dbheader{};

        dbheader.magic_number = DB_MAGIC;
        dbheader.version = DB_VERSION;
        dbheader.next_page_id = next_page_id_;
        dbheader.free_list_head = free_list_head_;

        std::array<std::byte, PAGE_SIZE> buffer{};

        std::memcpy(buffer.data(), &dbheader, sizeof(DBHeader));

        file_.clear();
        file_.seekp(0, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(buffer.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "Failed to write header page",
                HaruhiDB::ErrorCode::HeaderWriteFailed});
        }

        file_.flush();
        return {};
    }

    /**
     * ReadPage
     *
     * English:
     * Read a specific page from disk into memory.
     *
     * 中文：
     * 从磁盘读取指定 page_id 的页面到内存缓冲区。
     */
    std::expected<void, IOErr> DiskManager::ReadPage(page_id_t page_id, page_data_t& data)
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open.",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id < 0) {
            return std::unexpected(IOErr{
                "ReadPage: invalid page_id",
                HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();

        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        if (offset + PAGE_SIZE > file_size) {
            return std::unexpected(IOErr{
                "ReadPage: page_id out of range.",
                HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "ReadPage: I/O error",
                HaruhiDB::ErrorCode::ReadIOError});
        }

        return {};
    }

    /**
     * WritePage
     *
     * English:
     * Write a page from memory to disk.
     *
     * 中文：
     * 将内存中的页面写入磁盘。
     */
    std::expected<void, IOErr> DiskManager::WritePage(page_id_t page_id, const page_data_t& data)
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open.",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id < 0) {
            return std::unexpected(IOErr{
                "WritePage: invalid page_id",
                HaruhiDB::ErrorCode::WriteIOError});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();

        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        // Allow append only at end of file
        // 只允许在文件末尾追加新页
        if (offset + PAGE_SIZE > file_size) {

            if (offset == file_size) {
                // allowed
            } else {
                return std::unexpected(IOErr{
                    "WritePage: page_id out of range.",
                    HaruhiDB::ErrorCode::WriteIOError});
            }
        }

        file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.write(reinterpret_cast<const char*>(data.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "WritePage: I/O error",
                HaruhiDB::ErrorCode::WriteIOError});
        }

        file_.flush();
        return {};
    }

    /**
     * AllocatePage
     *
     * English:
     * Allocate a new page from free list or append a new page.
     *
     * 中文：
     * 分配一个新的页面：
     * 1. 优先从 free list 回收
     * 2. 否则从 next_page_id_ 扩展数据库文件
     */
    std::expected<page_id_t, IOErr> DiskManager::AllocatePage()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        // reuse page from free list
        // 从 free list 中复用页面
        if (free_list_head_ != INVALID_PAGE_ID) {

            page_id_t recycled_page = free_list_head_;

            auto r = ReadPage(recycled_page, buffer);
            if (!r) {
                return std::unexpected(IOErr{
                    "AllocatePage: read free page failed",
                    HaruhiDB::ErrorCode::AllocateReadFreePageFailed});
            }

            uint64_t next_free_raw;

            std::memcpy(&next_free_raw, buffer.data(), sizeof(uint64_t));

            free_list_head_ =
                (next_free_raw == static_cast<uint64_t>(INVALID_PAGE_ID))
                    ? INVALID_PAGE_ID
                    : static_cast<page_id_t>(next_free_raw);

            auto p = PersistHeader();
            if (!p) {
                return std::unexpected(IOErr{
                    "AllocatePage: persist header failed",
                    HaruhiDB::ErrorCode::PersistHeaderFailed});
            }

            return recycled_page;
        }

        // allocate new page
        // 分配新的页面
        page_id_t new_page = next_page_id_;

        auto w = WritePage(new_page, buffer);
        if (!w) {
            return std::unexpected(IOErr{
                "AllocatePage: write new page failed",
                HaruhiDB::ErrorCode::AllocateWriteFailed});
        }

        next_page_id_++;

        auto p = PersistHeader();
        if (!p) {
            return std::unexpected(IOErr{
                "AllocatePage: persist header failed",
                HaruhiDB::ErrorCode::PersistHeaderFailed});
        }

        return new_page;
    }

    /**
     * DeallocatePage
     *
     * English:
     * Recycle a page and push it into the free list.
     *
     * 中文：
     * 释放一个页面，并将其加入 free list。
     */
    std::expected<void, IOErr> DiskManager::DeallocatePage(page_id_t page_id)
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        std::array<std::byte, PAGE_SIZE> buffer{};

        uint64_t cur_head_raw = static_cast<uint64_t>(free_list_head_);

        // store next pointer in page
        // 在页面中写入 free list 的 next 指针
        std::memcpy(buffer.data(), &cur_head_raw, sizeof(uint64_t));

        auto w = WritePage(page_id, buffer);
        if (!w) {
            return std::unexpected(IOErr{
                "DeallocatePage: write failed",
                HaruhiDB::ErrorCode::DeallocateWriteFailed});
        }

        free_list_head_ = page_id;

        auto p = PersistHeader();
        if (!p) {
            return std::unexpected(IOErr{
                "DeallocatePage: persist header failed",
                HaruhiDB::ErrorCode::PersistHeaderFailed});
        }

        return {};
    }

    /**
     * Flush
     *
     * English:
     * Flush file buffer to disk.
     *
     * 中文：
     * 将文件缓冲区刷新到磁盘。
     */
    std::expected<void, IOErr> DiskManager::Flush()
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open",
                HaruhiDB::ErrorCode::FlushFileNotOpen});
        }

        file_.flush();
        return {};
    }

} // namespace storage
} // namespace HaruhiDB

