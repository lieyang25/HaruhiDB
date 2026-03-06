/**
 * CXX/src/buffer/buffer_pool_manager/buffer_pool_manager.cxx
 */

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"

namespace HaruhiDB
{
namespace buffer
{
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

    std::expected<storage::Page*,bool> BufferPoolManager::FetchPage(page_id_t page_id)
    {
        // TODO: 
        // 1. 在 page_table_ 中查找 page_id。如果存在，记录 access，固定(pin)该页并返回。
        // 2. 如果不存在，调用 GetVictimFrame() 获取一个可用的 frame_id。
        // 3. 如果 Victim 是脏页(is_dirty)，将其写回磁盘。
        // 4. 更新 page_table_，从磁盘读取 page 存储到该 frame。
        // 5. 设置该 Page 的元数据，调用 replacer_->RecordAccess()。
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == INVALID_PAGE_ID) {
            return std::unexpected(false);
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
            return std::unexpected(false);
        }

        fid = victim_frame.value();
        storage::Page& page = pages_[fid];
        const page_id_t old_page_id = page.PageId();

        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(),page.Data());
            if ( !write.has_value() ) {
                return std::unexpected(false);
            }
            
            page.ClearDirty();
        }

        auto read = disk_manager_->ReadPage(page_id,page.Data());
        if (!read.has_value()) {
            return std::unexpected(false);
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

    std::expected<storage::Page*,bool> BufferPoolManager::NewPage(page_id_t *page_id)
    {
        // TODO:
        // 1. 调用 GetVictimFrame()。如果没有可用 frame，返回 nullptr。
        // 2. 调用 disk_manager_->AllocatePage() 分配新的物理页 ID。
        // 3. 如果 Victim 是脏页，写回磁盘。
        // 4. 清空该 frame 对应的 Page 对象（ResetMemory）。
        // 5. 更新 page_table_ 和 replacer_ 的状态。
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == nullptr) {
            return std::unexpected(false);
        }

        auto new_page_id = disk_manager_->AllocatePage();
        if (!new_page_id.has_value()) {
            return std::unexpected(false);
        }

        auto frame = GetVictimFrame();
        if (!frame.has_value()) {
            disk_manager_->DeallocatePage(new_page_id.value());
            return std::unexpected(false);
        }

        frame_id_t fid = frame.value();
        storage::Page& page = pages_[fid];
        const page_id_t old_page_id = page.PageId();

        if (page.IsDirty()) {
            auto write = disk_manager_->WritePage(page.PageId(),page.Data());
            if (!write.has_value()) {
                (void)disk_manager_->DeallocatePage(new_page_id.value());
                return std::unexpected(false);
            }
            page.ClearDirty();
        }

        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }

        page.InitBlank(new_page_id.value(),storage::PageType::HEAP);
        page.Pin();
        replacer_->SetEvictable(fid,false);
        replacer_->RecordAccess(fid);
        page_table_[new_page_id.value()] = fid;
        *page_id = new_page_id.value();
        return &pages_[fid];
    }

    bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty)
    {
        // TODO:
        // 1. 如果 page_id 不在 page_table_ 中，直接返回 false。
        // 2. 获取该 Page 对象，将其 pin_count 减 1。
        // 3. 如果 pin_count 减到 0，调用 replacer_->SetEvictable(fid, true)。
        // 4. 如果参数 is_dirty 为 true，更新该 Page 的脏标记。
        std::lock_guard<std::mutex> guard(latch_);
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;
        }
        frame_id_t fid = it->second;
        storage::Page &page = pages_[fid];

        if (page.PinCount() <= 0) {
            return false;
        }

        if (is_dirty) {
            page.MarkDirty();
        }
        page.UnPin();


        if (page.PinCount() == 0) {
            replacer_->SetEvictable(fid,true);
        }


        return true;
    }

    std::expected<bool,bool> BufferPoolManager::FlushPage(page_id_t page_id)
    {
        // TODO:
        // 1. 检查 page_id 是否有效且在缓冲池中。
        // 2. 调用 disk_manager_->WritePage() 将数据写入物理磁盘。
        // 3. 重置 Page 的 is_dirty 标记。
        std::lock_guard<std::mutex> guard(latch_);
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) {
            return false;
        }
        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];
        auto write = disk_manager_->WritePage(page.PageId(),page.Data());
        if (!write.has_value()) {
            return std::unexpected(false);
        }
        page.ClearDirty();
        return true;
    }

    std::expected<bool,bool> BufferPoolManager::FlushAllPages()
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
                return std::unexpected(false);
            }
            page.ClearDirty();
        }
        return true;
    }
    bool BufferPoolManager::DeletePage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == INVALID_PAGE_ID) {
            return false;
        }

        auto it = page_table_.find(page_id);
        
        // 逻辑：不在内存就去磁盘回收；在内存就先检查 PinCount，再回收磁盘，最后清理内存。
        if (it == page_table_.end()) {
            return disk_manager_->DeallocatePage(page_id).has_value();
        }

        frame_id_t fid = it->second;
        storage::Page& page = pages_[fid];

        if (page.PinCount() > 0) {
            return false; 
        }

        // 磁盘先行：如果磁盘都没法回收这个 ID，内存映射就该保留
        if (!disk_manager_->DeallocatePage(page_id).has_value()) {
            return false;
        }

        // 内存清理
        page_table_.erase(it);
        replacer_->Remove(fid);
        page.ResetMetaData(INVALID_PAGE_ID);
        frame_list_.push_back(fid);

        return true;
    }


    std::expected<frame_id_t, bool> BufferPoolManager::GetVictimFrame()
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

        return std::unexpected(false);
    }
} // namespace buffer
} // namespace HaruhiDB
