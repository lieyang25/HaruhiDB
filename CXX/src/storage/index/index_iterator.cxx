/**
 * CXX/src/storage/index/index_iterator.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 IndexIterator 的顺序扫描逻辑。
 *
 * 主要完成：
 *
 * 1. end / 起始迭代器构造
 * 2. 当前键值对读取
 * 3. 叶子页内前进
 * 4. 沿叶子链跨页前进
 * 5. 迭代器比较
 *
 *
 * ========================= 遍历规则 =========================
 *
 * - 仅遍历叶子页中的有效下标范围
 * - 到达当前叶子页末尾后自动跳转 next_page_id
 * - 若页类型非法或取页失败，则进入 end 状态
 * - 解引用返回 {key, RID} 拷贝
 */

#include "storage/index/index_iterator.h"

#include "storage/page/b_plus_tree_leaf_page.h"

namespace HaruhiDB
{
namespace storage
{

    IndexIterator::IndexIterator()
    {
    }

    /**
     * @param bpm          缓冲池管理器
     * @param leaf_page_id 起始叶子页号
     * @param index        起始下标
     */
    IndexIterator::IndexIterator(
        buffer::BufferPoolManager* bpm,
        page_id_t leaf_page_id,
        uint16_t index)
        : bpm_(bpm),
          leaf_page_id_(leaf_page_id),
          index_(index),
          at_end_(false)
    {
        // step 1: 检查初始状态是否合法。
        if (bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            at_end_ = true;
            return;
        }

        // step 2: 从给定起点定位第一条有效键值对。
        if (!AdvanceToValid()) {
            at_end_ = true;
        }
    }

    IndexIterator::MappingType IndexIterator::operator*() const
    {
        // step 1: end 迭代器或无效状态直接返回空映射。
        if (at_end_ || bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            return {0, record::RID{}};
        }

        // step 2: 取页并检查当前叶子页与下标是否合法。
        auto page_exp = bpm_->FetchPage(leaf_page_id_);
        if (!page_exp.has_value()) {
            return {0, record::RID{}};
        }

        Page* page = page_exp.value();
        page->RLock();
        BPlusTreeLeafPage leaf(page);
        if (leaf.GetPageType() != PageType::LEAF || index_ >= leaf.GetSize()) {
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            return {0, record::RID{}};
        }

        // step 3: 读取当前键值对并返回拷贝。
        const auto item = leaf.ItemAt(index_);
        page->RUnLock();
        bpm_->UnpinPage(leaf_page_id_, false);
        return {item.key, item.value};
    }

    IndexIterator& IndexIterator::operator++()
    {
        // step 1: end 迭代器递增保持不变。
        if (at_end_ || bpm_ == nullptr || leaf_page_id_ == INVALID_PAGE_ID) {
            return *this;
        }

        // step 2: 取当前叶子页，优先尝试在页内前进。
        auto page_exp = bpm_->FetchPage(leaf_page_id_);
        if (!page_exp.has_value()) {
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
            return *this;
        }

        Page* page = page_exp.value();
        page->RLock();
        BPlusTreeLeafPage leaf(page);
        if (leaf.GetPageType() != PageType::LEAF) {
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
            return *this;
        }

        if (index_ + 1 < leaf.GetSize()) {
            ++index_;
            page->RUnLock();
            bpm_->UnpinPage(leaf_page_id_, false);
            return *this;
        }

        // step 3: 当前页已到末尾，则跳到下一叶子页。
        leaf_page_id_ = leaf.GetNextPageId();
        index_ = 0;
        page->RUnLock();
        bpm_->UnpinPage(page->PageId(), false);

        // step 4: 在新的叶子页位置继续定位有效项。
        if (leaf_page_id_ == INVALID_PAGE_ID || !AdvanceToValid()) {
            at_end_ = true;
            leaf_page_id_ = INVALID_PAGE_ID;
        }
        return *this;
    }

    bool IndexIterator::operator==(const IndexIterator& other) const noexcept
    {
        if (at_end_ && other.at_end_) {
            return true;
        }

        return bpm_ == other.bpm_ &&
            leaf_page_id_ == other.leaf_page_id_ &&
            index_ == other.index_ &&
            at_end_ == other.at_end_;
    }

    bool IndexIterator::operator!=(const IndexIterator& other) const noexcept
    {
        return !(*this == other);
    }

    bool IndexIterator::AdvanceToValid()
    {
        if (bpm_ == nullptr) {
            return false;
        }

        // step 1: 沿叶子链顺序扫描，直到找到有效下标。
        while (leaf_page_id_ != INVALID_PAGE_ID) {
            auto page_exp = bpm_->FetchPage(leaf_page_id_);
            if (!page_exp.has_value()) {
                return false;
            }

            Page* page = page_exp.value();
            page->RLock();
            BPlusTreeLeafPage leaf(page);
            if (leaf.GetPageType() != PageType::LEAF) {
                page->RUnLock();
                bpm_->UnpinPage(leaf_page_id_, false);
                return false;
            }

            // step 2: 若当前页下标有效，则停止。
            if (index_ < leaf.GetSize()) {
                page->RUnLock();
                bpm_->UnpinPage(leaf_page_id_, false);
                return true;
            }

            // step 3: 当前页已无可访问项，则跳到下一叶子页。
            leaf_page_id_ = leaf.GetNextPageId();
            index_ = 0;
            page->RUnLock();
            bpm_->UnpinPage(page->PageId(), false);
        }

        return false;
    }

} // namespace storage
} // namespace HaruhiDB