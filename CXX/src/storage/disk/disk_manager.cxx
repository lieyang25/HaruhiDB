/**
 * CXX/src/storage/disk/disk_manager.cxx
 */

#include "storage/disk/disk_manager.h"
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
        try {
            std::filesystem::create_directories(path_.parent_path());
        }
        catch (const std::exception& e) {
            return std::unexpected(IOErr{std::format("Dir error: {}", e.what()), -1});
        }

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

        try {
            auto size = std::filesystem::file_size(path_);
            if (size % PAGE_SIZE != 0) {
                return std::unexpected(IOErr{"Database file is corrupted (size mismatch)", -4});
            }

            page_id_t pages = static_cast<page_id_t>(size / PAGE_SIZE);
            next_page_id_ = (pages == 0) ? 1 : pages;
        }
        catch (const std::exception&e) {
            return std::unexpected(IOErr{std::format("File size error: {}", e.what()), -5});
        }

        return {};
    }
    std::expected<void,IOErr> DiskManager::InitHeaderIfNeeded()
    {

    }
    std::expected<void,IOErr> DiskManager::LoagHeader()
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
