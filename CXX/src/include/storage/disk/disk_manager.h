/**
 * CXX/src/include/storage/disk/disk_manager.h
 *
 * ========================= 设计目标 =========================
 *
 * DiskManager 负责管理数据库文件的页级持久化读写。
 *
 * 它向上层提供统一的 page 访问接口，使上层模块不直接依赖文件流。
 *
 * 核心职责：
 *
 * 1. 读取指定 page_id 的页面
 * 2. 将页面写回磁盘
 * 3. 分配新的 page_id
 * 4. 回收空闲页面并维护 free list
 * 5. 维护数据库头页 DBHeader
 *
 *
 * ========================= 为什么需要 DiskManager =========================
 *
 * BufferPoolManager、TableHeap、B+Tree 等模块都以“页”为基本单位工作，
 * 但磁盘本身只提供文件读写能力。
 *
 * DiskManager 负责在“页模型”和“文件模型”之间做转换：
 *
 * - 上层只关心 page_id
 * - DiskManager 负责定位文件偏移并完成实际 I/O
 *
 *
 * ========================= DiskManager 在系统中的位置 =========================
 *
 * BufferPoolManager
 *   └── DiskManager
 *         └── database file
 *
 * 磁盘布局：
 *
 *   +-----------+-----------+-----------+-----------+
 *   | Header    | Page 1    | Page 2    | Page 3    |
 *   +-----------+-----------+-----------+-----------+
 *
 * 其中：
 *
 * - Page 0 固定存放 DBHeader
 * - Page 1 及之后存放普通数据页、索引页或其他页面
 */

#pragma once

#include "common/config.h"
#include "common/error_code.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>

namespace HaruhiDB
{
namespace storage
{

    /**
     * 数据库头页中的持久化元数据。
     *
     * 该结构写入 Page 0，用于恢复数据库文件的基本状态。
     */
    struct DBHeader
    {
        uint32_t magic_number;
        uint32_t version;
        page_id_t next_page_id;
        page_id_t free_list_head;
        page_id_t catalog_meta_page_id;
    };

    static_assert(std::is_trivially_copyable_v<DBHeader>);
    static_assert(sizeof(DBHeader) == 20, "DBHeader size must be 20 bytes");

    /**
     * 磁盘 I/O 错误信息。
     */
    struct IOErr
    {
        std::string msg;
        HaruhiDB::ErrorCode err_code;
    };

    /**
     * 数据库文件的页级管理器。
     *
     * 负责：
     *
     * - 打开和初始化数据库文件
     * - 按页读取与写入
     * - 分配与回收页面
     * - 维护头页元数据
     */
    class DiskManager
    {
    public:
        /**
         * 打开或创建数据库文件，并完成头页初始化或加载。
         *
         * @param path 数据库文件路径
         */
        explicit DiskManager(const std::filesystem::path& path);

        /**
         * 关闭数据库文件。
         */
        ~DiskManager();

        /**
         * 读取指定页面。
         *
         * @param page_id 要读取的页号
         * @param data    输出缓冲区
         * @note page_id 必须在当前文件范围内
         */
        auto ReadPage(page_id_t page_id, page_data_t& data) -> std::expected<void, IOErr>;

        /**
         * 写入指定页面。
         *
         * @param page_id 要写入的页号
         * @param data    输入缓冲区
         * @note Page 0 由头页专用，不能通过该接口写入
         */
        auto WritePage(page_id_t page_id, const page_data_t& data) -> std::expected<void, IOErr>;

        /**
         * 分配一个可用页面。
         *
         * @note 优先复用 free list 中的空闲页，否则在文件末尾追加新页
         */
        auto AllocatePage() -> std::expected<page_id_t, IOErr>;

        /**
         * 回收一个页面并挂回 free list。
         *
         * @param page_id 要回收的页号
         */
        auto DeallocatePage(page_id_t page_id) -> std::expected<void, IOErr>;

        /**
         * 刷新文件缓冲区。
         */
        auto Flush() -> std::expected<void, IOErr>;

        /**
         * 返回 catalog 元数据入口页号。
         */
        page_id_t CatalogMetaPageId() const noexcept { return catalog_meta_page_id_; }

        /**
         * 设置 catalog 元数据入口页号并持久化到头页。
         *
         * @param catalog_meta_page_id 入口页号，允许 INVALID_PAGE_ID 表示未初始化
         */
        auto SetCatalogMetaPageId(page_id_t catalog_meta_page_id) -> std::expected<void, IOErr>;

    private:
        /**
         * 打开数据库文件；若不存在则创建。
         */
        auto OpenFile() -> std::expected<void, IOErr>;

        /**
         * 当文件为空时初始化头页，否则加载已有头页。
         */
        auto InitHeaderIfNeeded() -> std::expected<void, IOErr>;

        /**
         * 从 Page 0 读取 DBHeader。
         */
        auto LoadHeader() -> std::expected<void, IOErr>;

        /**
         * 将当前 DBHeader 写回 Page 0。
         */
        auto PersistHeader() -> std::expected<void, IOErr>;

    private:
        /// 数据库文件路径
        std::filesystem::path path_;

        /// 磁盘文件流
        std::fstream file_;

        /// 下一个可分配页号
        page_id_t next_page_id_{1};

        /// 空闲页链表头
        page_id_t free_list_head_{INVALID_PAGE_ID};

        /// catalog 元数据页链入口
        page_id_t catalog_meta_page_id_{INVALID_PAGE_ID};
    };

} // namespace storage
} // namespace HaruhiDB
