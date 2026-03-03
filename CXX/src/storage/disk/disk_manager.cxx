/**
 * CXX/src/storage/disk/disk_manager.cxx
 */

#include "storage/disk/disk_manager.h"
#include <cstring>
#include <format>
#include <system_error>

namespace HaruhiDB
{
namespace storage
{
    DiskManager::DiskManager(const std::filesystem::path& path)
        : path_(path)
    {
        // Open file (create if not exist)
        auto open_file = OpenFile();
        if (!open_file) {
            throw std::runtime_error(
                "DiskManager: failed to open file: " + open_file.error().msg);
        }
        // Load header (if file existed) or init header
        auto init_header = InitHeaderIfNeeded();
        if (!init_header) {
            throw std::runtime_error(
                "DiskManager: failed to init header: " + init_header.error().msg);
        }
    }

    DiskManager::~DiskManager()
    {
        // Flush data to disk and close file
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    std::expected<void, IOErr> DiskManager::OpenFile()
    {
        // Ensure parent directory exists
        try {
            std::filesystem::create_directories(path_.parent_path());
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("Dir error: {}", e.what()), HaruhiDB::ErrorCode::DirError});
        }

        // Try to open existing file (read/write, binary)
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            // create empty file then reopen in r/w
            file_.clear();
            file_.open(path_, std::ios::out | std::ios::binary);
            file_.close();
            file_.open(path_, std::ios::binary | std::ios::out | std::ios::in);
        }

        if (!file_.is_open()) {
            return std::unexpected(IOErr{std::format("Failed to open file: {}", path_.string()), HaruhiDB::ErrorCode::FileOpenFailed});
        }

        // Check file size and set next_page_id_
        try {
            auto file_size = std::filesystem::file_size(path_);
            if (file_size % PAGE_SIZE != 0) {
                return std::unexpected(IOErr{"Database file is corrupted (file_size mismatch)", HaruhiDB::ErrorCode::FileCorruptedSizeMismatch});
            }

            page_id_t pages = static_cast<page_id_t>(file_size / PAGE_SIZE);
            next_page_id_ = (pages == 0) ? 1 : pages;
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("File size error: {}", e.what()), HaruhiDB::ErrorCode::FileSizeError});
        }

        return {};
    }

    std::expected<void, IOErr> DiskManager::InitHeaderIfNeeded()
    {
        // Check file size, if less than PAGE_SIZE, initialize header
        uint64_t file_size = 0;
        try {
            file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("File size error: {}", e.what()), HaruhiDB::ErrorCode::FileSizeError});
        }

        // If file is new or corrupted (size less than PAGE_SIZE), initialize header
        if (file_size < PAGE_SIZE) {
            DBHeader dbheader;
            dbheader.magic_number = DB_MAGIC;
            dbheader.version = DB_VERSION;
            dbheader.next_page_id = next_page_id_;
            dbheader.free_list_head = INVALID_PAGE_ID;

            std::array<std::byte, PAGE_SIZE> buffer{};
            std::memcpy(buffer.data(), &dbheader, sizeof(DBHeader));

            // Write header page
            file_.seekp(0, std::ios::beg);
            file_.write(reinterpret_cast<const char*>(buffer.data()), PAGE_SIZE);
            if (!file_) {
                return std::unexpected(IOErr{"Failed to write header page", HaruhiDB::ErrorCode::HeaderWriteFailed});
            }
            file_.flush();
            return {};
        }

        // If header already exists, load it to restore next_page_id_ and free_list_head_
        return LoadHeader();
    }

    std::expected<void, IOErr> DiskManager::LoadHeader()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        file_.clear();
        file_.seekg(0, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buffer.data()), PAGE_SIZE);
        if (!file_) {
            return std::unexpected(IOErr{"Failed to read header page", HaruhiDB::ErrorCode::HeaderReadFailed});
        }

        DBHeader dbheader;
        std::memcpy(&dbheader, buffer.data(), sizeof(DBHeader));

        if (dbheader.magic_number != DB_MAGIC) {
            return std::unexpected(IOErr{"Header magic mismatch", HaruhiDB::ErrorCode::HeaderMagicMismatch});
        }
        next_page_id_ = static_cast<page_id_t>(dbheader.next_page_id);
        free_list_head_ = static_cast<page_id_t>(dbheader.free_list_head);

        if (next_page_id_ < 1) next_page_id_ = 1;
        return {};
    }

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
        if (!file_) return std::unexpected(IOErr{"Failed to write header page", HaruhiDB::ErrorCode::HeaderWriteFailed});
        file_.flush();
        return {};
    }

    std::expected<void, IOErr> DiskManager::ReadPage(page_id_t page_id, page_data_t& data)
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{"File not open.", HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id < 0) {
            return std::unexpected(IOErr{"ReadPage: invalid page_id", HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();
        // get file size
        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        if (offset + PAGE_SIZE > file_size) {
            return std::unexpected(IOErr{"ReadPage: page_id out of range.", HaruhiDB::ErrorCode::ReadPageOutOfRange});
        }

        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);
        if (!file_) return std::unexpected(IOErr{"ReadPage: I/O error", HaruhiDB::ErrorCode::ReadIOError});
        return {};
    }

    std::expected<void, IOErr> DiskManager::WritePage(page_id_t page_id, const page_data_t& data)
    {
        if (!file_.is_open()) {
            return std::unexpected(IOErr{"File not open.", HaruhiDB::ErrorCode::FileNotOpen});
        }

        if (page_id < 0) {
            return std::unexpected(IOErr{"WritePage: invalid page_id", HaruhiDB::ErrorCode::WriteIOError});
        }

        uint64_t offset = static_cast<uint64_t>(page_id) * PAGE_SIZE;

        file_.clear();
        // get file size
        uint64_t file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));

        // If trying to write past file size, allow only append at exactly file_size (即扩展文件)
        if (offset + PAGE_SIZE > file_size) {
            if (offset == file_size) {
                // allowed: append a new page
            } else {
                return std::unexpected(IOErr{"WritePage: page_id out of range.", HaruhiDB::ErrorCode::WriteIOError});
            }
        }

        // Use seekp for writing
        file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.write(reinterpret_cast<const char*>(data.data()), PAGE_SIZE);
        if (!file_) return std::unexpected(IOErr{"WritePage: I/O error", HaruhiDB::ErrorCode::WriteIOError});
        file_.flush();
        return {};
    }

    std::expected<page_id_t, IOErr> DiskManager::AllocatePage()
    {
        std::array<std::byte, PAGE_SIZE> buffer{};

        // reuse from free list if possible
        if (free_list_head_ != INVALID_PAGE_ID) {
            page_id_t recycled_page = free_list_head_;

            auto r = ReadPage(recycled_page, buffer);
            if (!r) return std::unexpected(IOErr{"AllocatePage: read free page failed", HaruhiDB::ErrorCode::AllocateReadFreePageFailed});

            uint64_t next_free_raw;
            std::memcpy(&next_free_raw, buffer.data(), sizeof(uint64_t));

            free_list_head_ = (next_free_raw == static_cast<uint64_t>(INVALID_PAGE_ID))
                                ? INVALID_PAGE_ID
                                : static_cast<page_id_t>(next_free_raw);

            auto p = PersistHeader();
            if (!p) return std::unexpected(IOErr{"AllocatePage: persist header failed", HaruhiDB::ErrorCode::PersistHeaderFailed});

            return recycled_page;
        }

        // allocate at next_page_id_
        page_id_t new_page = next_page_id_;

        // write zeroed page, WritePage 已支持在文件末尾追加
        auto w = WritePage(new_page, buffer);
        if (!w) return std::unexpected(IOErr{"AllocatePage: write new page failed", HaruhiDB::ErrorCode::AllocateWriteFailed});

        next_page_id_++;
        auto p = PersistHeader();
        if (!p) return std::unexpected(IOErr{"AllocatePage: persist header failed", HaruhiDB::ErrorCode::PersistHeaderFailed});

        return new_page;
    }

    std::expected<void, IOErr> DiskManager::DeallocatePage(page_id_t page_id)
    {
        if (!file_.is_open()) return std::unexpected(IOErr{"File not open", HaruhiDB::ErrorCode::FileNotOpen});
        
        std::array<std::byte, PAGE_SIZE> buffer{};
        uint64_t cur_head_raw = static_cast<uint64_t>(free_list_head_);
        std::memcpy(buffer.data(), &cur_head_raw, sizeof(uint64_t));

        auto w = WritePage(page_id, buffer);
        if (!w) return std::unexpected(IOErr{"DeallocatePage: write failed", HaruhiDB::ErrorCode::DeallocateWriteFailed});

        free_list_head_ = page_id;
        auto p = PersistHeader();
        if (!p) return std::unexpected(IOErr{"DeallocatePage: persist header failed", HaruhiDB::ErrorCode::PersistHeaderFailed});

        return {};
    }

    std::expected<void, IOErr> DiskManager::Flush()
    {
        if (!file_.is_open()) return std::unexpected(IOErr{"File not open", HaruhiDB::ErrorCode::FlushFileNotOpen});
        file_.flush();
        return {};
    }

} // namespace storage
} // namespace HaruhiDB