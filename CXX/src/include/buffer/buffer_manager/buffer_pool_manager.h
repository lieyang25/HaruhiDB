/**
 * CXX/src/include/buffer/buffer_manager/buffer_pool_manager.h
 */

#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include "replacer/lru_k_replacer.h"

namespace HaruhiDB
{
namespace buffer
{
    class BufferPoolManager
    {
    public:
        BufferPoolManager(
            size_t pool_size,storage::DiskManager* disk_manager,size_t k =2);
        ~BufferPoolManager() = default;

        storage::Page* FetchPage(page_id_t page_id);
        storage::Page* NewPage(page_id_t *page_id);
        bool UnpinPage(page_id_t page_id,bool is_dirty);
        bool FlushPage(page_id_t page_id);
        bool DeletePage(page_id_t page_id);
        void FlushAllPages();

    private:
        std::expected<frame_id_t,bool> GetVictimFrame();

    private:
        size_t pool_size_;
        std::unordered_map<page_id_t,frame_id_t> page_table_;
        std::vector<storage::Page> pages_;
        mutable std::mutex latch_;
        std::deque<frame_id_t> frame_list_;
        storage::DiskManager* disk_manager_;
        std::unique_ptr<replacer::LruKReplacer> replacer_;
    };
} // namespace buffer
} // namespace HaruhiDB

