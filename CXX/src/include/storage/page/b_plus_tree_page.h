/**
 * CXX/src/include/storage/page/b_plus_tree_page.h
 *
 * ========================= 设计目标 =========================
 *
 * BPlusTreePage 是 B+Tree 页的公共页头包装层。
 *
 * 它建立在通用 Page 之上，
 * 负责把 Page 解释为“B+Tree 页面”，
 * 并管理所有 B+Tree 页共享的基础元数据。
 *
 * 核心职责：
 *
 * 1. 标识当前页类型（INTERNAL / LEAF）
 * 2. 维护 parent_page_id
 * 3. 维护当前键数量 size
 * 4. 维护容量上限 max_size
 * 5. 维护节点专有链接页号（LEAF next / INTERNAL leftmost child）
 * 6. 提供根页 / 叶页 / 内部页判断
 *
 *
 * ========================= 为什么需要 BPlusTreePage =========================
 *
 * B+Tree 的内部页和叶子页虽然存储内容不同，
 * 但都共享一部分基础页元信息：
 *
 * - 它们都属于 B+Tree
 * - 都需要 parent_page_id
 * - 都需要 size / max_size
 * - 都需要统一的页初始化入口
 *
 * 因此需要一个公共基类包装，
 * 让内部页与叶子页共享这一层逻辑。
 *
 *
 * ========================= BPlusTreePage 在系统中的位置 =========================
 *
 * Page
 *   └── BPlusTreePage
 *         ├── BPlusTreeInternalPage
 *         └── BPlusTreeLeafPage
 *
 *
 * ========================= 页面组织 =========================
 *
 *   +------------------------------+
 *   | PersistentHeader             |
 *   +------------------------------+
 *   | BPlusTreeOpaqueHeader        |
 *   +------------------------------+
 *   | internal/leaf body ...       |
 *   +------------------------------+
 *
 * 其中：
 *
 * - PersistentHeader::page_type 标识 INTERNAL / LEAF
 * - PersistentHeader::opaque 被解释为 BPlusTreeOpaqueHeader
 */

#pragma once

#include "common/config.h"
#include "storage/page/page.h"

#include <cstddef>
#include <limits>

namespace HaruhiDB
{
namespace storage
{

    /**
     * B+Tree 页在 opaque 区域中的公共头部。
     */
    struct BPlusTreeOpaqueHeader
    {
        page_id_t parent_page_id{INVALID_PAGE_ID};
        uint16_t size{0};
        uint16_t max_size{0};
        // LEAF: next_page_id, INTERNAL: leftmost_child_page_id
        page_id_t node_link_page_id{INVALID_PAGE_ID};
        uint32_t reserved{0};
    };

    static_assert(std::is_trivially_copyable_v<BPlusTreeOpaqueHeader>);
    static_assert(sizeof(BPlusTreeOpaqueHeader) == PAGE_HEADER_OPAQUE_SIZE);
    static_assert((offsetof(PersistentHeader, opaque) % alignof(BPlusTreeOpaqueHeader)) == 0);

    class BPlusTreePage
    {
    public:
        /**
         * 并发约定：
         *
         * 调用方在读写页面内容前，
         * 应自行持有合适的 page latch。
         * 该包装层除 InitForNewPage 外不主动加锁。
         *
         * @param page 底层通用页面对象
         */
        explicit BPlusTreePage(Page* page) : page_(page) {}

        /**
         * 返回底层 Page。
         */
        Page* GetPage() noexcept { return page_; }

        /**
         * 返回底层 Page。
         */
        const Page* GetPage() const noexcept { return page_; }

        /**
         * 将当前页面初始化为新的 B+Tree 页。
         *
         * @param page_id        页号
         * @param page_type      页面类型，必须为 LEAF 或 INTERNAL
         * @param max_size       最大容量
         * @param parent_page_id 父页页号
         * @return 成功返回 true
         */
        bool InitForNewPage(
            page_id_t page_id,
            PageType page_type,
            uint16_t max_size,
            page_id_t parent_page_id = INVALID_PAGE_ID) noexcept;

        /**
         * 返回当前页号。
         */
        page_id_t GetPageId() const noexcept
        {
            return page_ == nullptr ? INVALID_PAGE_ID : page_->Header()->page_id;
        }

        /**
         * 返回当前页面类型。
         */
        PageType GetPageType() const noexcept
        {
            return page_ == nullptr ? PageType::INVALID : page_->Header()->page_type;
        }

        /**
         * 判断当前是否为叶子页。
         */
        bool IsLeafPage() const noexcept
        {
            return GetPageType() == PageType::LEAF;
        }

        /**
         * 判断当前是否为内部页。
         */
        bool IsInternalPage() const noexcept
        {
            return GetPageType() == PageType::INTERNAL;
        }

        /**
         * 判断当前是否为根页。
         */
        bool IsRootPage() const noexcept
        {
            return GetParentPageId() == INVALID_PAGE_ID;
        }

        /**
         * 返回父页页号。
         */
        page_id_t GetParentPageId() const noexcept;

        /**
         * 设置父页页号。
         *
         * @param parent_page_id 父页页号
         */
        void SetParentPageId(page_id_t parent_page_id) noexcept;

        /**
         * 返回当前元素数量。
         */
        uint16_t GetSize() const noexcept;

        /**
         * 设置当前元素数量。
         *
         * @param size 新数量
         */
        void SetSize(uint16_t size) noexcept;

        /**
         * 对当前元素数量做增减。
         *
         * @param delta 增量，可为负数
         */
        void IncreaseSize(int delta) noexcept;

        /**
         * 返回最大容量。
         */
        uint16_t GetMaxSize() const noexcept;

        /**
         * 设置最大容量。
         *
         * @param max_size 最大容量
         */
        void SetMaxSize(uint16_t max_size) noexcept;

        /**
         * 返回当前页的最小合法大小。
         */
        uint16_t GetMinSize() const noexcept
        {
            if (IsRootPage()) {
                return IsLeafPage() ? 1 : 2;
            }
            return GetMaxSize() / 2;
        }

    public:
        /**
         * 返回节点专有链接页号。
         *
         * LEAF: next_page_id
         * INTERNAL: leftmost_child_page_id
         */
        page_id_t GetNodeLinkPageId() const noexcept
        {
            const auto* header = OpaqueHeader();
            return header == nullptr ? INVALID_PAGE_ID : header->node_link_page_id;
        }

        /**
         * 设置节点专有链接页号。
         *
         * LEAF: next_page_id
         * INTERNAL: leftmost_child_page_id
         */
        void SetNodeLinkPageId(page_id_t link_page_id) noexcept
        {
            auto* header = OpaqueHeader();
            if (header == nullptr) {
                return;
            }
            header->node_link_page_id = link_page_id;
            page_->MarkDirty();
        }

    protected:
        /**
         * 返回 B+Tree 公共 opaque 头部。
         */
        BPlusTreeOpaqueHeader* OpaqueHeader() noexcept
        {
            if (page_ == nullptr) {
                return nullptr;
            }
            return reinterpret_cast<BPlusTreeOpaqueHeader*>(page_->Header()->opaque);
        }

        /**
         * 返回 B+Tree 公共 opaque 头部。
         */
        const BPlusTreeOpaqueHeader* OpaqueHeader() const noexcept
        {
            if (page_ == nullptr) {
                return nullptr;
            }
            return reinterpret_cast<const BPlusTreeOpaqueHeader*>(page_->Header()->opaque);
        }

        Page* page_{nullptr};
    };

} // namespace storage
} // namespace HaruhiDB
