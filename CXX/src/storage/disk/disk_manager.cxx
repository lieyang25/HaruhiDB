/**
 * CXX/src/storage/disk/disk_manager.cxx
 */

#include "storage/disk/disk_manager.h"

namespace HaruhiDB
{
namespace storage
{
    DiskManager::DiskManager(const std::filesystem::path& path)
        :path_(path)
    {
        
    }
    DiskManager::~DiskManager()
    {
        
    }

    std::expected<void,IOErr> DiskManager::OpenFile()
    {

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
