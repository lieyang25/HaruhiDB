/**
 * CXX/src/include/table/table_heap.h
 *
 * ========================= 设计目标 =========================
 *
 * TableHeap 表示一张表在存储层上的堆式组织。
 *
 * 它不直接负责单页内 tuple 的布局细节，
 * 而是把多个 TablePage 串成页链，
 * 对外提供表级别的插入、读取、删除、更新与遍历能力。
 *
 * 核心职责：
 *
 * 1. 管理表页链
 * 2. 把单页 tuple 操作提升为表级语义
 * 3. 分配新页并接入页链
 * 4. 维护 free_space_map 以加速插入定位
 * 5. 与 BufferPoolManager / WAL 协作完成持久化路径
 *
 *
 * ========================= 为什么需要 TableHeap =========================
 *
 * TablePage 只能处理“单个页内部”的 tuple。
 *
 * 但一张表通常会跨多个页，
 * 因此需要 TableHeap 来解决：
 *
 * - 插入时应该落到哪一页
 * - 页满后如何扩展新页
 * - 如何沿页链扫描整张表
 * - 更新失败时如何迁移记录
 * - 删除后是否可以尝试回收整页
 *
 *
 * ========================= TableHeap 在系统中的位置 =========================
 *
 * Catalog / TableInfo
 *   └── TableHeap
 *         ├── TablePage
 *         ├── BufferPoolManager
 *         └── WalManager
 *
 *
 * ========================= 组织形式 =========================
 *
 *   first_page_id_
 *        │
 *        ▼
 *   +-----------+    +-----------+    +-----------+
 *   | TablePage | -> | TablePage | -> | TablePage |
 *   +-----------+    +-----------+    +-----------+
 *
 * 同时维护：
 *
 *   free_space_map_
 *     page_id -> free_bytes
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/page/table_page.h"
#include "storage/record/rid.h"
#include "storage/record/tuple.h"
#include "storage/wal/wal_manager.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace HaruhiDB
{
namespace table
{

class TableIterator;

class TableHeap {
public:
    /**
     * 创建一个合法的空堆表，并初始化首页。
     *
     * @param bpm 缓冲池管理器
     * @return 成功返回 TableHeap，失败返回错误信息
     */
    static std::expected<std::unique_ptr<TableHeap>, std::string>
    Create(buffer::BufferPoolManager* bpm);

    /**
     * @param bpm            缓冲池管理器
     * @param first_page_id  首页页号
     */
    explicit TableHeap(buffer::BufferPoolManager* bpm, page_id_t first_page_id = INVALID_PAGE_ID);

    /**
     * 插入一条 tuple。
     *
     * @param tuple   待插入记录
     * @param out_rid 成功时返回新 RID
     * @return 成功返回 true
     */
    bool InsertTuple(const record::Tuple& tuple, record::RID* out_rid = nullptr);

    /**
     * 根据 RID 读取 tuple。
     *
     * @param rid       目标记录位置
     * @param out_tuple 输出 tuple
     * @return 成功返回 true
     */
    bool GetTuple(const record::RID& rid, record::Tuple* out_tuple);

    /**
     * 逻辑删除一条记录。
     *
     * @param rid 目标记录位置
     * @return 成功返回 true
     */
    bool DeleteTuple(const record::RID& rid);

    /**
     * 更新一条记录。
     *
     * 若原页空间不足，则会迁移到新位置。
     * 迁移成功后旧 RID 失效，调用方必须使用 out_rid。
     *
     * @param rid       原记录位置
     * @param new_tuple 新记录内容
     * @param out_rid   成功时返回最终 RID
     * @return 成功返回 true
     */
    bool UpdateTuple(const record::RID& rid, const record::Tuple& new_tuple, record::RID* out_rid = nullptr);

    /**
     * 返回表扫描起始迭代器。
     */
    TableIterator Begin();

    /**
     * 返回表扫描结束迭代器。
     */
    TableIterator End();

    /**
     * 设置 WAL 管理器。
     */
    void SetWalManager(storage::wal::WalManager* wal_manager) noexcept { wal_manager_ = wal_manager; }

    /**
     * 返回 WAL 管理器。
     */
    storage::wal::WalManager* GetWalManager() const noexcept { return wal_manager_; }

    /**
     * 返回首页页号。
     */
    page_id_t FirstPageId() const noexcept
    {
        std::shared_lock lock(table_latch_);
        return first_page_id_;
    }

    /**
     * 设置首页页号。
     *
     * @param pid 新首页页号
     */
    void SetFirstPageId(page_id_t pid) noexcept
    {
        std::unique_lock lock(table_latch_);
        first_page_id_ = pid;
    }

private:
    friend class TableIterator;

    /**
     * 查找一个有足够空闲空间的页。
     *
     * @param need 需要的字节数
     * @return 找到则返回 page_id，否则返回 nullopt
     */
    std::optional<page_id_t> FindPageWithFreeSpace(uint32_t need);

    /**
     * 分配并初始化一个新页，并接入页链尾部。
     *
     * @return 成功返回新 page_id，否则返回 nullopt
     */
    std::optional<page_id_t> AllocateNewPage();

    /**
     * 更新 free_space_map。
     *
     * @param page_id    页号
     * @param free_space 当前空闲字节数
     */
    void UpdateFreeSpaceMap(page_id_t page_id, uint32_t free_space);

    /**
     * 重新扫描页链并刷新 tail_page_id_。
     *
     * @note 调用方需已持有 table_latch_
     */
    page_id_t RefreshTailPageIdLocked();

    /**
     * 若页已空且安全，则尝试回收该页。
     *
     * @param page_id 目标页号
     * @return 成功回收返回 true
     */
    bool ReclaimPageIfEmpty(page_id_t page_id);

    /**
     * WAL 失败时直接致命退出。
     *
     * @param where 失败位置说明
     */
    [[noreturn]] static void HandleWalFailureOrDie(const char* where);

    /**
     * 追加页面 after-image 日志并强制刷盘。
     *
     * @param page 目标页面
     * @param type 日志类型
     */
    void AppendPageAfterImageLogOrDie(storage::Page* page, storage::wal::LogRecordType type);

    buffer::BufferPoolManager* bpm_;
    page_id_t first_page_id_;
    page_id_t tail_page_id_;
    storage::wal::WalManager* wal_manager_{nullptr};

    /// page_id -> free_bytes
    std::unordered_map<page_id_t, uint32_t> free_space_map_;

    /// 保护页链与首页/尾页状态
    mutable std::shared_mutex table_latch_;

    /// 保护 free_space_map_
    mutable std::mutex free_space_mutex_;
};

} // namespace table
} // namespace HaruhiDB
