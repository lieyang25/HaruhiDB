/**
 * CXX/src/catalog/catalog.cxx
 */

#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace HaruhiDB
{
namespace catalog
{
    namespace
    {
        std::string NormalizeTableName(std::string_view table_name)
        {
            std::string normalized(table_name);
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return normalized;
        }
    } // namespace

    Catalog::Catalog(buffer::BufferPoolManager* bpm)
        : bpm_(bpm)
    {
    }

    std::expected<TableInfo*, std::string> Catalog::CreateTable(
        std::string table_name, const Schema& schema)
    {
        auto validated = ValidateTableName(table_name);
        if (!validated.has_value()) {
            return std::unexpected(validated.error());
        }

        std::unique_lock lock(latch_);

        const std::string normalized_name = NormalizeTableName(table_name);
        if (name_to_oid_.contains(normalized_name)) {
            return std::unexpected("Catalog: table already exists: " + table_name);
        }

        auto table_heap_exp = CreateTableHeap();
        if (!table_heap_exp.has_value()) {
            return std::unexpected(table_heap_exp.error());
        }

        const table_oid_t table_oid = AllocateTableOid();

        auto table_info = std::make_unique<TableInfo>(
            table_oid,
            std::move(table_name),
            schema,
            std::move(table_heap_exp.value()));

        TableInfo* table_info_ptr = table_info.get();
        name_to_oid_[normalized_name] = table_oid;
        tables_[table_oid] = std::move(table_info);
        return table_info_ptr;
    }

    TableInfo* Catalog::GetTable(std::string_view table_name) noexcept
    {
        std::shared_lock lock(latch_);

        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return nullptr;
        }

        const auto table_it = tables_.find(name_it->second);
        return table_it == tables_.end() ? nullptr : table_it->second.get();
    }

    const TableInfo* Catalog::GetTable(std::string_view table_name) const noexcept
    {
        std::shared_lock lock(latch_);

        const auto name_it = name_to_oid_.find(NormalizeTableName(table_name));
        if (name_it == name_to_oid_.end()) {
            return nullptr;
        }

        const auto table_it = tables_.find(name_it->second);
        return table_it == tables_.end() ? nullptr : table_it->second.get();
    }

    TableInfo* Catalog::GetTable(table_oid_t table_oid) noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    const TableInfo* Catalog::GetTable(table_oid_t table_oid) const noexcept
    {
        std::shared_lock lock(latch_);
        const auto it = tables_.find(table_oid);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    bool Catalog::HasTable(std::string_view table_name) const noexcept
    {
        std::shared_lock lock(latch_);
        return name_to_oid_.contains(NormalizeTableName(table_name));
    }

    std::vector<TableInfo*> Catalog::GetAllTables()
    {
        std::shared_lock lock(latch_);
        std::vector<TableInfo*> table_infos;
        table_infos.reserve(tables_.size());

        for (const auto& [oid, table_info] : tables_) {
            (void)oid;
            table_infos.push_back(table_info.get());
        }

        std::ranges::sort(table_infos, [](const TableInfo* lhs, const TableInfo* rhs) {
            return lhs->Oid() < rhs->Oid();
        });
        return table_infos;
    }

    std::vector<const TableInfo*> Catalog::GetAllTables() const
    {
        std::shared_lock lock(latch_);
        std::vector<const TableInfo*> table_infos;
        table_infos.reserve(tables_.size());

        for (const auto& [oid, table_info] : tables_) {
            (void)oid;
            table_infos.push_back(table_info.get());
        }

        std::ranges::sort(table_infos, [](const TableInfo* lhs, const TableInfo* rhs) {
            return lhs->Oid() < rhs->Oid();
        });
        return table_infos;
    }

    size_t Catalog::TableCount() const noexcept
    {
        std::shared_lock lock(latch_);
        return tables_.size();
    }

    std::expected<std::unique_ptr<table::TableHeap>, std::string> Catalog::CreateTableHeap()
    {
        if (bpm_ == nullptr) {
            return std::unexpected("Catalog: buffer pool manager is null");
        }

        page_id_t first_page_id = INVALID_PAGE_ID;
        auto first_page_exp = bpm_->NewPage(&first_page_id);
        if (!first_page_exp.has_value()) {
            return std::unexpected(
                "Catalog: failed to create first table page: " + first_page_exp.error().msg);
        }

        storage::Page* first_page = first_page_exp.value();
        first_page->MarkDirty();

        if (!bpm_->UnpinPage(first_page_id, true)) {
            bpm_->DeletePage(first_page_id);
            return std::unexpected("Catalog: failed to unpin first table page");
        }

        return std::make_unique<table::TableHeap>(bpm_, first_page_id);
    }

    table_oid_t Catalog::AllocateTableOid() noexcept
    {
        return next_table_oid_++;
    }

    std::expected<void, std::string> Catalog::ValidateTableName(std::string_view table_name)
    {
        if (table_name.empty()) {
            return std::unexpected("Catalog: table name must not be empty");
        }

        const bool all_space = std::ranges::all_of(table_name, [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
        if (all_space) {
            return std::unexpected("Catalog: table name must not be blank");
        }

        for (unsigned char ch : table_name) {
            if (std::iscntrl(ch) != 0) {
                return std::unexpected("Catalog: table name must not contain control characters");
            }
        }

        return {};
    }

} // namespace catalog
} // namespace HaruhiDB
