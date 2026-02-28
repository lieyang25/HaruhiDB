#pragma once

/**
 * CXX/src/include/storage/disk/disk_manager.h
 */

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
    struct IOErr { 
        std::string msg;
        int err_code;
    };

    class DiskManager
    {
    private:
        std::filesystem::path path_;
        std::fstream file_;
        page_id_t next_page;
    public:
        explicit DiskManager(const std::filesystem::path& path);
        ~DiskManager();
        std::expected<void,IOErr> ReadPage(page_id_t page_id , char* data);
        std::expected<void,IOErr> WritePage(page_id_t page_id , const char* data);
        std::expected<page_id_t,IOErr> AllocatePage();
    };
} // namespace storage
} // namespace HaruhiDB