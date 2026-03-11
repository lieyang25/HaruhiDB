/**
 * CXX/src/include/buffer/buffer_pool_manager/buffer_pool_manager.h
 */

#pragma once

#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include "buffer/replacer/lru_k_replacer.h"

#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace HaruhiDB
{
namespace buffer
{
    enum class BufferPoolErrCode : int {
        InvalidPageId = 1,
        NullPageIdOutput,
        PageNotFound,
        NoAvailableFrame,
        DiskReadFailed,
        DiskWriteFailed,
        DiskAllocateFailed,
        DiskDeallocateFailed
    };

    struct BufferPoolErr {
        std::string msg;
        BufferPoolErrCode err_code;
    };

    class BufferPoolManager
    {
    public:
        BufferPoolManager(size_t pool_size,storage::DiskManager* disk_manager,size_t k =2);
        ~BufferPoolManager() = default;

        std::expected<storage::Page*,BufferPoolErr> FetchPage(page_id_t page_id);
        std::expected<storage::Page*,BufferPoolErr> NewPage(page_id_t *page_id);
        bool UnpinPage(page_id_t page_id,bool is_dirty);
        bool DeletePage(page_id_t page_id);
        std::expected<void,BufferPoolErr> FlushPage(page_id_t page_id);
        std::expected<void,BufferPoolErr> FlushAllPages();

    private:
        std::expected<frame_id_t,BufferPoolErr> GetVictimFrame();

    private:
        /** 缓冲池中页面的最大数量（即 Frame 的总数） */
        size_t pool_size_;

        /** 映射表：用于快速通过 page_id（逻辑页 ID）找到对应的 frame_id（物理内存索引） */
        std::unordered_map<page_id_t, frame_id_t> page_table_;

        /** 存储所有 frame 对应的 Page。使用 deque 以支持不可移动对象（Page 含 shared_mutex）。 */
        std::deque<storage::Page> pages_;

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
