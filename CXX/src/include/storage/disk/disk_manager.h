/**
 * CXX/src/include/storage/disk/disk_manager.h
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
     * To return msg ,if some error happen.
     */

    struct DBHeader
    {
        uint32_t magic_number;
        uint32_t version;
        page_id_t next_page_id;
        page_id_t free_list_head;
    };
    
    struct IOErr {
        std::string msg;
        HaruhiDB::ErrorCode err_code;
    };

    class DiskManager
    {
    public:
        explicit DiskManager(const std::filesystem::path& path);
        ~DiskManager();
        std::expected<void,IOErr> ReadPage(page_id_t page_id , page_data_t& data);
        std::expected<void,IOErr> WritePage(page_id_t page_id , const page_data_t& data);
        std::expected<page_id_t,IOErr> AllocatePage();
        std::expected<void,IOErr> DeallocatePage(page_id_t page_id);
        std::expected<void,IOErr> Flush();
    private:
        std::expected<void,IOErr> OpenFile();
        std::expected<void,IOErr> InitHeaderIfNeeded();
        std::expected<void,IOErr> LoadHeader();
        std::expected<void,IOErr> PersistHeader();

    private:
        std::filesystem::path path_;
        std::fstream file_;
        page_id_t next_page_id_{1};
        page_id_t free_list_head_{INVALID_PAGE_ID};
        static constexpr uint32_t DB_MAGIC = 0x48415255;
        static constexpr uint32_t DB_VERSION = 1;
    };
} // namespace storage
} // namespace HaruhiDB