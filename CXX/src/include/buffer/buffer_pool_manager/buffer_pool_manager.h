/**
 * CXX/src/include/buffer/buffer_pool_manager/buffer_pool_manager.h
 *
 * ========================= 设计目标 =========================
 *
 * BufferPoolManager 负责管理数据库页面在内存中的驻留与替换。
 *
 * 它把磁盘页加载到缓冲池 frame 中，
 * 并对外提供获取页、新建页、释放页、刷盘、删除页等操作。
 *
 * 核心职责：
 *
 * 1. 维护 page_id 到 frame_id 的映射
 * 2. 管理空闲 frame 与可淘汰 frame
 * 3. 与 DiskManager 协作完成页的装入与刷回
 * 4. 与 LruKReplacer 协作完成页面替换
 * 5. 维护 Page 的 pin / dirty / metadata 状态
 *
 *
 * ========================= 为什么需要 BufferPoolManager =========================
 *
 * 上层模块希望按“页对象”访问数据，
 * 但磁盘 I/O 代价高，不能每次都直接读写磁盘。
 *
 * BufferPoolManager 提供一层内存缓存：
 *
 * - 热页可以常驻内存
 * - 脏页可以延迟刷盘
 * - 满池时可通过替换策略回收 frame
 *
 *
 * ========================= BufferPoolManager 在系统中的位置 =========================
 *
 * TableHeap / B+Tree / Executor
 *            │
 *            ▼
 *   BufferPoolManager
 *      ├── page_table_
 *      ├── frame_list_
 *      ├── pages_
 *      ├── LruKReplacer
 *      └── DiskManager
 *
 *
 * ========================= 内部组织 =========================
 *
 *   page_id ----> page_table_ ----> frame_id ----> pages_[frame_id]
 *
 *   可用 frame 来源：
 *
 *   1. frame_list_ 中的空闲 frame
 *   2. replacer_ 选出的 victim frame
 */

#pragma once

#include "buffer/replacer/lru_k_replacer.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"

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
        InvalidPageType,
        NullDiskManager,
        NullPageIdOutput,
        PageNotFound,
        PagePinned,
        PageNotPinned,
        NoAvailableFrame,
        DiskReadFailed,
        DiskWriteFailed,
        DiskAllocateFailed,
        DiskDeallocateFailed
    };

    /**
     * BufferPoolManager 相关错误。
     */
    struct BufferPoolErr
    {
        std::string msg;
        BufferPoolErrCode err_code;
    };

    class BufferPoolManager
    {
    public:
        /**
         * @param pool_size    缓冲池 frame 数量
         * @param disk_manager 磁盘管理器
         * @param k            LRU-K 中的 K
         */
        BufferPoolManager(size_t pool_size, storage::DiskManager* disk_manager, size_t k = 2);

        ~BufferPoolManager() = default;

        /**
         * 获取一个已有页面。
         *
         * @param page_id 目标页号
         * @return 成功时返回页指针
         */
        std::expected<storage::Page*, BufferPoolErr> FetchPage(page_id_t page_id);

        /**
         * 新建一个 HEAP 类型页面。
         *
         * @param page_id 输出参数，返回新页号
         */
        std::expected<storage::Page*, BufferPoolErr> NewPage(page_id_t* page_id);

        /**
         * 新建一个指定类型页面。
         *
         * @param page_id   输出参数，返回新页号
         * @param page_type 新页类型
         */
        std::expected<storage::Page*, BufferPoolErr> NewPage(
            page_id_t* page_id, storage::PageType page_type);

        /**
         * 释放一个页面的 pin。
         *
         * @param page_id 目标页号
         * @param is_dirty 是否将页面标记为脏页
         */
        bool UnpinPage(page_id_t page_id, bool is_dirty);

        /**
         * 删除一个页面。
         *
         * @param page_id 目标页号
         */
        bool DeletePage(page_id_t page_id);

        /**
         * 释放一个页面的 pin，带错误返回。
         */
        std::expected<void, BufferPoolErr> UnpinPageEx(page_id_t page_id, bool is_dirty);

        /**
         * 删除一个页面，带错误返回。
         */
        std::expected<void, BufferPoolErr> DeletePageEx(page_id_t page_id);

        /**
         * 刷新指定页面到磁盘。
         *
         * @param page_id 目标页号
         */
        std::expected<void, BufferPoolErr> FlushPage(page_id_t page_id);

        /**
         * 刷新所有脏页到磁盘。
         */
        std::expected<void, BufferPoolErr> FlushAllPages();

    private:
        /**
         * 获取一个可用 frame。
         *
         * @note 优先使用空闲 frame，其次从 replacer 中选择 victim
         */
        std::expected<frame_id_t, BufferPoolErr> GetVictimFrame();

    private:
        /// 缓冲池 frame 总数
        size_t pool_size_;

        /// page_id 到 frame_id 的映射
        std::unordered_map<page_id_t, frame_id_t> page_table_;

        /// 所有 frame 对应的 Page 存储
        std::deque<storage::Page> pages_;

        /// 保护内部状态的互斥锁
        mutable std::mutex latch_;

        /// 当前空闲 frame 列表
        std::deque<frame_id_t> frame_list_;

        /// 磁盘管理器
        storage::DiskManager* disk_manager_;

        /// 页面替换器
        std::unique_ptr<replacer::LruKReplacer> replacer_;
    };
} // namespace buffer
} // namespace HaruhiDB