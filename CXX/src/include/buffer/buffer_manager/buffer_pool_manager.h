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
    class BufferPoolManaer
    {
    public:
        BufferPoolManaer();
        ~BufferPoolManaer() = default;
    private:
        
        std::unordered_map<page_id_t,frame_id_t> page_table_;
        std::vector<storage::Page> pages_;
        mutable std::mutex latch_;
        std::deque<frame_id_t> frame_list_;
        storage::DiskManager* disk_manager_;
        replacer::LruKReplacer replacer_;
    };
} // namespace buffer
} // namespace HaruhiDB

