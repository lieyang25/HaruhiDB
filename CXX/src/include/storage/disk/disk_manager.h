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
    /**
     * DBHeader structure is stored in the first page of the file, 
     * containing metadata about the database file, 
     * such as magic number, version, next_page_id and free_list_head.
     */
    #pragma pack(push, 1)
    struct DBHeader
    {
        uint32_t magic_number;
        uint32_t version;
        page_id_t next_page_id;
        page_id_t free_list_head;
    };
    #pragma pace(pop)
    static_assert(sizeof(DBHeader) == 16, "DBHeader size must be 16 bytes");
    /**
     * IOErr is used to return error message and error code 
     * when disk operations fail,
     */
    struct IOErr {
        std::string msg;
        HaruhiDB::ErrorCode err_code;
    };

    /**
     * DiskManager is responsible for managing the disk storage of the database,
     * including reading/writing pages, allocating/deallocating pages, and maintaining the free page
     */
    class DiskManager
    {
    public:
        explicit DiskManager(const std::filesystem::path& path);
        ~DiskManager();
        // Basic page operations
        std::expected<void,IOErr> ReadPage(page_id_t page_id , page_data_t& data);
        std::expected<void,IOErr> WritePage(page_id_t page_id , const page_data_t& data);
        std::expected<page_id_t,IOErr> AllocatePage();
        std::expected<void,IOErr> DeallocatePage(page_id_t page_id);
        std::expected<void,IOErr> Flush();
    private:
        //helper functions
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