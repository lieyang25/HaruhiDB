#include "capi/haruhidb.h"

#include "catalog/column.h"
#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "execution/delete_executor.h"
#include "execution/executor.h"
#include "execution/filter_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/update_executor.h"
#include "execution/values_executor.h"
#include "runtime/database_runtime.h"
#include "type/type.h"
#include "type/value.h"

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <expected>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct DatabaseState
{
    explicit DatabaseState(HaruhiDB::runtime::DatabaseRuntime runtime_in)
        : runtime(std::move(runtime_in))
    {
    }

    HaruhiDB::runtime::DatabaseRuntime runtime;
};

struct haruhidb_database
{
    std::shared_ptr<DatabaseState> state;
};

struct haruhidb_scan
{
    std::shared_ptr<DatabaseState> state;
    HaruhiDB::catalog::TableInfo* table_info{nullptr};
    std::unique_ptr<HaruhiDB::execution::AbstractExecutor> executor;
};

namespace
{

using HaruhiDB::catalog::Catalog;
using HaruhiDB::catalog::Column;
using HaruhiDB::catalog::Schema;
using HaruhiDB::catalog::TableInfo;
using HaruhiDB::execution::DeleteExecutor;
using HaruhiDB::execution::AbstractExecutor;
using HaruhiDB::execution::ExecutorContext;
using HaruhiDB::execution::ExecutorRow;
using HaruhiDB::execution::FilterExecutor;
using HaruhiDB::execution::IndexScanExecutor;
using HaruhiDB::execution::InsertExecutor;
using HaruhiDB::execution::SeqScanExecutor;
using HaruhiDB::execution::UpdateExecutor;
using HaruhiDB::execution::ValuesExecutor;
using HaruhiDB::runtime::DatabaseOpenOptions;
using HaruhiDB::runtime::DatabaseRuntime;
using HaruhiDB::type::TypeId;
using HaruhiDB::type::TypeUtil;
using HaruhiDB::type::Value;

constexpr size_t kDefaultBufferPoolSize = 64;
constexpr size_t kDefaultLRUK = 2;
constexpr char kApiVersion[] = "1.1.0";
constexpr haruhidb_capabilities_t kCapabilities =
    HARUHIDB_CAPABILITY_PRIMARY_INT_INDEX |
    HARUHIDB_CAPABILITY_PRIMARY_INT_POINT_GET |
    HARUHIDB_CAPABILITY_PRIMARY_INT_RANGE_SCAN |
    HARUHIDB_CAPABILITY_METADATA_READ |
    HARUHIDB_CAPABILITY_WAL_RUNTIME_OPTION;

struct RowAllocationMetadata
{
    std::vector<char*> owned_string_buffers;
};

std::mutex& RegistryMutex()
{
    static std::mutex mutex;
    return mutex;
}

auto& ActiveDatabaseHandles()
{
    static std::unordered_set<const haruhidb_database_t*> handles;
    return handles;
}

auto& ActiveScanHandles()
{
    static std::unordered_set<const haruhidb_scan_t*> handles;
    return handles;
}

auto& RowAllocations()
{
    static std::unordered_map<const haruhidb_value_t*, RowAllocationMetadata> allocations;
    return allocations;
}

bool RegisterDatabaseHandle(const haruhidb_database_t* db) noexcept
{
    if (db == nullptr) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> guard(RegistryMutex());
        return ActiveDatabaseHandles().insert(db).second;
    } catch (...) {
        return false;
    }
}

bool IsDatabaseHandleValid(const haruhidb_database_t* db)
{
    if (db == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> guard(RegistryMutex());
    return ActiveDatabaseHandles().contains(db);
}

bool UnregisterDatabaseHandle(const haruhidb_database_t* db)
{
    if (db == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> guard(RegistryMutex());
    return ActiveDatabaseHandles().erase(db) > 0;
}

bool RegisterScanHandle(const haruhidb_scan_t* scan) noexcept
{
    if (scan == nullptr) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> guard(RegistryMutex());
        return ActiveScanHandles().insert(scan).second;
    } catch (...) {
        return false;
    }
}

bool IsScanHandleValid(const haruhidb_scan_t* scan)
{
    if (scan == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> guard(RegistryMutex());
    return ActiveScanHandles().contains(scan);
}

bool UnregisterScanHandle(const haruhidb_scan_t* scan)
{
    if (scan == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> guard(RegistryMutex());
    return ActiveScanHandles().erase(scan) > 0;
}

bool RegisterRowAllocation(
    const haruhidb_value_t* values,
    std::vector<char*>&& owned_string_buffers) noexcept
{
    if (values == nullptr) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> guard(RegistryMutex());
        RowAllocations().insert_or_assign(
            values,
            RowAllocationMetadata{
                .owned_string_buffers = std::move(owned_string_buffers),
            });
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<RowAllocationMetadata> TakeRowAllocation(const haruhidb_value_t* values)
{
    if (values == nullptr) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> guard(RegistryMutex());
    auto it = RowAllocations().find(values);
    if (it == RowAllocations().end()) {
        return std::nullopt;
    }

    RowAllocationMetadata out = std::move(it->second);
    RowAllocations().erase(it);
    return out;
}

void ClearError(char** out_error)
{
    if (out_error != nullptr) {
        *out_error = nullptr;
    }
}

void ClearErrorCode(haruhidb_error_code_t* out_error_code)
{
    if (out_error_code != nullptr) {
        *out_error_code = HARUHIDB_ERROR_OK;
    }
}

auto& ActiveErrorCodeOutput()
{
    static thread_local haruhidb_error_code_t* out_error_code = nullptr;
    return out_error_code;
}

auto& ActiveFallbackErrorCode()
{
    static thread_local haruhidb_error_code_t fallback = HARUHIDB_ERROR_INTERNAL;
    return fallback;
}

class ErrorCodeScope
{
public:
    ErrorCodeScope(
        haruhidb_error_code_t* out_error_code,
        haruhidb_error_code_t fallback)
        : old_out_(ActiveErrorCodeOutput()),
          old_fallback_(ActiveFallbackErrorCode())
    {
        ActiveErrorCodeOutput() = out_error_code;
        ActiveFallbackErrorCode() = fallback;
    }

    ~ErrorCodeScope()
    {
        ActiveErrorCodeOutput() = old_out_;
        ActiveFallbackErrorCode() = old_fallback_;
    }

private:
    haruhidb_error_code_t* old_out_{nullptr};
    haruhidb_error_code_t old_fallback_{HARUHIDB_ERROR_INTERNAL};
};

std::string Lowercase(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (unsigned char ch : text) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool Contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

haruhidb_error_code_t ClassifyError(
    std::string_view message,
    haruhidb_error_code_t fallback)
{
    const std::string lowered = Lowercase(message);
    const std::string_view text(lowered);

    if (Contains(text, "invalid or already closed")) {
        return HARUHIDB_ERROR_INVALID_HANDLE;
    }
    if (Contains(text, "already exists") || Contains(text, "duplicate")) {
        return HARUHIDB_ERROR_ALREADY_EXISTS;
    }
    if (Contains(text, "not found") || Contains(text, "out of range")) {
        return HARUHIDB_ERROR_NOT_FOUND;
    }
    if (Contains(text, "not supported") || Contains(text, "required for this operation")) {
        return HARUHIDB_ERROR_UNSUPPORTED;
    }
    if (Contains(text, "must equal key") ||
        Contains(text, "value cannot cast") ||
        Contains(text, "value_count does not match") ||
        Contains(text, "indexed key") ||
        Contains(text, "did not insert exactly one row")) {
        return HARUHIDB_ERROR_CONSTRAINT;
    }
    if (Contains(text, "disk") ||
        Contains(text, "wal") ||
        Contains(text, "recover") ||
        Contains(text, "i/o") ||
        Contains(text, "read") ||
        Contains(text, "write") ||
        Contains(text, "flush")) {
        return HARUHIDB_ERROR_IO;
    }
    if (Contains(text, "must not") ||
        Contains(text, "must be") ||
        Contains(text, "invalid")) {
        return HARUHIDB_ERROR_INVALID_ARGUMENT;
    }

    return fallback;
}

char* DuplicateCString(std::string_view text)
{
    char* copy = static_cast<char*>(std::malloc(text.size() + 1));
    if (copy == nullptr) {
        return nullptr;
    }

    if (!text.empty()) {
        std::memcpy(copy, text.data(), text.size());
    }
    copy[text.size()] = '\0';
    return copy;
}

haruhidb_status_t FailWithCode(
    haruhidb_error_code_t code,
    char** out_error,
    std::string_view message)
{
    if (ActiveErrorCodeOutput() != nullptr) {
        *ActiveErrorCodeOutput() = code;
    }
    if (out_error != nullptr) {
        *out_error = DuplicateCString(message);
    }
    return HARUHIDB_STATUS_ERROR;
}

haruhidb_status_t Fail(char** out_error, std::string_view message)
{
    return FailWithCode(
        ClassifyError(message, ActiveFallbackErrorCode()),
        out_error,
        message);
}

template <typename Fn>
haruhidb_status_t Guard(
    haruhidb_error_code_t* out_error_code,
    char** out_error,
    std::string_view op_name,
    Fn&& fn)
{
    ClearError(out_error);
    ClearErrorCode(out_error_code);
    ErrorCodeScope error_code_scope(out_error_code, HARUHIDB_ERROR_INTERNAL);

    try {
        return fn();
    } catch (const std::bad_alloc&) {
        return FailWithCode(
            HARUHIDB_ERROR_INTERNAL,
            out_error,
            std::string(op_name) + ": out of memory");
    } catch (const std::exception& e) {
        return FailWithCode(
            HARUHIDB_ERROR_INTERNAL,
            out_error,
            std::string(op_name) + ": " + e.what());
    } catch (...) {
        return FailWithCode(
            HARUHIDB_ERROR_INTERNAL,
            out_error,
            std::string(op_name) + ": unknown exception");
    }
}

template <typename Fn>
haruhidb_status_t Guard(char** out_error, std::string_view op_name, Fn&& fn)
{
    return Guard(nullptr, out_error, op_name, std::forward<Fn>(fn));
}

void ResetRow(haruhidb_row_t* row)
{
    if (row != nullptr) {
        row->values = nullptr;
        row->value_count = 0;
    }
}

void DestroyTemporaryRow(haruhidb_row_t* row)
{
    if (row == nullptr || row->values == nullptr) {
        ResetRow(row);
        return;
    }

    for (size_t i = 0; i < row->value_count; ++i) {
        if (row->values[i].type == HARUHIDB_TYPE_VARCHAR && row->values[i].string_data != nullptr) {
            std::free(const_cast<char*>(row->values[i].string_data));
        }
    }

    std::free(row->values);
    ResetRow(row);
}

std::expected<std::string, std::string> RequireCString(
    const char* text,
    std::string_view field_name,
    bool allow_empty = false)
{
    if (text == nullptr) {
        return std::unexpected(std::string(field_name) + " must not be null");
    }

    std::string out(text);
    if (!allow_empty && out.empty()) {
        return std::unexpected(std::string(field_name) + " must not be empty");
    }
    return out;
}

std::expected<TypeId, std::string> ConvertType(haruhidb_type_t type)
{
    switch (type) {
        case HARUHIDB_TYPE_BOOLEAN: return TypeId::BOOLEAN;
        case HARUHIDB_TYPE_TINYINT: return TypeId::TINYINT;
        case HARUHIDB_TYPE_SMALLINT: return TypeId::SMALLINT;
        case HARUHIDB_TYPE_INTEGER: return TypeId::INTEGER;
        case HARUHIDB_TYPE_BIGINT: return TypeId::BIGINT;
        case HARUHIDB_TYPE_FLOAT: return TypeId::FLOAT;
        case HARUHIDB_TYPE_DOUBLE: return TypeId::DOUBLE;
        case HARUHIDB_TYPE_DECIMAL:
            return std::unexpected("DECIMAL is not supported in the first version");
        case HARUHIDB_TYPE_VARCHAR: return TypeId::VARCHAR;
        case HARUHIDB_TYPE_INVALID:
        default:
            return std::unexpected("invalid type id");
    }
}

std::expected<haruhidb_type_t, std::string> ConvertTypeToC(TypeId type)
{
    switch (type) {
        case TypeId::BOOLEAN: return HARUHIDB_TYPE_BOOLEAN;
        case TypeId::TINYINT: return HARUHIDB_TYPE_TINYINT;
        case TypeId::SMALLINT: return HARUHIDB_TYPE_SMALLINT;
        case TypeId::INTEGER: return HARUHIDB_TYPE_INTEGER;
        case TypeId::BIGINT: return HARUHIDB_TYPE_BIGINT;
        case TypeId::FLOAT: return HARUHIDB_TYPE_FLOAT;
        case TypeId::DOUBLE: return HARUHIDB_TYPE_DOUBLE;
        case TypeId::VARCHAR: return HARUHIDB_TYPE_VARCHAR;
        case TypeId::DECIMAL:
            return std::unexpected("DECIMAL is not supported in the first version");
        case TypeId::INVALID:
        default:
            return std::unexpected("invalid type id");
    }
}

std::expected<Column, std::string> ConvertColumnDef(const haruhidb_column_def_t& def)
{
    auto name = RequireCString(def.name, "column name");
    if (!name.has_value()) {
        return std::unexpected(name.error());
    }
    if (def.nullable) {
        return std::unexpected("nullable columns are not supported in the first version");
    }

    auto type_exp = ConvertType(def.type);
    if (!type_exp.has_value()) {
        return std::unexpected(type_exp.error());
    }

    if (TypeUtil::IsVariableLength(type_exp.value())) {
        if (def.length == 0) {
            return std::unexpected("VARCHAR columns must declare a positive length");
        }
        return Column(std::move(name.value()), type_exp.value(), def.length, false);
    }

    if (def.length != 0) {
        return std::unexpected("fixed-length columns must set length to 0");
    }
    return Column(std::move(name.value()), type_exp.value(), false);
}

std::expected<std::vector<Column>, std::string> ConvertColumnDefs(
    const haruhidb_column_def_t* columns,
    size_t column_count)
{
    if (column_count == 0) {
        return std::unexpected("column_count must be greater than 0");
    }
    if (columns == nullptr) {
        return std::unexpected("columns must not be null when column_count is greater than 0");
    }

    std::vector<Column> converted;
    converted.reserve(column_count);
    for (size_t i = 0; i < column_count; ++i) {
        auto column = ConvertColumnDef(columns[i]);
        if (!column.has_value()) {
            return std::unexpected(
                "invalid column definition at index " + std::to_string(i) + ": " + column.error());
        }
        converted.push_back(std::move(column.value()));
    }
    return converted;
}

Catalog* GetCatalog(haruhidb_database_t* db)
{
    return db->state->runtime.GetCatalog();
}

ExecutorContext* GetExecutorContext(const std::shared_ptr<DatabaseState>& state)
{
    return state->runtime.GetExecutorContext();
}

std::expected<TableInfo*, std::string> LookupTable(haruhidb_database_t* db, const char* table_name)
{
    auto name = RequireCString(table_name, "table_name");
    if (!name.has_value()) {
        return std::unexpected(name.error());
    }

    auto* catalog = GetCatalog(db);
    if (catalog == nullptr) {
        return std::unexpected("catalog is null");
    }

    TableInfo* table_info = catalog->GetTable(name.value());
    if (table_info == nullptr) {
        return std::unexpected("table not found: " + name.value());
    }
    return table_info;
}

std::expected<void, std::string> ValidatePrimaryIntFirstColumn(const Schema& schema)
{
    if (schema.ColumnCount() == 0) {
        return std::unexpected("table schema has no columns");
    }

    const auto& key_column = schema.GetColumn(0);
    if (key_column.Type() != TypeId::INTEGER) {
        return std::unexpected("table first column must be INTEGER for primary-int operations");
    }
    if (key_column.Nullable()) {
        return std::unexpected("table first column must be NOT NULL for primary-int operations");
    }
    return {};
}

std::expected<void, std::string> ValidateUpdatePayloadMatchesKey(
    std::span<const Value> values,
    int32_t key)
{
    if (values.empty()) {
        return std::unexpected("update payload must not be empty");
    }
    const int32_t* payload_key = values[0].TryAs<int32_t>();
    if (payload_key == nullptr) {
        return std::unexpected("update payload first value must be INTEGER");
    }
    if (*payload_key != key) {
        return std::unexpected("update payload first value must equal key");
    }
    return {};
}

bool MatchPrimaryIntKey(const ExecutorRow& row, int32_t key)
{
    if (row.values.empty()) {
        return false;
    }
    const int32_t* id = row.values[0].TryAs<int32_t>();
    return id != nullptr && *id == key;
}

std::expected<HaruhiDB::storage::BPlusTree*, std::string> RequirePrimaryIntIndex(TableInfo* table_info)
{
    if (table_info == nullptr) {
        return std::unexpected("table info is null");
    }
    if (table_info->IndexEntries().empty()) {
        return std::unexpected("primary-int index is required for this operation");
    }

    auto* index = table_info->IndexEntries().front().index.get();
    if (index == nullptr) {
        return std::unexpected("primary-int index is null");
    }
    return index;
}

std::expected<std::unique_ptr<AbstractExecutor>, std::string> BuildPrimaryIntEqualityExecutor(
    ExecutorContext* exec_ctx,
    TableInfo* table_info,
    int32_t key,
    bool require_index)
{
    if (exec_ctx == nullptr) {
        return std::unexpected("executor context is null");
    }
    if (table_info == nullptr) {
        return std::unexpected("table info is null");
    }

    HaruhiDB::storage::BPlusTree* index = nullptr;
    if (!table_info->IndexEntries().empty()) {
        index = table_info->IndexEntries().front().index.get();
    }

    if (require_index) {
        auto index_exp = RequirePrimaryIntIndex(table_info);
        if (!index_exp.has_value()) {
            return std::unexpected(index_exp.error());
        }
        index = index_exp.value();
    }

    if (index != nullptr) {
        auto child = std::make_unique<IndexScanExecutor>(exec_ctx, table_info, index, key);
        return std::make_unique<FilterExecutor>(
            exec_ctx,
            std::move(child),
            [key](const ExecutorRow& row) {
                return MatchPrimaryIntKey(row, key);
            });
    }

    auto child = std::make_unique<SeqScanExecutor>(exec_ctx, table_info);
    return std::make_unique<FilterExecutor>(
        exec_ctx,
        std::move(child),
        [key](const ExecutorRow& row) {
            return MatchPrimaryIntKey(row, key);
        });
}

std::expected<std::unique_ptr<AbstractExecutor>, std::string> BuildPrimaryIntRangeExecutor(
    ExecutorContext* exec_ctx,
    TableInfo* table_info,
    int32_t start_key,
    int32_t end_key)
{
    if (exec_ctx == nullptr) {
        return std::unexpected("executor context is null");
    }
    if (table_info == nullptr) {
        return std::unexpected("table info is null");
    }
    if (start_key > end_key) {
        return std::unexpected("start_key must be less than or equal to end_key");
    }

    auto index_exp = RequirePrimaryIntIndex(table_info);
    if (!index_exp.has_value()) {
        return std::unexpected(index_exp.error());
    }

    auto child = std::make_unique<IndexScanExecutor>(exec_ctx, table_info, index_exp.value(), start_key);
    return std::make_unique<FilterExecutor>(
        exec_ctx,
        std::move(child),
        [start_key, end_key](const ExecutorRow& row) {
            if (row.values.empty()) {
                return false;
            }
            const int32_t* id = row.values[0].TryAs<int32_t>();
            if (id == nullptr) {
                return false;
            }
            return *id >= start_key && *id <= end_key;
        });
}

std::expected<size_t, std::string> ExtractAffectedRowCount(
    const ExecutorRow& result,
    std::string_view op_name)
{
    const int32_t* count = result.values.empty() ? nullptr : result.values[0].TryAs<int32_t>();
    if (count == nullptr) {
        return std::unexpected(std::string(op_name) + ": executor returned an invalid row count");
    }
    if (*count < 0) {
        return std::unexpected(std::string(op_name) + ": executor returned a negative row count");
    }
    return static_cast<size_t>(*count);
}

std::expected<Value, std::string> ConvertValue(const haruhidb_value_t& value)
{
    if (value.is_null) {
        return std::unexpected("NULL values are not supported in the first version");
    }

    auto type_exp = ConvertType(value.type);
    if (!type_exp.has_value()) {
        return std::unexpected(type_exp.error());
    }

    switch (type_exp.value()) {
        case TypeId::BOOLEAN: return Value::Boolean(value.boolean_v);
        case TypeId::TINYINT: return Value::Int8(value.int8_v);
        case TypeId::SMALLINT: return Value::Int16(value.int16_v);
        case TypeId::INTEGER: return Value::Int32(value.int32_v);
        case TypeId::BIGINT: return Value::Int64(value.int64_v);
        case TypeId::FLOAT: return Value::Float(value.float_v);
        case TypeId::DOUBLE: return Value::Double(value.double_v);
        case TypeId::VARCHAR: {
            if (value.string_data == nullptr && value.string_len != 0) {
                return std::unexpected("VARCHAR value has null data with non-zero length");
            }
            return Value::VarChar(std::string(value.string_data == nullptr ? "" : value.string_data, value.string_len));
        }
        case TypeId::DECIMAL:
        case TypeId::INVALID:
            break;
    }

    return std::unexpected("unsupported value type");
}

std::expected<std::vector<Value>, std::string> ConvertValues(
    const haruhidb_value_t* values,
    size_t value_count,
    const Schema& schema)
{
    if (value_count != schema.ColumnCount()) {
        return std::unexpected(
            "value_count does not match schema column count: expected " +
            std::to_string(schema.ColumnCount()) + ", got " +
            std::to_string(value_count));
    }
    if (value_count != 0 && values == nullptr) {
        return std::unexpected("values must not be null when value_count is greater than 0");
    }

    std::vector<Value> converted;
    converted.reserve(value_count);
    for (size_t i = 0; i < value_count; ++i) {
        auto value = ConvertValue(values[i]);
        if (!value.has_value()) {
            return std::unexpected(
                "invalid value at index " + std::to_string(i) + ": " + value.error());
        }
        converted.push_back(std::move(value.value()));
    }
    return converted;
}

haruhidb_status_t PopulateValue(const Value& value, haruhidb_value_t* out_value, char** out_error)
{
    if (out_value == nullptr) {
        return Fail(out_error, "output value pointer is null");
    }

    *out_value = haruhidb_value_t{};
    out_value->type = HARUHIDB_TYPE_INVALID;
    out_value->is_null = value.IsNull();

    if (value.IsNull()) {
        return HARUHIDB_STATUS_OK;
    }

    switch (value.Type()) {
        case TypeId::BOOLEAN: {
            const bool* v = value.TryAs<bool>();
            if (v == nullptr) {
                return Fail(out_error, "BOOLEAN value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_BOOLEAN;
            out_value->boolean_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::TINYINT: {
            const int8_t* v = value.TryAs<int8_t>();
            if (v == nullptr) {
                return Fail(out_error, "TINYINT value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_TINYINT;
            out_value->int8_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::SMALLINT: {
            const int16_t* v = value.TryAs<int16_t>();
            if (v == nullptr) {
                return Fail(out_error, "SMALLINT value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_SMALLINT;
            out_value->int16_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::INTEGER: {
            const int32_t* v = value.TryAs<int32_t>();
            if (v == nullptr) {
                return Fail(out_error, "INTEGER value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_INTEGER;
            out_value->int32_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::BIGINT: {
            const int64_t* v = value.TryAs<int64_t>();
            if (v == nullptr) {
                return Fail(out_error, "BIGINT value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_BIGINT;
            out_value->int64_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::FLOAT: {
            const float* v = value.TryAs<float>();
            if (v == nullptr) {
                return Fail(out_error, "FLOAT value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_FLOAT;
            out_value->float_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::DOUBLE: {
            const double* v = value.TryAs<double>();
            if (v == nullptr) {
                return Fail(out_error, "DOUBLE value payload is missing");
            }
            out_value->type = HARUHIDB_TYPE_DOUBLE;
            out_value->double_v = *v;
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::VARCHAR: {
            const std::string* text = value.TryAs<std::string>();
            if (text == nullptr) {
                return Fail(out_error, "VARCHAR value payload is missing");
            }

            out_value->type = HARUHIDB_TYPE_VARCHAR;
            out_value->string_len = text->size();
            out_value->string_data = DuplicateCString(*text);
            if (out_value->string_data == nullptr && !text->empty()) {
                return Fail(out_error, "failed to allocate VARCHAR output buffer");
            }
            return HARUHIDB_STATUS_OK;
        }
        case TypeId::DECIMAL:
            return Fail(out_error, "DECIMAL is not supported in the first version");
        case TypeId::INVALID:
            break;
    }

    return Fail(out_error, "unsupported output value type");
}

haruhidb_status_t PopulateRow(const ExecutorRow& row, haruhidb_row_t* out_row, char** out_error)
{
    if (out_row == nullptr) {
        return Fail(out_error, "row must not be null");
    }

    ResetRow(out_row);
    if (row.values.empty()) {
        return HARUHIDB_STATUS_OK;
    }

    auto* copied_values = static_cast<haruhidb_value_t*>(
        std::calloc(row.values.size(), sizeof(haruhidb_value_t)));
    if (copied_values == nullptr) {
        return Fail(out_error, "failed to allocate row values");
    }

    haruhidb_row_t temp_row{
        .values = copied_values,
        .value_count = row.values.size(),
    };

    std::vector<char*> owned_string_buffers;
    owned_string_buffers.reserve(row.values.size());

    for (size_t i = 0; i < row.values.size(); ++i) {
        auto status = PopulateValue(row.values[i], &temp_row.values[i], out_error);
        if (status != HARUHIDB_STATUS_OK) {
            DestroyTemporaryRow(&temp_row);
            return status;
        }

        if (temp_row.values[i].type == HARUHIDB_TYPE_VARCHAR &&
            temp_row.values[i].string_data != nullptr) {
            owned_string_buffers.push_back(const_cast<char*>(temp_row.values[i].string_data));
        }
    }

    if (!RegisterRowAllocation(temp_row.values, std::move(owned_string_buffers))) {
        DestroyTemporaryRow(&temp_row);
        return Fail(out_error, "failed to track row allocation");
    }

    *out_row = temp_row;
    return HARUHIDB_STATUS_OK;
}

DatabaseOpenOptions ConvertOpenOptions(const haruhidb_open_options_t* options)
{
    DatabaseOpenOptions converted;
    if (options == nullptr) {
        return converted;
    }

    converted.buffer_pool_size =
        options->buffer_pool_size == 0 ? kDefaultBufferPoolSize : options->buffer_pool_size;
    converted.lru_k =
        options->lru_k == 0 ? kDefaultLRUK : options->lru_k;
    converted.enable_wal = options->enable_wal;
    if (options->wal_path != nullptr && options->wal_path[0] != '\0') {
        converted.wal_path = options->wal_path;
    }

    return converted;
}

} // namespace

extern "C" {

const char* haruhidb_api_version(void)
{
    return kApiVersion;
}

haruhidb_capabilities_t haruhidb_capabilities(void)
{
    return kCapabilities;
}

haruhidb_status_t haruhidb_open_ex(
    const char* db_path,
    const haruhidb_open_options_t* options,
    haruhidb_database_t** out_db,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_open", [&]() -> haruhidb_status_t {
        if (out_db == nullptr) {
            return Fail(out_error, "out_db must not be null");
        }
        *out_db = nullptr;

        auto db_path_text = RequireCString(db_path, "db_path");
        if (!db_path_text.has_value()) {
            return Fail(out_error, db_path_text.error());
        }

        auto opened = DatabaseRuntime::Open(db_path_text.value(), ConvertOpenOptions(options));
        if (!opened.has_value()) {
            return Fail(out_error, opened.error());
        }

        auto state = std::make_shared<DatabaseState>(std::move(opened.value()));
        auto* handle = new haruhidb_database{
            .state = std::move(state),
        };
        if (!RegisterDatabaseHandle(handle)) {
            delete handle;
            return Fail(out_error, "failed to register database handle");
        }

        *out_db = handle;
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_open(
    const char* db_path,
    const haruhidb_open_options_t* options,
    haruhidb_database_t** out_db,
    char** out_error)
{
    return haruhidb_open_ex(
        db_path,
        options,
        out_db,
        nullptr,
        out_error);
}

void haruhidb_close(haruhidb_database_t* db)
{
    if (db == nullptr || !UnregisterDatabaseHandle(db)) {
        return;
    }

    try {
        if (db->state != nullptr) {
            auto* bpm = db->state->runtime.GetBufferPoolManager();
            if (bpm != nullptr) {
                (void)bpm->FlushAllPages();
            }
        }
    } catch (...) {
        // C ABI close path must not propagate exceptions across language boundaries.
    }

    delete db;
}

haruhidb_status_t haruhidb_table_create_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_column_def_t* columns,
    size_t column_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_create", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }

        auto table_name_text = RequireCString(table_name, "table_name");
        if (!table_name_text.has_value()) {
            return Fail(out_error, table_name_text.error());
        }

        auto converted_columns = ConvertColumnDefs(columns, column_count);
        if (!converted_columns.has_value()) {
            return Fail(out_error, converted_columns.error());
        }

        auto schema = Schema::Create(std::move(converted_columns.value()));
        if (!schema.has_value()) {
            return Fail(out_error, schema.error());
        }

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        auto created = catalog->CreateTable(table_name_text.value(), schema.value());
        if (!created.has_value()) {
            return Fail(out_error, created.error());
        }
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_create(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_column_def_t* columns,
    size_t column_count,
    char** out_error)
{
    return haruhidb_table_create_ex(
        db,
        table_name,
        columns,
        column_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_exists_ex(
    haruhidb_database_t* db,
    const char* table_name,
    bool* out_exists,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_exists", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_exists == nullptr) {
            return Fail(out_error, "out_exists must not be null");
        }
        *out_exists = false;

        auto table_name_text = RequireCString(table_name, "table_name");
        if (!table_name_text.has_value()) {
            return Fail(out_error, table_name_text.error());
        }

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        *out_exists = catalog->HasTable(table_name_text.value());
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_exists(
    haruhidb_database_t* db,
    const char* table_name,
    bool* out_exists,
    char** out_error)
{
    return haruhidb_table_exists_ex(
        db,
        table_name,
        out_exists,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_index_create_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_index_create_primary_int", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }

        auto table_name_text = RequireCString(table_name, "table_name");
        if (!table_name_text.has_value()) {
            return Fail(out_error, table_name_text.error());
        }
        auto index_name_text = RequireCString(index_name, "index_name");
        if (!index_name_text.has_value()) {
            return Fail(out_error, index_name_text.error());
        }

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        auto created = catalog->CreateIndex(table_name_text.value(), index_name_text.value());
        if (!created.has_value()) {
            return Fail(out_error, created.error());
        }
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_index_create_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    char** out_error)
{
    return haruhidb_index_create_primary_int_ex(
        db,
        table_name,
        index_name,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_index_drop_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_index_drop", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }

        auto table_name_text = RequireCString(table_name, "table_name");
        if (!table_name_text.has_value()) {
            return Fail(out_error, table_name_text.error());
        }
        auto index_name_text = RequireCString(index_name, "index_name");
        if (!index_name_text.has_value()) {
            return Fail(out_error, index_name_text.error());
        }

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        auto dropped = catalog->DropIndex(table_name_text.value(), index_name_text.value());
        if (!dropped.has_value()) {
            return Fail(out_error, dropped.error());
        }
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_index_drop(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    char** out_error)
{
    return haruhidb_index_drop_ex(
        db,
        table_name,
        index_name,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_drop_ex(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_drop", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }

        auto table_name_text = RequireCString(table_name, "table_name");
        if (!table_name_text.has_value()) {
            return Fail(out_error, table_name_text.error());
        }

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        auto dropped = catalog->DropTable(table_name_text.value());
        if (!dropped.has_value()) {
            return Fail(out_error, dropped.error());
        }
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_drop(
    haruhidb_database_t* db,
    const char* table_name,
    char** out_error)
{
    return haruhidb_table_drop_ex(
        db,
        table_name,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_count_ex(
    haruhidb_database_t* db,
    size_t* out_table_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_count", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_table_count == nullptr) {
            return Fail(out_error, "out_table_count must not be null");
        }
        *out_table_count = 0;

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        *out_table_count = catalog->TableCount();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_count(
    haruhidb_database_t* db,
    size_t* out_table_count,
    char** out_error)
{
    return haruhidb_table_count_ex(
        db,
        out_table_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_name_at_ex(
    haruhidb_database_t* db,
    size_t table_index,
    char** out_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_name_at", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_name == nullptr) {
            return Fail(out_error, "out_name must not be null");
        }
        *out_name = nullptr;

        auto* catalog = GetCatalog(db);
        if (catalog == nullptr) {
            return Fail(out_error, "catalog is null");
        }

        const auto tables = catalog->GetAllTables();
        if (table_index >= tables.size()) {
            return FailWithCode(
                HARUHIDB_ERROR_NOT_FOUND,
                out_error,
                "table_index is out of range");
        }

        const std::string& name = tables[table_index]->Name();
        char* copied = DuplicateCString(name);
        if (copied == nullptr && !name.empty()) {
            return FailWithCode(
                HARUHIDB_ERROR_INTERNAL,
                out_error,
                "failed to allocate table name output buffer");
        }
        *out_name = copied;
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_name_at(
    haruhidb_database_t* db,
    size_t table_index,
    char** out_name,
    char** out_error)
{
    return haruhidb_table_name_at_ex(
        db,
        table_index,
        out_name,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_column_count_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_column_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_column_count", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_column_count == nullptr) {
            return Fail(out_error, "out_column_count must not be null");
        }
        *out_column_count = 0;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        *out_column_count = table_info.value()->GetSchema().ColumnCount();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_column_count(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_column_count,
    char** out_error)
{
    return haruhidb_table_column_count_ex(
        db,
        table_name,
        out_column_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_column_at_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t column_index,
    haruhidb_column_def_t* out_column,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_column_at", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_column == nullptr) {
            return Fail(out_error, "out_column must not be null");
        }
        *out_column = haruhidb_column_def_t{};

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        const auto& schema = table_info.value()->GetSchema();
        if (column_index >= schema.ColumnCount()) {
            return FailWithCode(
                HARUHIDB_ERROR_NOT_FOUND,
                out_error,
                "column_index is out of range");
        }

        const auto& column = schema.GetColumn(column_index);
        auto type_exp = ConvertTypeToC(column.Type());
        if (!type_exp.has_value()) {
            return Fail(out_error, type_exp.error());
        }

        const std::string& name = column.Name();
        char* copied_name = DuplicateCString(name);
        if (copied_name == nullptr && !name.empty()) {
            return FailWithCode(
                HARUHIDB_ERROR_INTERNAL,
                out_error,
                "failed to allocate column name output buffer");
        }

        out_column->name = copied_name;
        out_column->type = type_exp.value();
        out_column->length = TypeUtil::IsVariableLength(column.Type()) ? column.Length() : 0U;
        out_column->nullable = column.Nullable();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_column_at(
    haruhidb_database_t* db,
    const char* table_name,
    size_t column_index,
    haruhidb_column_def_t* out_column,
    char** out_error)
{
    return haruhidb_table_column_at_ex(
        db,
        table_name,
        column_index,
        out_column,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_index_count_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_index_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_index_count", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_index_count == nullptr) {
            return Fail(out_error, "out_index_count must not be null");
        }
        *out_index_count = 0;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        *out_index_count = table_info.value()->IndexEntries().size();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_index_count(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_index_count,
    char** out_error)
{
    return haruhidb_table_index_count_ex(
        db,
        table_name,
        out_index_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_table_index_name_at_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t index_pos,
    char** out_index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_table_index_name_at", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_index_name == nullptr) {
            return Fail(out_error, "out_index_name must not be null");
        }
        *out_index_name = nullptr;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        const auto& indexes = table_info.value()->IndexEntries();
        if (index_pos >= indexes.size()) {
            return FailWithCode(
                HARUHIDB_ERROR_NOT_FOUND,
                out_error,
                "index_pos is out of range");
        }

        const std::string& index_name = indexes[index_pos].index_name;
        char* copied = DuplicateCString(index_name);
        if (copied == nullptr && !index_name.empty()) {
            return FailWithCode(
                HARUHIDB_ERROR_INTERNAL,
                out_error,
                "failed to allocate index name output buffer");
        }
        *out_index_name = copied;
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_table_index_name_at(
    haruhidb_database_t* db,
    const char* table_name,
    size_t index_pos,
    char** out_index_name,
    char** out_error)
{
    return haruhidb_table_index_name_at_ex(
        db,
        table_name,
        index_pos,
        out_index_name,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_row_insert_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_value_t* values,
    size_t value_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_row_insert", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto converted_values = ConvertValues(values, value_count, table_info.value()->GetSchema());
        if (!converted_values.has_value()) {
            return Fail(out_error, converted_values.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto child = std::make_unique<ValuesExecutor>(
            exec_ctx,
            std::vector<std::vector<Value>>{std::move(converted_values.value())});
        InsertExecutor insert(exec_ctx, table_info.value(), std::move(child));
        insert.Init();

        ExecutorRow result;
        if (!insert.Next(&result)) {
            if (insert.Failed()) {
                return Fail(out_error, insert.LastError());
            }
            return Fail(out_error, "insert executor returned no result row");
        }

        const int32_t* inserted_count =
            result.values.empty() ? nullptr : result.values[0].TryAs<int32_t>();
        if (inserted_count == nullptr) {
            return Fail(out_error, "insert executor returned an invalid row count");
        }
        if (*inserted_count != 1) {
            return Fail(out_error, "insert executor did not insert exactly one row");
        }
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_row_insert(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_value_t* values,
    size_t value_count,
    char** out_error)
{
    return haruhidb_row_insert_ex(
        db,
        table_name,
        values,
        value_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_row_update_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    const haruhidb_value_t* values,
    size_t value_count,
    size_t* out_updated_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_row_update_primary_int", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_updated_count == nullptr) {
            return Fail(out_error, "out_updated_count must not be null");
        }
        *out_updated_count = 0;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto key_validated = ValidatePrimaryIntFirstColumn(table_info.value()->GetSchema());
        if (!key_validated.has_value()) {
            return Fail(out_error, key_validated.error());
        }

        auto converted_values = ConvertValues(values, value_count, table_info.value()->GetSchema());
        if (!converted_values.has_value()) {
            return Fail(out_error, converted_values.error());
        }

        auto payload_checked = ValidateUpdatePayloadMatchesKey(converted_values.value(), key);
        if (!payload_checked.has_value()) {
            return Fail(out_error, payload_checked.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto child_exp = BuildPrimaryIntEqualityExecutor(
            exec_ctx,
            table_info.value(),
            key,
            false);
        if (!child_exp.has_value()) {
            return Fail(out_error, child_exp.error());
        }

        std::vector<Value> next_values = std::move(converted_values.value());
        UpdateExecutor updater(
            exec_ctx,
            table_info.value(),
            std::move(child_exp.value()),
            [next_values](const ExecutorRow&) {
                return next_values;
            });
        updater.Init();

        ExecutorRow result;
        if (!updater.Next(&result)) {
            return Fail(out_error, "update executor returned no result row");
        }

        auto updated_count = ExtractAffectedRowCount(result, "update executor");
        if (!updated_count.has_value()) {
            return Fail(out_error, updated_count.error());
        }

        *out_updated_count = updated_count.value();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_row_update_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    const haruhidb_value_t* values,
    size_t value_count,
    size_t* out_updated_count,
    char** out_error)
{
    return haruhidb_row_update_primary_int_ex(
        db,
        table_name,
        key,
        values,
        value_count,
        out_updated_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_row_delete_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    size_t* out_deleted_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_row_delete_primary_int", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_deleted_count == nullptr) {
            return Fail(out_error, "out_deleted_count must not be null");
        }
        *out_deleted_count = 0;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto key_validated = ValidatePrimaryIntFirstColumn(table_info.value()->GetSchema());
        if (!key_validated.has_value()) {
            return Fail(out_error, key_validated.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto child_exp = BuildPrimaryIntEqualityExecutor(
            exec_ctx,
            table_info.value(),
            key,
            false);
        if (!child_exp.has_value()) {
            return Fail(out_error, child_exp.error());
        }

        DeleteExecutor deleter(exec_ctx, table_info.value(), std::move(child_exp.value()));
        deleter.Init();

        ExecutorRow result;
        if (!deleter.Next(&result)) {
            return Fail(out_error, "delete executor returned no result row");
        }

        auto deleted_count = ExtractAffectedRowCount(result, "delete executor");
        if (!deleted_count.has_value()) {
            return Fail(out_error, deleted_count.error());
        }

        *out_deleted_count = deleted_count.value();
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_row_delete_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    size_t* out_deleted_count,
    char** out_error)
{
    return haruhidb_row_delete_primary_int_ex(
        db,
        table_name,
        key,
        out_deleted_count,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_scan_open_all_ex(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_scan_t** out_scan,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_scan_open_all", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_scan == nullptr) {
            return Fail(out_error, "out_scan must not be null");
        }
        *out_scan = nullptr;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto executor = std::make_unique<SeqScanExecutor>(exec_ctx, table_info.value());
        executor->Init();

        auto* handle = new haruhidb_scan{
            .state = db->state,
            .table_info = table_info.value(),
            .executor = std::move(executor),
        };
        if (!RegisterScanHandle(handle)) {
            delete handle;
            return Fail(out_error, "failed to register scan handle");
        }
        *out_scan = handle;
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_scan_open_all(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_scan_t** out_scan,
    char** out_error)
{
    return haruhidb_scan_open_all_ex(
        db,
        table_name,
        out_scan,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_scan_open_primary_int_range_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t start_key,
    int32_t end_key,
    haruhidb_scan_t** out_scan,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_scan_open_primary_int_range", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_scan == nullptr) {
            return Fail(out_error, "out_scan must not be null");
        }
        *out_scan = nullptr;

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto key_validated = ValidatePrimaryIntFirstColumn(table_info.value()->GetSchema());
        if (!key_validated.has_value()) {
            return Fail(out_error, key_validated.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto executor_exp = BuildPrimaryIntRangeExecutor(
            exec_ctx,
            table_info.value(),
            start_key,
            end_key);
        if (!executor_exp.has_value()) {
            return Fail(out_error, executor_exp.error());
        }

        auto executor = std::move(executor_exp.value());
        executor->Init();

        auto* handle = new haruhidb_scan{
            .state = db->state,
            .table_info = table_info.value(),
            .executor = std::move(executor),
        };
        if (!RegisterScanHandle(handle)) {
            delete handle;
            return Fail(out_error, "failed to register scan handle");
        }
        *out_scan = handle;
        return HARUHIDB_STATUS_OK;
    });
}

haruhidb_status_t haruhidb_scan_open_primary_int_range(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t start_key,
    int32_t end_key,
    haruhidb_scan_t** out_scan,
    char** out_error)
{
    return haruhidb_scan_open_primary_int_range_ex(
        db,
        table_name,
        start_key,
        end_key,
        out_scan,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_row_get_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    bool* out_found,
    haruhidb_row_t* out_row,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_row_get_primary_int", [&]() -> haruhidb_status_t {
        if (!IsDatabaseHandleValid(db)) {
            return Fail(out_error, "db handle is invalid or already closed");
        }
        if (out_found == nullptr) {
            return Fail(out_error, "out_found must not be null");
        }
        if (out_row == nullptr) {
            return Fail(out_error, "out_row must not be null");
        }
        *out_found = false;
        haruhidb_row_destroy(out_row);

        auto table_info = LookupTable(db, table_name);
        if (!table_info.has_value()) {
            return Fail(out_error, table_info.error());
        }

        auto key_validated = ValidatePrimaryIntFirstColumn(table_info.value()->GetSchema());
        if (!key_validated.has_value()) {
            return Fail(out_error, key_validated.error());
        }

        auto* exec_ctx = GetExecutorContext(db->state);
        if (exec_ctx == nullptr) {
            return Fail(out_error, "executor context is null");
        }

        auto child_exp = BuildPrimaryIntEqualityExecutor(
            exec_ctx,
            table_info.value(),
            key,
            true);
        if (!child_exp.has_value()) {
            return Fail(out_error, child_exp.error());
        }

        auto child = std::move(child_exp.value());
        child->Init();

        ExecutorRow result;
        if (!child->Next(&result)) {
            return HARUHIDB_STATUS_OK;
        }

        *out_found = true;
        return PopulateRow(result, out_row, out_error);
    });
}

haruhidb_status_t haruhidb_row_get_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    bool* out_found,
    haruhidb_row_t* out_row,
    char** out_error)
{
    return haruhidb_row_get_primary_int_ex(
        db,
        table_name,
        key,
        out_found,
        out_row,
        nullptr,
        out_error);
}

haruhidb_status_t haruhidb_scan_next_ex(
    haruhidb_scan_t* scan,
    haruhidb_row_t* row,
    haruhidb_error_code_t* out_error_code,
    char** out_error)
{
    return Guard(out_error_code, out_error, "haruhidb_scan_next", [&]() -> haruhidb_status_t {
        if (row == nullptr) {
            return Fail(out_error, "row must not be null");
        }
        haruhidb_row_destroy(row);

        if (!IsScanHandleValid(scan)) {
            return Fail(out_error, "scan handle is invalid or already closed");
        }
        if (scan->executor == nullptr) {
            return Fail(out_error, "scan executor is null");
        }

        ExecutorRow current_row;
        if (!scan->executor->Next(&current_row)) {
            if (auto* seq_scan = dynamic_cast<SeqScanExecutor*>(scan->executor.get());
                seq_scan != nullptr && seq_scan->Failed()) {
                return Fail(out_error, seq_scan->LastError());
            }
            return HARUHIDB_STATUS_END;
        }

        return PopulateRow(current_row, row, out_error);
    });
}

haruhidb_status_t haruhidb_scan_next(
    haruhidb_scan_t* scan,
    haruhidb_row_t* row,
    char** out_error)
{
    return haruhidb_scan_next_ex(
        scan,
        row,
        nullptr,
        out_error);
}

void haruhidb_scan_close(haruhidb_scan_t* scan)
{
    if (scan == nullptr || !UnregisterScanHandle(scan)) {
        return;
    }

    delete scan;
}

void haruhidb_row_destroy(haruhidb_row_t* row)
{
    if (row == nullptr || row->values == nullptr) {
        ResetRow(row);
        return;
    }

    auto allocation = TakeRowAllocation(row->values);
    if (!allocation.has_value()) {
        ResetRow(row);
        return;
    }

    for (char* text : allocation->owned_string_buffers) {
        std::free(text);
    }

    std::free(row->values);
    ResetRow(row);
}

void haruhidb_free_string(char* text)
{
    std::free(text);
}

} // extern "C"
