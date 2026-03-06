/**
 * CXX/src/include/buffer/buffer_manager/buffer_pool_manager.h
 */

#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include "buffer/replacer/lru_k_replacer.h"

namespace HaruhiDB
{
namespace buffer
{
    class BufferPoolManager
    {
    public:
        BufferPoolManager(size_t pool_size,storage::DiskManager* disk_manager,size_t k =2);
        ~BufferPoolManager() = default;

        std::expected<storage::Page*,bool> FetchPage(page_id_t page_id);
        std::expected<storage::Page*,bool> NewPage(page_id_t *page_id);
        bool UnpinPage(page_id_t page_id,bool is_dirty);
        bool DeletePage(page_id_t page_id);
        std::expected<bool,bool> FlushPage(page_id_t page_id);
        std::expected<bool,bool> FlushAllPages();

    private:
        std::expected<frame_id_t,bool> GetVictimFrame();

    private:
        /** 缓冲池中页面的最大数量（即 Frame 的总数） */
        size_t pool_size_;

        /** 映射表：用于快速通过 page_id（逻辑页 ID）找到对应的 frame_id（物理内存索引） */
        std::unordered_map<page_id_t, frame_id_t> page_table_;

        /** 实际存储页面数据的连续内存区域，数组长度为 pool_size_ */
        std::vector<storage::Page> pages_;

        /** 用于保证 BufferPoolManager 内部数据结构（如 page_table_）线程安全的互斥锁 */
        mutable std::mutex latch_;

        /** 空闲列表：保存当前未被使用的 frame_id，以便快速分配新页面 */
        std::deque<frame_id_t> frame_list_;

        /** 磁盘管理器指针：负责将页面从缓冲池刷入磁盘，或从磁盘读取到缓冲池 */
        storage::DiskManager* disk_manager_;

        /** 页面置换器：实现 LRU-K 算法，当缓冲池满时决定哪个页面应该被淘汰 */
        std::unique_ptr<replacer::LruKReplacer> replacer_;
    };
} // namespace buffer
} // namespace HaruhiDB

