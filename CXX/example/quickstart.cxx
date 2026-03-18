#include "catalog/column.h"
#include "catalog/schema.h"
#include "execution/delete_executor.h"
#include "execution/filter_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/projection_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/update_executor.h"
#include "execution/values_executor.h"
#include "runtime/database_runtime.h"
#include "type/value.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using namespace HaruhiDB;

void Require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

catalog::Schema MakeStudentSchema()
{
    auto schema_exp = catalog::Schema::Create({
        catalog::Column("id", type::TypeId::INTEGER, false),
        catalog::Column("name", type::TypeId::VARCHAR, 32, false),
    });
    if (!schema_exp.has_value()) {
        throw std::runtime_error(schema_exp.error());
    }
    return std::move(schema_exp.value());
}

std::string RowToString(const execution::ExecutorRow& row)
{
    std::string out = "[";
    for (size_t i = 0; i < row.values.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += row.values[i].ToString();
    }
    out += "]";
    if (row.has_rid) {
        out += " @RID(";
        out += std::to_string(row.rid.GetPageId());
        out += ", ";
        out += std::to_string(row.rid.GetSlotId());
        out += ")";
    }
    return out;
}

void PrintHeader(std::string_view title)
{
    std::cout << "\n== " << title << " ==\n";
}

void PrintSeqScan(execution::ExecutorContext* exec_ctx, catalog::TableInfo* table_info)
{
    execution::SeqScanExecutor scan(exec_ctx, table_info);
    scan.Init();

    execution::ExecutorRow row;
    while (scan.Next(&row)) {
        std::cout << "  " << RowToString(row) << '\n';
    }
}

void PrintIndexScan(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    storage::BPlusTree* index,
    int32_t start_key)
{
    execution::IndexScanExecutor scan(exec_ctx, table_info, index, start_key);
    scan.Init();

    execution::ExecutorRow row;
    while (scan.Next(&row)) {
        std::cout << "  " << RowToString(row) << '\n';
    }
}

int32_t InsertRows(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    std::vector<std::vector<type::Value>> rows)
{
    auto values = std::make_unique<execution::ValuesExecutor>(exec_ctx, std::move(rows));
    execution::InsertExecutor insert(exec_ctx, table_info, std::move(values));
    insert.Init();

    execution::ExecutorRow result;
    Require(insert.Next(&result), "InsertExecutor returned no result row");
    const int32_t* inserted_count = result.values.empty() ? nullptr : result.values[0].TryAs<int32_t>();
    Require(inserted_count != nullptr, "InsertExecutor result is not an INTEGER count");
    return *inserted_count;
}

int32_t DeleteWhereId(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    int32_t id)
{
    auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
    auto filter = std::make_unique<execution::FilterExecutor>(
        exec_ctx,
        std::move(scan_child),
        [id](const execution::ExecutorRow& row) {
            return !row.values.empty() && row.values[0] == type::Value::Int32(id);
        });

    execution::DeleteExecutor deleter(exec_ctx, table_info, std::move(filter));
    deleter.Init();

    execution::ExecutorRow result;
    Require(deleter.Next(&result), "DeleteExecutor returned no result row");
    const int32_t* deleted_count = result.values.empty() ? nullptr : result.values[0].TryAs<int32_t>();
    Require(deleted_count != nullptr, "DeleteExecutor result is not an INTEGER count");
    return *deleted_count;
}

int32_t UpdateNameById(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    int32_t id,
    std::string next_name)
{
    auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
    auto filter = std::make_unique<execution::FilterExecutor>(
        exec_ctx,
        std::move(scan_child),
        [id](const execution::ExecutorRow& row) {
            return !row.values.empty() && row.values[0] == type::Value::Int32(id);
        });

    execution::UpdateExecutor updater(
        exec_ctx,
        table_info,
        std::move(filter),
        [next_name = std::move(next_name)](const execution::ExecutorRow& row) {
            auto out = row.values;
            if (out.size() > 1) {
                out[1] = type::Value::VarChar(next_name);
            }
            return out;
        });
    updater.Init();

    execution::ExecutorRow result;
    Require(updater.Next(&result), "UpdateExecutor returned no result row");
    const int32_t* updated_count = result.values.empty() ? nullptr : result.values[0].TryAs<int32_t>();
    Require(updated_count != nullptr, "UpdateExecutor result is not an INTEGER count");
    return *updated_count;
}

void PrintProjectedNameForId(
    execution::ExecutorContext* exec_ctx,
    catalog::TableInfo* table_info,
    int32_t id)
{
    auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
    auto filter = std::make_unique<execution::FilterExecutor>(
        exec_ctx,
        std::move(scan_child),
        [id](const execution::ExecutorRow& row) {
            return !row.values.empty() && row.values[0] == type::Value::Int32(id);
        });

    execution::ProjectionExecutor projection(exec_ctx, std::move(filter), {1});
    projection.Init();

    execution::ExecutorRow row;
    while (projection.Next(&row)) {
        std::cout << "  projected name: " << RowToString(row) << '\n';
    }
}

runtime::DatabaseRuntime OpenRuntime(
    const std::filesystem::path& db_path,
    const std::filesystem::path& wal_path)
{
    auto open_exp = runtime::DatabaseRuntime::Open(
        db_path,
        runtime::DatabaseOpenOptions{
            .buffer_pool_size = 64,
            .lru_k = 2,
            .enable_wal = true,
            .wal_path = wal_path,
        });
    if (!open_exp.has_value()) {
        throw std::runtime_error(open_exp.error());
    }
    return std::move(open_exp.value());
}

std::filesystem::path DefaultDemoDbPath(const char* argv0)
{
    std::error_code ec;
    std::filesystem::path exe_path =
        argv0 == nullptr ? std::filesystem::current_path() : std::filesystem::absolute(argv0, ec);
    if (ec || exe_path.empty()) {
        exe_path = std::filesystem::current_path();
    }

    std::filesystem::path output_dir = exe_path.has_parent_path() ? exe_path.parent_path() : exe_path;
    std::filesystem::create_directories(output_dir, ec);
    return output_dir / "quickstart_demo.db";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        std::filesystem::path db_path =
            argc > 1 ? std::filesystem::path(argv[1]) : DefaultDemoDbPath(argc > 0 ? argv[0] : nullptr);
        std::filesystem::path wal_path = db_path;
        wal_path.replace_extension(".wal");

        std::error_code ec;
        std::filesystem::remove(db_path, ec);
        std::filesystem::remove(wal_path, ec);

        table_oid_t table_oid = 0;
        index_oid_t index_oid = 0;

        PrintHeader("Open Runtime");
        std::cout << "database file: " << db_path << '\n';
        std::cout << "wal file     : " << wal_path << '\n';

        {
            auto runtime = OpenRuntime(db_path, wal_path);
            auto* catalog = runtime.GetCatalog();
            auto* exec_ctx = runtime.GetExecutorContext();
            Require(catalog != nullptr, "Catalog is null");
            Require(exec_ctx != nullptr, "ExecutorContext is null");

            PrintHeader("Create Table And Index");
            auto table_exp = catalog->CreateTable("student", MakeStudentSchema());
            if (!table_exp.has_value()) {
                throw std::runtime_error(table_exp.error());
            }
            catalog::TableInfo* table_info = table_exp.value();
            Require(table_info != nullptr, "CreateTable returned null TableInfo");

            auto index_exp = catalog->CreateIndex(table_info->Oid(), "idx_student_id");
            if (!index_exp.has_value()) {
                throw std::runtime_error(index_exp.error());
            }
            storage::BPlusTree* index = index_exp.value();
            Require(index != nullptr, "CreateIndex returned null index");

            table_oid = table_info->Oid();
            Require(!table_info->IndexOids().empty(), "table has no registered index oid");
            index_oid = table_info->IndexOids().front();

            std::cout << "table oid : " << table_oid << '\n';
            std::cout << "index oid : " << index_oid << '\n';

            PrintHeader("Insert Rows");
            const int32_t inserted = InsertRows(
                exec_ctx,
                table_info,
                {
                    {type::Value::Int32(1), type::Value::VarChar("haruhi")},
                    {type::Value::Int32(2), type::Value::VarChar("mio")},
                    {type::Value::Int32(3), type::Value::VarChar("yuki")},
                });
            std::cout << "inserted rows: " << inserted << '\n';

            PrintHeader("Seq Scan");
            PrintSeqScan(exec_ctx, table_info);

            PrintHeader("Filter + Projection (id = 2)");
            PrintProjectedNameForId(exec_ctx, table_info, 2);

            PrintHeader("Index Scan (start_key = 2)");
            PrintIndexScan(exec_ctx, table_info, index, 2);

            PrintHeader("Update id = 2");
            std::cout << "updated rows: " << UpdateNameById(exec_ctx, table_info, 2, "mio-updated") << '\n';
            PrintSeqScan(exec_ctx, table_info);

            PrintHeader("Delete id = 1");
            std::cout << "deleted rows: " << DeleteWhereId(exec_ctx, table_info, 1) << '\n';
            PrintSeqScan(exec_ctx, table_info);

            PrintHeader("Flush All Pages");
            auto* bpm = runtime.GetBufferPoolManager();
            Require(bpm != nullptr, "BufferPoolManager is null");
            auto flush_exp = bpm->FlushAllPages();
            if (!flush_exp.has_value()) {
                throw std::runtime_error(flush_exp.error().msg);
            }
            std::cout << "flush complete\n";
        }

        PrintHeader("Reopen And Verify Recovery");
        {
            auto runtime = OpenRuntime(db_path, wal_path);
            auto* catalog = runtime.GetCatalog();
            auto* exec_ctx = runtime.GetExecutorContext();
            Require(catalog != nullptr, "Catalog is null after reopen");
            Require(exec_ctx != nullptr, "ExecutorContext is null after reopen");

            auto* table_info = catalog->GetTable(table_oid);
            Require(table_info != nullptr, "recovered table not found");
            auto* index = catalog->GetIndex(table_oid, index_oid);
            Require(index != nullptr, "recovered index not found");

            std::cout << "recovered table: " << table_info->Name() << '\n';
            PrintSeqScan(exec_ctx, table_info);

            PrintHeader("Recovered Index Scan (start_key = 2)");
            PrintIndexScan(exec_ctx, table_info, index, 2);
        }

        PrintHeader("Done");
        std::cout << "Quickstart completed successfully.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "quickstart failed: " << e.what() << '\n';
        return 1;
    }
}
