/**
 * CXX/src/buffer/buffer_manager/buffer_pool_manager.cxx
 */

#include "buffer/buffer_manager/buffer_pool_manager.h"

namespace HaruhiDB
{
namespace buffer
{
    BufferPoolManager::BufferPoolManager(
        size_t pool_size,storage::DiskManager* disk_manager,size_t k)
    {
        pool_size_ = pool_size;
        disk_manager_ =disk_manager;
        pages_.resize(pool_size);
        replacer_ = std::make_unique<replacer::LruKReplacer>(pool_size,k);
        for (page_id_t fid = 0;fid < pool_size;fid++) {
            frame_list_.push_back(fid);
        }
        
    }
    storage::Page* BufferPoolManager::FetchPage(page_id_t page_id)
    {
        
    }
    storage::Page* BufferPoolManager::NewPage(page_id_t *page_id)
    {

    }
    bool BufferPoolManager::UnpinPage(page_id_t page_id,bool is_dirty)
    {

    }
    bool BufferPoolManager::FlushPage(page_id_t page_id)
    {

    }
    bool BufferPoolManager::DeletePage(page_id_t page_id)
    {

    }
    void BufferPoolManager::FlushAllPages()
    {

    }
    std::expected<frame_id_t,bool> BufferPoolManager::GetVictimFrame()
    {
        std::lock_guard<std::mutex> guard(latch_);

        frame_id_t fid;
        if (!frame_list_.empty()) {
            fid = frame_list_.front();
            frame_list_.pop_front();
            return fid;
        }

        if (replacer_->Victim(fid)) {
            return fid;
        }

        return std::unexpected(false);
    }
} // namespace buffer
} // namespace HaruhiDB
 