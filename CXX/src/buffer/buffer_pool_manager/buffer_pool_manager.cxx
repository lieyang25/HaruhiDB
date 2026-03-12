/**
 * CXX/src/buffer/buffer_pool_manager/buffer_pool_manager.cxx
 */

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"

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

    BufferPoolManager::BufferPoolManager(
        size_t pool_size, storage::DiskManager* disk_manager, size_t k)
    {
        pool_size_ = pool_size;
        disk_manager_ = disk_manager;
        pages_.resize(pool_size);
        replacer_ = std::make_unique<replacer::LruKReplacer>(pool_size, k);
        for (frame_id_t fid = 0; fid < static_cast<frame_id_t>(pool_size); fid++) {
            frame_list_.push_back(fid);
        }
    }

    std::expected<storage::Page*,BufferPoolErr> BufferPoolManager::FetchPage(page_id_t page_id)
    {
        // TODO: 
        // 1. 在 page_table_ 中查找 page_id。如果存在，记录 access，固定(pin)该页并返回。
        // 2. 如果不存在，调用 GetVictimFrame() 获取一个可用的 frame_id。
        // 3. 如果 Victim 是脏页(is_dirty)，将其写回磁盘。
        // 4. 更新 page_table_，从磁盘读取 page 存储到该 frame。
        // 5. 设置该 Page 的元数据，调用 replacer_->RecordAccess()。
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "FetchPage: invalid page id");
        }

        frame_id_t fid;
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            fid = it->second;
            pages_[fid].Pin();
            replacer_->SetEvictable(fid,false);
            replacer_->RecordAccess(fid);
            return &pages_[fid];
        }

        auto victim_frame = GetVictimFrame();
        if (!victim_frame.has_value()) {
            return std::unexpected(victim_frame.error());
        }

        fid = victim_frame.value();
        storage::Page& page = pages_[fid];
        const page_id_t old_page_id = page.PageId();

        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(),page.Data());
            if ( !write.has_value() ) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskWriteFailed,
                    "FetchPage: flush victim failed: " + write.error().msg);
            }
            
            page.ClearDirty();
        }

        auto read = disk_manager_->ReadPage(page_id,page.Data());
        if (!read.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskReadFailed,
                "FetchPage: load page failed: " + read.error().msg);
        }
        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }

        page.ResetMetaData(page_id);
        page.Pin();
        replacer_->SetEvictable(fid,false);
        replacer_->RecordAccess(fid);
        page_table_[page_id] = fid;
        return &pages_[fid];
    }

    std::expected<storage::Page*,BufferPoolErr> BufferPoolManager::NewPage(page_id_t *page_id)
    {
        return NewPage(page_id, storage::PageType::HEAP);
    }

    std::expected<storage::Page*,BufferPoolErr> BufferPoolManager::NewPage(
        page_id_t *page_id, storage::PageType page_type)
    {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == nullptr) {
            return MakeBpmErr(BufferPoolErrCode::NullPageIdOutput, "NewPage: output page_id pointer is null");
        }
        if (page_type == storage::PageType::INVALID) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageType, "NewPage: page type must be valid");
        }

        auto new_page_id = disk_manager_->AllocatePage();
        if (!new_page_id.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskAllocateFailed,
                "NewPage: allocate page failed: " + new_page_id.error().msg);
        }

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

        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(),page.Data());
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

        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }

        page.InitBlank(new_page_id.value(),page_type);
        page.Pin();
        replacer_->SetEvictable(fid,false);
        replacer_->RecordAccess(fid);
        page_table_[new_page_id.value()] = fid;
        *page_id = new_page_id.value();
        return &pages_[fid];
    }

    bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty)
    {
        return UnpinPageEx(page_id, is_dirty).has_value();
    }

    std::expected<void,BufferPoolErr> BufferPoolManager::UnpinPageEx(page_id_t page_id, bool is_dirty)
    {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "UnpinPage: invalid page id");
        }
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return MakeBpmErr(BufferPoolErrCode::PageNotFound, "UnpinPage: page not found in buffer pool");
        }
        frame_id_t fid = it->second;
        storage::Page &page = pages_[fid];

        if (page.PinCount() <= 0) {
            return MakeBpmErr(BufferPoolErrCode::PageNotPinned, "UnpinPage: page pin count already zero");
        }

        if (is_dirty) {
            page.MarkDirty();
        }
        page.UnPin();

        if (page.PinCount() == 0) {
            replacer_->SetEvictable(fid,true);
        }

        return {};
    }

    std::expected<void,BufferPoolErr> BufferPoolManager::FlushPage(page_id_t page_id)
    {
        // TODO:
        // 1. 检查 page_id 是否有效且在缓冲池中。
        // 2. 调用 disk_manager_->WritePage() 将数据写入物理磁盘。
        // 3. 重置 Page 的 is_dirty 标记。
        std::lock_guard<std::mutex> guard(latch_);
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return MakeBpmErr(BufferPoolErrCode::PageNotFound, "FlushPage: page not found in buffer pool");
        }
        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];
        auto write = disk_manager_->WritePage(page.PageId(),page.Data());
        if (!write.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskWriteFailed,
                "FlushPage: write page failed: " + write.error().msg);
        }
        page.ClearDirty();
        return {};
    }

    std::expected<void,BufferPoolErr> BufferPoolManager::FlushAllPages()
    {
        // TODO: 遍历 page_table_，对每个有效的 page_id 调用 FlushPage。
        std::lock_guard<std::mutex> guard(latch_);

        for (auto const& [pid,fid] : page_table_) {
            storage::Page& page = pages_[fid];
            if (!page.IsDirty()) {
                continue;
            }
            auto write = disk_manager_->WritePage(page.PageId(),page.Data());
            if (!write.has_value()) {
                return MakeBpmErr(
                    BufferPoolErrCode::DiskWriteFailed,
                    "FlushAllPages: write page failed: " + write.error().msg);
            }
            page.ClearDirty();
        }
        return {};
    }
    bool BufferPoolManager::DeletePage(page_id_t page_id)
    {
        return DeletePageEx(page_id).has_value();
    }

    std::expected<void,BufferPoolErr> BufferPoolManager::DeletePageEx(page_id_t page_id)
    {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return MakeBpmErr(BufferPoolErrCode::InvalidPageId, "DeletePage: invalid page id");
        }

        auto it = page_table_.find(page_id);
        
        // 逻辑：不在内存就去磁盘回收；在内存就先检查 PinCount，再回收磁盘，最后清理内存。
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

        if (page.PinCount() > 0) {
            return MakeBpmErr(BufferPoolErrCode::PagePinned, "DeletePage: page is currently pinned");
        }

        // 磁盘先行：如果磁盘都没法回收这个 ID，内存映射就该保留
        auto dealloc = disk_manager_->DeallocatePage(page_id);
        if (!dealloc.has_value()) {
            return MakeBpmErr(
                BufferPoolErrCode::DiskDeallocateFailed,
                "DeletePage: deallocate page failed: " + dealloc.error().msg);
        }

        // 内存清理
        page_table_.erase(it);
        replacer_->Remove(fid);
        page.ResetMetaData(INVALID_PAGE_ID);
        frame_list_.push_back(fid);

        return {};
    }


    std::expected<frame_id_t, BufferPoolErr> BufferPoolManager::GetVictimFrame()
    {

        frame_id_t fid;
        // 优先从空闲列表(Free List)获取
        if (!frame_list_.empty()) {
            fid = frame_list_.front();
            frame_list_.pop_front();
            return fid;
        }

        // 其次从替换器(Replacer)中选择淘汰页
        if (replacer_->Victim(fid)) {
            return fid;
        }

        return MakeBpmErr(BufferPoolErrCode::NoAvailableFrame, "GetVictimFrame: no evictable frame available");
    }
} // namespace buffer
} // namespace HaruhiDB
