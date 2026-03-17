/**
 * CXX/src/catalog/table_info.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 TableInfo 的运行时组织逻辑。
 *
 * 主要完成：
 *
 * 1. 表信息对象构造
 * 2. 索引 oid 登记
 * 3. 索引创建
 * 4. 索引加载
 * 5. 索引查找
 * 6. 调试字符串输出
 *
 *
 * ========================= 核心流程 =========================
 *
 * CreateIndex:
 *   检查参数
 *   检查 oid / name 冲突
 *   创建 BPlusTree
 *   记录 header_page_id
 *   挂接到 indexes_
 *
 * LoadIndex:
 *   检查参数
 *   检查 oid / name 冲突
 *   用已有 header_page_id 构造 BPlusTree
 *   挂接到 indexes_
 */

#include "catalog/table_info.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
        /**
         * 将索引名标准化为小写。
         *
         * @param index_name 原始索引名
         * @return 标准化后的索引名
         */
        std::string NormalizeIndexName(std::string_view index_name)
        {
            std::string normalized(index_name);
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return normalized;
        }
    } // namespace

    /**
     * @param oid        表 oid
     * @param name       表名
     * @param schema     表结构
     * @param table_heap 表数据入口
     */
    TableInfo::TableInfo(
        table_oid_t oid,
        std::string name,
        Schema schema,
        std::unique_ptr<table::TableHeap> table_heap)
        : oid_(oid),
          name_(std::move(name)),
          schema_(std::move(schema)),
          table_heap_(std::move(table_heap))
    {
        if (name_.empty()) {
            throw std::invalid_argument("TableInfo: table name must not be empty");
        }

        if (table_heap_ == nullptr) {
            throw std::invalid_argument("TableInfo: table heap must not be null");
        }
    }

    /**
     * @param index_oid 索引 oid
     */
    void TableInfo::AddIndexOid(index_oid_t index_oid)
    {
        if (!std::ranges::contains(index_oids_, index_oid)) {
            index_oids_.push_back(index_oid);
        }
    }

    /**
     * @param index_oid  索引 oid
     * @param index_name 索引名
     * @param bpm        缓冲池管理器
     */
    std::expected<storage::BPlusTree*, std::string> TableInfo::CreateIndex(
        index_oid_t index_oid,
        std::string index_name,
        buffer::BufferPoolManager* bpm)
    {
        // step 1: 检查基础参数。
        if (bpm == nullptr) {
            return std::unexpected("TableInfo::CreateIndex: buffer pool manager is null");
        }
        if (index_name.empty()) {
            return std::unexpected("TableInfo::CreateIndex: index name must not be empty");
        }

        // step 2: 检查索引 oid 与名称是否冲突。
        const std::string normalized = NormalizeIndexName(index_name);
        for (const auto& entry : indexes_) {
            if (entry.index_oid == index_oid) {
                return std::unexpected("TableInfo::CreateIndex: index oid already exists");
            }
            if (NormalizeIndexName(entry.index_name) == normalized) {
                return std::unexpected("TableInfo::CreateIndex: index name already exists");
            }
        }

        // step 3: 创建索引对象并检查 header page。
        auto index = std::make_unique<storage::BPlusTree>(bpm);
        const page_id_t header_page_id = index->HeaderPageId();
        if (header_page_id == INVALID_PAGE_ID) {
            return std::unexpected("TableInfo::CreateIndex: failed to allocate index header page");
        }

        // step 4: 把索引挂接到当前表信息。
        AddIndexOid(index_oid);
        indexes_.push_back(IndexEntry{
            .index_oid = index_oid,
            .index_name = std::move(index_name),
            .header_page_id = header_page_id,
            .index = std::move(index),
        });

        return indexes_.back().index.get();
    }

    /**
     * @param index_oid      索引 oid
     * @param index_name     索引名
     * @param header_page_id 索引 header page
     * @param bpm            缓冲池管理器
     */
    std::expected<storage::BPlusTree*, std::string> TableInfo::LoadIndex(
        index_oid_t index_oid,
        std::string index_name,
        page_id_t header_page_id,
        buffer::BufferPoolManager* bpm)
    {
        // step 1: 检查基础参数。
        if (bpm == nullptr) {
            return std::unexpected("TableInfo::LoadIndex: buffer pool manager is null");
        }
        if (index_name.empty()) {
            return std::unexpected("TableInfo::LoadIndex: index name must not be empty");
        }
        if (header_page_id == INVALID_PAGE_ID) {
            return std::unexpected("TableInfo::LoadIndex: header page id is invalid");
        }

        // step 2: 检查索引 oid 与名称是否冲突。
        const std::string normalized = NormalizeIndexName(index_name);
        for (const auto& entry : indexes_) {
            if (entry.index_oid == index_oid) {
                return std::unexpected("TableInfo::LoadIndex: index oid already exists");
            }
            if (NormalizeIndexName(entry.index_name) == normalized) {
                return std::unexpected("TableInfo::LoadIndex: index name already exists");
            }
        }

        // step 3: 按给定 header page 加载索引对象。
        auto index = std::make_unique<storage::BPlusTree>(bpm, header_page_id);
        if (index->HeaderPageId() != header_page_id) {
            return std::unexpected("TableInfo::LoadIndex: header page id mismatch");
        }

        // step 4: 把索引挂接到当前表信息。
        AddIndexOid(index_oid);
        indexes_.push_back(IndexEntry{
            .index_oid = index_oid,
            .index_name = std::move(index_name),
            .header_page_id = header_page_id,
            .index = std::move(index),
        });

        return indexes_.back().index.get();
    }

    storage::BPlusTree* TableInfo::GetIndex(index_oid_t index_oid) noexcept
    {
        for (auto& entry : indexes_) {
            if (entry.index_oid == index_oid) {
                return entry.index.get();
            }
        }
        return nullptr;
    }

    const storage::BPlusTree* TableInfo::GetIndex(index_oid_t index_oid) const noexcept
    {
        for (const auto& entry : indexes_) {
            if (entry.index_oid == index_oid) {
                return entry.index.get();
            }
        }
        return nullptr;
    }

    std::optional<page_id_t> TableInfo::GetIndexHeaderPageId(index_oid_t index_oid) const noexcept
    {
        for (const auto& entry : indexes_) {
            if (entry.index_oid == index_oid) {
                return entry.header_page_id;
            }
        }
        return std::nullopt;
    }

    std::string TableInfo::ToString() const
    {
        const auto column_count = schema_.ColumnCount();
        const page_id_t first_page_id = table_heap_ != nullptr ? table_heap_->FirstPageId() : INVALID_PAGE_ID;

        return "TableInfo{oid=" + std::to_string(oid_) +
            ", name=" + name_ +
            ", columns=" + std::to_string(column_count) +
            ", first_page_id=" + std::to_string(first_page_id) +
            ", index_count=" + std::to_string(index_oids_.size()) + "}";
    }

} // namespace catalog
} // namespace HaruhiDB