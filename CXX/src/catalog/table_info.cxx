/**
 * CXX/src/catalog/table_info.cxx
 */

#include "catalog/table_info.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{

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

    void TableInfo::AddIndexOid(index_oid_t index_oid)
    {
        if (!std::ranges::contains(index_oids_, index_oid)) {
            index_oids_.push_back(index_oid);
        }
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
