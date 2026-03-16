/**
 * CXX/src/storage/disk/disk_manager.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 DiskManager 的页级磁盘管理逻辑。
 *
 * 主要完成：
 *
 * 1. 数据库文件打开与初始化
 * 2. Page 0 头页加载与持久化
 * 3. 固定大小页面读写
 * 4. 页面分配与回收
 * 5. free list 维护
 *
 *
 * ========================= 文件组织 =========================
 *
 *   +-----------+-----------+-----------+-----------+
 *   | Header    | Page 1    | Page 2    | Page 3    |
 *   +-----------+-----------+-----------+-----------+
 *
 * - Header 对应 Page 0
 * - 其他页面按 page_id * PAGE_SIZE 定位
 *
 *
 * ========================= 实现说明 =========================
 *
 * 该实现只负责页级 I/O 与头页管理，
 * 不解释普通页面内部的数据结构。
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
     * @param path 数据库文件路径
     * @note 构造时完成文件打开与头页初始化/加载
     */
    DiskManager::DiskManager(const std::filesystem::path& path)
        : path_(path)
    {
        // step 1: 打开数据库文件，必要时创建。
        auto open_file = OpenFile();
        if (!open_file) {
            throw std::runtime_error(
                "DiskManager: failed to open file: " + open_file.error().msg);
        }

        // step 2: 若为空文件则初始化头页，否则加载已有头页。
        auto init_header = InitHeaderIfNeeded();
        if (!init_header) {
            throw std::runtime_error(
                "DiskManager: failed to init header: " + init_header.error().msg);
        }
    }

    /**
     * @note 析构时刷新并关闭文件
     */
    DiskManager::~DiskManager()
    {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    /**
     * 打开数据库文件，并根据文件大小恢复 next_page_id_。
     *
     * @note 若文件不存在，则先创建再重新打开
     */
    std::expected<void, IOErr> DiskManager::OpenFile()
    {
        // step 1: 确保数据库文件所在目录存在。
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

        // step 2: 以读写二进制模式打开文件；若不存在则先创建。
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
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

        // step 3: 检查文件大小是否合法，并恢复 next_page_id_。
        try {
            auto file_size = std::filesystem::file_size(path_);
            if (file_size % PAGE_SIZE != 0) {
                return std::unexpected(IOErr{
                    "Database file is corrupted (file_size mismatch)",
                    HaruhiDB::ErrorCode::FileCorruptedSizeMismatch});
            }

            page_id_t pages = static_cast<page_id_t>(file_size / PAGE_SIZE);
            next_page_id_ = (pages == 0) ? 1 : pages;
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{
                std::format("File size error: {}", e.what()),
                HaruhiDB::ErrorCode::FileSizeError});
        }

        return {};
    }

    /**
     * 当数据库文件为空时初始化头页；否则加载已有头页。
     */
    std::expected<void, IOErr> DiskManager::InitHeaderIfNeeded()
    {
        uint64_t file_size = 0;

        // step 1: 读取文件大小，判断当前是否为空数据库文件。
        try {
            file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{
                std::format("File size error: {}", e.what()),
                HaruhiDB::ErrorCode::FileSizeError});
        }

        // step 2: 若文件不足一页，则写入新的头页。
        if (file_size < PAGE_SIZE) {
            DBHeader dbheader;
            dbheader.magic_number = DB_MAGIC;
            dbheader.version = DB_VERSION;
            dbheader.next_page_id = next_page_id_;
            dbheader.free_list_head = INVALID_PAGE_ID;

            std::array<std::byte, PAGE_SIZE> buffer{};
            std::memcpy(buffer.data(), &dbheader, sizeof(DBHeader));

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

        // step 3: 若头页已存在，则直接加载。
        return LoadHeader();
    }

    /**
     * 从 Page 0 读取 DBHeader 并恢复内存状态。
     */
    std::expected<void, IOErr> DiskManager::LoadHeader()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        // step 1: 读取 Page 0 到缓冲区。
        file_.clear();
        file_.seekg(0, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buffer.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "Failed to read header page",
                HaruhiDB::ErrorCode::HeaderReadFailed});
        }

        // step 2: 反序列化 DBHeader 并校验 magic。
        DBHeader dbheader;
        std::memcpy(&dbheader, buffer.data(), sizeof(DBHeader));

        if (dbheader.magic_number != DB_MAGIC) {
            return std::unexpected(IOErr{
                "Header magic mismatch",
                HaruhiDB::ErrorCode::HeaderMagicMismatch});
        }

        // step 3: 用头页内容恢复内存中的分配状态。
        next_page_id_ = static_cast<page_id_t>(dbheader.next_page_id);
        free_list_head_ = static_cast<page_id_t>(dbheader.free_list_head);

        if (next_page_id_ < 1) {
            next_page_id_ = 1;
        }

        return {};
    }

    /**
     * 将当前头页元数据写回 Page 0。
     */
    std::expected<void, IOErr> DiskManager::PersistHeader()
    {
        // step 1: 组装最新的 DBHeader。
        DBHeader dbheader{};
        dbheader.magic_number = DB_MAGIC;
        dbheader.version = DB_VERSION;
        dbheader.next_page_id = next_page_id_;
        dbheader.free_list_head = free_list_head_;

        // step 2: 序列化后覆盖写入 Page 0。
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

        return {};
    }

    /**
     * @param page_id 要读取的页号
     * @param data    输出缓冲区
     * @note 仅支持读取当前文件范围内的页面
     */
    std::expected<void, IOErr> DiskManager::ReadPage(page_id_t page_id, page_data_t& data)
    {
        // step 1: 检查文件状态与页号合法性。
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open.",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id == INVALID_PAGE_ID) {
            return std::unexpected(IOErr{
                "ReadPage: invalid page_id",
                HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();

        // step 2: 检查目标页面是否仍在文件范围内。
        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        if (offset + PAGE_SIZE > file_size) {
            return std::unexpected(IOErr{
                "ReadPage: page_id out of range.",
                HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        // step 3: 定位并读取整页内容。
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
     * @param page_id 要写入的页号
     * @param data    输入缓冲区
     * @note Page 0 由头页专用；普通写入只允许覆盖已有页或在末尾追加新页
     */
    std::expected<void, IOErr> DiskManager::WritePage(page_id_t page_id, const page_data_t& data)
    {
        // step 1: 检查文件状态与页号合法性。
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open.",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id == INVALID_PAGE_ID) {
            return std::unexpected(IOErr{
                "WritePage: invalid page_id",
                HaruhiDB::ErrorCode::WriteIOError});
        }
        if (page_id == 0) {
            return std::unexpected(IOErr{
                "WritePage: page 0 is reserved for DB header",
                HaruhiDB::ErrorCode::WriteIOError});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();

        // step 2: 检查是否为合法覆盖写或末尾追加写。
        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        if (offset + PAGE_SIZE > file_size) {
            if (offset != file_size) {
                return std::unexpected(IOErr{
                    "WritePage: page_id out of range.",
                    HaruhiDB::ErrorCode::WriteIOError});
            }
        }

        // step 3: 定位并写入整页内容。
        file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.write(reinterpret_cast<const char*>(data.data()), PAGE_SIZE);

        if (!file_) {
            return std::unexpected(IOErr{
                "WritePage: I/O error",
                HaruhiDB::ErrorCode::WriteIOError});
        }

        return {};
    }

    /**
     * 分配一个可用页面。
     *
     * @note 优先从 free list 复用，否则在文件末尾创建新页
     */
    std::expected<page_id_t, IOErr> DiskManager::AllocatePage()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        // step 1: 若 free list 非空，则弹出链表头作为复用页。
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

            // step 2: 持久化新的 free list 头指针。
            auto p = PersistHeader();
            if (!p) {
                return std::unexpected(IOErr{
                    "AllocatePage: persist header failed",
                    HaruhiDB::ErrorCode::PersistHeaderFailed});
            }

            return recycled_page;
        }

        // step 3: 若无可复用页，则在文件尾部分配新页。
        page_id_t new_page = next_page_id_;

        auto w = WritePage(new_page, buffer);
        if (!w) {
            return std::unexpected(IOErr{
                "AllocatePage: write new page failed",
                HaruhiDB::ErrorCode::AllocateWriteFailed});
        }

        next_page_id_++;

        // step 4: 持久化 next_page_id_ 的变化。
        auto p = PersistHeader();
        if (!p) {
            return std::unexpected(IOErr{
                "AllocatePage: persist header failed",
                HaruhiDB::ErrorCode::PersistHeaderFailed});
        }

        return new_page;
    }

    /**
     * @param page_id 要回收的页号
     * @note 回收页面时会把旧的 free_list_head_ 写入该页起始位置，随后更新头指针
     */
    std::expected<void, IOErr> DiskManager::DeallocatePage(page_id_t page_id)
    {
        // step 1: 检查文件状态与待回收页号是否合法。
        if (!file_.is_open()) {
            return std::unexpected(IOErr{
                "File not open",
                HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id == INVALID_PAGE_ID || page_id == 0 || page_id >= next_page_id_) {
            return std::unexpected(IOErr{
                "DeallocatePage: invalid page_id",
                HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        // step 2: 遍历当前 free list，防止重复回收或链表损坏。
        page_id_t cur = free_list_head_;
        size_t hops = 0;
        std::array<std::byte, PAGE_SIZE> cursor_buf{};

        while (cur != INVALID_PAGE_ID) {
            if (cur == page_id) {
                return std::unexpected(IOErr{
                    "DeallocatePage: page is already in free list",
                    HaruhiDB::ErrorCode::DeallocateWriteFailed});
            }
            if (cur == 0 || cur >= next_page_id_) {
                return std::unexpected(IOErr{
                    "DeallocatePage: free list corrupted (page id out of range)",
                    HaruhiDB::ErrorCode::DeallocateWriteFailed});
            }

            auto read_cur = ReadPage(cur, cursor_buf);
            if (!read_cur.has_value()) {
                return std::unexpected(IOErr{
                    "DeallocatePage: read free-list node failed: " + read_cur.error().msg,
                    HaruhiDB::ErrorCode::DeallocateWriteFailed});
            }

            uint64_t next_raw = 0;
            std::memcpy(&next_raw, cursor_buf.data(), sizeof(uint64_t));

            if (next_raw == static_cast<uint64_t>(INVALID_PAGE_ID)) {
                cur = INVALID_PAGE_ID;
            } else if (next_raw >= static_cast<uint64_t>(next_page_id_)) {
                return std::unexpected(IOErr{
                    "DeallocatePage: free list corrupted (next pointer out of range)",
                    HaruhiDB::ErrorCode::DeallocateWriteFailed});
            } else {
                cur = static_cast<page_id_t>(next_raw);
            }

            hops++;
            if (hops > static_cast<size_t>(next_page_id_)) {
                return std::unexpected(IOErr{
                    "DeallocatePage: free list corrupted (cycle detected)",
                    HaruhiDB::ErrorCode::DeallocateWriteFailed});
            }
        }

        // step 3: 把旧链表头写入当前页起始位置，使该页成为新的链表头。
        std::array<std::byte, PAGE_SIZE> buffer{};
        uint64_t cur_head_raw = static_cast<uint64_t>(free_list_head_);

        std::memcpy(buffer.data(), &cur_head_raw, sizeof(uint64_t));

        auto w = WritePage(page_id, buffer);
        if (!w) {
            return std::unexpected(IOErr{
                "DeallocatePage: write failed",
                HaruhiDB::ErrorCode::DeallocateWriteFailed});
        }

        // step 4: 更新 free_list_head_ 并持久化头页。
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