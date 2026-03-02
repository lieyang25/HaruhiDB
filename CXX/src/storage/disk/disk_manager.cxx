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
        :path_(path)
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

    std::expected<void,IOErr> DiskManager::OpenFile()
    {
        // Ensure parent directory exists
        try {
            std::filesystem::create_directories(path_.parent_path());
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("Dir error: {}", e.what()), -1});
        }

        // Try to open existing file
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            file_.clear();
            file_.open(path_, std::ios::out | std::ios::binary);
            file_.close();
            file_.open(path_, std::ios::binary | std::ios::out | std::ios::in);
        }

        if (!file_.is_open()) {
            return std::unexpected(IOErr{std::format("Failed to open file: {}", path_.string()), -1});
        }

        // Check file size and set next_page_id_
        try {
            auto file_size = std::filesystem::file_size(path_);
            if (file_size % PAGE_SIZE != 0) {
                return std::unexpected(IOErr{"Database file is corrupted (file_size mismatch)", -4});
            }

            page_id_t pages = static_cast<page_id_t>(file_size / PAGE_SIZE);
            next_page_id_ = (pages == 0) ? 1 : pages;
        } catch (const std::exception&e) {
            return std::unexpected(IOErr{std::format("File size error: {}", e.what()), -5});
        }

        return {};
    }
    std::expected<void,IOErr> DiskManager::InitHeaderIfNeeded()
    {
        uint64_t file_size = 0;
        try {
            file_size = static_cast<uint64_t>(std::filesystem::file_size(path_));
        } catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("File size error: {}", e.what()), -5});
        }

        if (file_size < PAGE_SIZE) {
            DBHeader dbheader;
            dbheader.magic_number = 0x48415255;
            dbheader.version = 1;
            dbheader.next_page_id = next_page_id_;
            dbheader.free_list_head = static_cast<uint64_t>(INVALID_PAGE_ID);

            std::array<std::byte,PAGE_SIZE> buffer{};
            std::memcpy(buffer.data(),&dbheader,sizeof(DBHeader));

            file_.seekg(0,std::ios::beg);
            file_.write(reinterpret_cast<const char*>(buffer.data()),PAGE_SIZE);
            if (!file_) {
                return std::unexpected(IOErr{std::format("Failed to write header page"),-1});
            }
            file_.flush();
            return {};
        }
    }
    std::expected<void,IOErr> DiskManager::LoadHeader()
    {

    }
    std::expected<void,IOErr> DiskManager::PersistHeader()
    {

    }

    std::expected<void,IOErr> DiskManager::ReadPage(page_id_t page_id 
        , page_data_t& data)
    {

    }
    std::expected<void,IOErr> DiskManager::WritePage(page_id_t page_id , const page_data_t& data)
    {

    }
    std::expected<page_id_t,IOErr> DiskManager::AllocatePage()
    {

    }
    std::expected<void,IOErr> DiskManager::DeallocatePage(page_id_t page_id)
    {

    }
    std::expected<void,IOErr> DiskManager::Flush()
    {

    }

} // namespace storage
} // namespace HaruhiDB
