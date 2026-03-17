/**
 * CXX/src/storage/page/b_plus_tree_page.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 BPlusTreePage 的公共页头逻辑。
 *
 * 主要完成：
 *
 * 1. 新页初始化
 * 2. parent_page_id 读写
 * 3. size 读写
 * 4. max_size 读写
 * 5. size 增减
 *
 *
 * ========================= 实现说明 =========================
 *
 * BPlusTreePage 只处理所有 B+Tree 页共享的公共元数据。
 *
 * 具体键数组、子指针数组、叶子记录数组等布局，
 * 由更具体的内部页和叶子页实现继续解释。
 */

#include "storage/page/b_plus_tree_page.h"

#include <algorithm>

namespace HaruhiDB
{
namespace storage
{

    /**
     * @param page_id        页号
     * @param page_type      页面类型
     * @param max_size       最大容量
     * @param parent_page_id 父页页号
     */
    bool BPlusTreePage::InitForNewPage(
        page_id_t page_id,
        PageType page_type,
        uint16_t max_size,
        page_id_t parent_page_id) noexcept
    {
        // step 1: 检查底层 page 与页面类型是否合法。
        if (page_ == nullptr) {
            return false;
        }
        if (page_type != PageType::LEAF && page_type != PageType::INTERNAL) {
            return false;
        }

        // step 2: 初始化通用页头。
        page_->WLock();

        auto* base_header = page_->Header();
        base_header->lsn = 0;
        base_header->page_id = page_id;
        base_header->page_type = page_type;
        std::fill(std::begin(base_header->reserved0), std::end(base_header->reserved0), 0);

        // step 3: 初始化 B+Tree 公共 opaque 头部。
        auto* opaque = OpaqueHeader();
        opaque->parent_page_id = parent_page_id;
        opaque->size = 0;
        opaque->max_size = max_size;
        std::fill(std::begin(opaque->reserved), std::end(opaque->reserved), 0);

        // step 4: 标记脏页并释放写锁。
        page_->MarkDirty();
        page_->WUnLock();
        return true;
    }

    page_id_t BPlusTreePage::GetParentPageId() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? INVALID_PAGE_ID : header->parent_page_id;
    }

    void BPlusTreePage::SetParentPageId(page_id_t parent_page_id) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->parent_page_id = parent_page_id;
        page_->MarkDirty();
    }

    uint16_t BPlusTreePage::GetSize() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? 0 : header->size;
    }

    void BPlusTreePage::SetSize(uint16_t size) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->size = size;
        page_->MarkDirty();
    }

    void BPlusTreePage::IncreaseSize(int delta) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }

        const int next = static_cast<int>(header->size) + delta;
        if (next < 0) {
            header->size = 0;
        } else if (next > static_cast<int>(std::numeric_limits<uint16_t>::max())) {
            header->size = std::numeric_limits<uint16_t>::max();
        } else {
            header->size = static_cast<uint16_t>(next);
        }

        page_->MarkDirty();
    }

    uint16_t BPlusTreePage::GetMaxSize() const noexcept
    {
        const auto* header = OpaqueHeader();
        return header == nullptr ? 0 : header->max_size;
    }

    void BPlusTreePage::SetMaxSize(uint16_t max_size) noexcept
    {
        auto* header = OpaqueHeader();
        if (header == nullptr) {
            return;
        }
        header->max_size = max_size;
        page_->MarkDirty();
    }

} // namespace storage
} // namespace HaruhiDB