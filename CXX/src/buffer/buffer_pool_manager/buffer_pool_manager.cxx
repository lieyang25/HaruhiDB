/**
 * CXX/src/buffer/buffer_pool_manager/buffer_pool_manager.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 BufferPoolManager 的核心内存页管理逻辑。
 *
 * 主要完成：
 *
 * 1. 从磁盘加载页面到缓冲池
 * 2. 在缓冲池中分配新页面
 * 3. 维护 pin / dirty / page_table 状态
 * 4. 满池时选择 victim frame
 * 5. 将脏页刷回磁盘
 * 6. 删除页面并回收 frame
 *
 *
 * ========================= 核心流程 =========================
 *
 * FetchPage:
 *   page_id -> page_table_ 查找
 *           -> 命中则直接返回
 *           -> 未命中则申请 frame
 *           -> 必要时淘汰旧页
 *           -> 从磁盘读入新页
 *
 * NewPage:
 *   向 DiskManager 申请 page_id
 *           -> 获取 frame
 *           -> 必要时淘汰旧页
 *           -> 初始化空白页
 *
 * DeletePage:
 *   若页在内存中：
 *       检查 pin
 *       回收磁盘页
 *       清理 page_table / replacer / frame
 *
 *
 * ========================= 与其他模块的联动 =========================
 *
 * BufferPoolManager
 *   ├── 调用 DiskManager 进行读写、分配、回收
 *   ├── 调用 LruKReplacer 维护替换状态
 *   └── 维护 Page 的运行时元数据
 */

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"

#include <stdexcept>

namespace HaruhiDB
{
namespace buffer
{
    namespace
    {
        std::unexpected<BufferPoolErr> MakeBpmErr(BufferPoolErrCode err_code, const std::string& msg)
        {
            return std::unexpected(BufferPoolErr{msg, err_code});
        }
    } // namespace

    /**
     * @param pool_size    缓冲池 frame 数量
     * @param disk_manager 磁盘管理器
     * @param k            LRU-K 中的 K
     */
    BufferPoolManager::BufferPoolManager(
        size_t pool_size, storage::DiskManager* disk_manager, size_t k)
    {
        if (disk_manager == nullptr) {
            throw std::invalid_argument("BufferPoolManager: disk manager must not be null");
        }

        pool_size_ = pool_size;
        disk_manager_ = disk_manager;
        pages_.resize(pool_size);
        replacer_ = std::make_unique<replacer::LruKReplacer>(pool_size, k);

        for (frame_id_t fid = 0; fid < static_cast<frame_id_t>(pool_size); fid++) {
            frame_list_.push_back(fid);
        }
    }

    /**
     * @param page_id 目标页号
     * @return 成功时返回页指针
     */
    std::expected<storage::Page*, BufferPoolErr> BufferPoolManager::FetchPage(page_id_t page_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查基础参数合法性。
        if (disk_manager_ == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullDiskManager, "FetchPage: disk manager is null");
        }
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "FetchPage: invalid page id");
        }

        // step 2: 若页面已在缓冲池中，则直接 pin 并返回。
        frame_id_t fid;
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            fid = it->second;
            pages_[fid].Pin();
            replacer_->SetEvictable(fid, false);
            replacer_->RecordAccess(fid);
            return &pages_[fid];
        }

        // step 3: 页面未命中时，申请一个可用 frame。
        auto victim_frame = GetVictimFrame();
        if (!victim_frame.has_value()) {
            return std::unexpected(victim_frame.error());
        }

        fid = victim_frame.value();
        storage::Page& page = pages_[fid];
        const page_id_t old_page_id = page.PageId();

        // step 4: 若 victim 为脏页，则先刷回磁盘。
        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(), page.Data());
            if (!write.has_value()) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskWriteFailed,
                    "FetchPage: flush victim failed: " + write.error().msg);
            }

            page.ClearDirty();
        }

        // step 5: 从磁盘读取新页面到该 frame。
        auto read = disk_manager_->ReadPage(page_id, page.Data());
        if (!read.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskReadFailed,
                "FetchPage: load page failed: " + read.error().msg);
        }

        // step 6: 移除旧映射，写入新映射，并更新页运行时状态。
        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }

        page.ResetMetaData(page_id);
        page.Pin();
        replacer_->SetEvictable(fid, false);
        replacer_->RecordAccess(fid);
        page_table_[page_id] = fid;

        return &pages_[fid];
    }

    /**
     * @param page_id 输出参数，返回新页号
     */
    std::expected<storage::Page*, BufferPoolErr> BufferPoolManager::NewPage(page_id_t* page_id)
    {
        return NewPage(page_id, storage::PageType::HEAP);
    }

    /**
     * @param page_id   输出参数，返回新页号
     * @param page_type 新页类型
     */
    std::expected<storage::Page*, BufferPoolErr> BufferPoolManager::NewPage(
        page_id_t* page_id, storage::PageType page_type)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查参数合法性。
        if (disk_manager_ == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullDiskManager, "NewPage: disk manager is null");
        }
        if (page_id == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullPageIdOutput, "NewPage: output page_id pointer is null");
        }
        if (page_type == storage::PageType::INVALID) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageType, "NewPage: page type must be valid");
        }

        // step 2: 先向磁盘层申请新的 page_id。
        auto new_page_id = disk_manager_->AllocatePage();
        if (!new_page_id.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskAllocateFailed,
                "NewPage: allocate page failed: " + new_page_id.error().msg);
        }

        // step 3: 再为该新页申请一个 frame；若失败则回滚 page_id。
        auto frame = GetVictimFrame();
        if (!frame.has_value()) {
            auto rollback = disk_manager_->DeallocatePage(new_page_id.value());
            if (!rollback.has_value()) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskDeallocateFailed,
                    "NewPage: rollback deallocate failed: " + rollback.error().msg);
            }
            return std::unexpected(frame.error());
        }

        frame_id_t fid = frame.value();
        storage::Page& page = pages_[fid];
        const page_id_t old_page_id = page.PageId();

        // step 4: 若即将复用的 frame 中是脏页，则先刷回磁盘。
        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(), page.Data());
            if (!write.has_value()) {
                auto rollback = disk_manager_->DeallocatePage(new_page_id.value());
                if (!rollback.has_value()) {
                    return MakeBpmErr(
                        BufferPoolErrCode::DiskDeallocateFailed,
                        "NewPage: rollback deallocate failed: " + rollback.error().msg);
                }
                return MakeBpmErr(
                    BufferPoolErrCode::DiskWriteFailed,
                    "NewPage: flush victim failed: " + write.error().msg);
            }
            page.ClearDirty();
        }

        // step 5: 清理旧映射，并把该 frame 初始化为新页。
        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }

        page.InitBlank(new_page_id.value(), page_type);
        page.Pin();
        replacer_->SetEvictable(fid, false);
        replacer_->RecordAccess(fid);
        page_table_[new_page_id.value()] = fid;
        *page_id = new_page_id.value();

        return &pages_[fid];
    }

    /**
     * @param page_id 目标页号
     * @param is_dirty 是否将页面标记为脏页
     */
    bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty)
    {
        return UnpinPageEx(page_id, is_dirty).has_value();
    }

    /**
     * @param page_id 目标页号
     * @param is_dirty 是否将页面标记为脏页
     */
    std::expected<void, BufferPoolErr> BufferPoolManager::UnpinPageEx(page_id_t page_id, bool is_dirty)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查页号并查找对应 frame。
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "UnpinPage: invalid page id");
        }

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return MakeBpmErr(BufferPoolErrCode::PageNotFound, "UnpinPage: page not found in buffer pool");
        }

        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];

        // step 2: 检查该页当前是否真的被 pin。
        if (page.PinCount() <= 0) {
            return MakeBpmErr(BufferPoolErrCode::PageNotPinned, "UnpinPage: page pin count already zero");
        }

        // step 3: 更新 dirty 状态并执行 unpin。
        if (is_dirty) {
            page.MarkDirty();
        }
        page.UnPin();

        // step 4: 若 pin_count 归零，则允许 replacer 选择该页。
        if (page.PinCount() == 0) {
            replacer_->SetEvictable(fid, true);
        }

        return {};
    }

    /**
     * @param page_id 目标页号
     */
    std::expected<void, BufferPoolErr> BufferPoolManager::FlushPage(page_id_t page_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查依赖并定位目标页面。
        if (disk_manager_ == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullDiskManager, "FlushPage: disk manager is null");
        }

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return MakeBpmErr(BufferPoolErrCode::PageNotFound, "FlushPage: page not found in buffer pool");
        }

        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];

        // step 2: 将当前页内容写回磁盘。
        auto write = disk_manager_->WritePage(page.PageId(), page.Data());
        if (!write.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskWriteFailed,
                "FlushPage: write page failed: " + write.error().msg);
        }

        // step 3: 写回成功后清除脏标记。
        page.ClearDirty();
        return {};
    }

    /**
     * 刷新所有脏页到磁盘。
     */
    std::expected<void, BufferPoolErr> BufferPoolManager::FlushAllPages()
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查磁盘管理器是否存在。
        if (disk_manager_ == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullDiskManager, "FlushAllPages: disk manager is null");
        }

        // step 2: 遍历缓冲池中的所有映射页，只刷脏页。
        for (auto const& [pid, fid] : page_table_) {
            storage::Page& page = pages_[fid];
            if (!page.IsDirty()) {
                continue;
            }

            auto write = disk_manager_->WritePage(page.PageId(), page.Data());
            if (!write.has_value()) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskWriteFailed,
                    "FlushAllPages: write page failed: " + write.error().msg);
            }

            page.ClearDirty();
        }

        return {};
    }

    /**
     * @param page_id 目标页号
     */
    bool BufferPoolManager::DeletePage(page_id_t page_id)
    {
        return DeletePageEx(page_id).has_value();
    }

    /**
     * @param page_id 目标页号
     */
    std::expected<void, BufferPoolErr> BufferPoolManager::DeletePageEx(page_id_t page_id)
    {
        std::lock_guard<std::mutex> guard(latch_);

        // step 1: 检查基础参数。
        if (disk_manager_ == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullDiskManager, "DeletePage: disk manager is null");
        }
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "DeletePage: invalid page id");
        }

        auto it = page_table_.find(page_id);

        // step 2: 若页面不在内存中，则直接请求磁盘层回收 page_id。
        if (it == page_table_.end()) {
            auto dealloc = disk_manager_->DeallocatePage(page_id);
            if (!dealloc.has_value()) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskDeallocateFailed,
                    "DeletePage: deallocate page failed: " + dealloc.error().msg);
            }
            return {};
        }

        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];

        // step 3: 若页面仍被 pin，则禁止删除。
        if (page.PinCount() > 0) {
            return MakeBpmErr(BufferPoolErrCode::PagePinned, "DeletePage: page is currently pinned");
        }

        // step 4: 先回收磁盘页，再清理内存映射与 frame。
        auto dealloc = disk_manager_->DeallocatePage(page_id);
        if (!dealloc.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskDeallocateFailed,
                "DeletePage: deallocate page failed: " + dealloc.error().msg);
        }

        page_table_.erase(it);
        replacer_->Remove(fid);
        page.ResetMetaData(INVALID_PAGE_ID);
        frame_list_.push_back(fid);

        return {};
    }

    /**
     * @note 优先返回空闲 frame，其次尝试从 replacer 中淘汰 victim
     */
    std::expected<frame_id_t, BufferPoolErr> BufferPoolManager::GetVictimFrame()
    {
        frame_id_t fid;

        // step 1: 优先从 free list 直接取空闲 frame。
        if (!frame_list_.empty()) {
            fid = frame_list_.front();
            frame_list_.pop_front();
            return fid;
        }

        // step 2: 若没有空闲 frame，则尝试让 replacer 选 victim。
        if (replacer_->Victim(fid)) {
            return fid;
        }

        // step 3: 两种来源都不可用，说明当前没有可用 frame。
        return MakeBpmErr(BufferPoolErrCode::NoAvailableFrame, "GetVictimFrame: no evictable frame available");
    }

} // namespace buffer
} // namespace HaruhiDB