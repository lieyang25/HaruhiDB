#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct haruhidb_database haruhidb_database_t;
typedef struct haruhidb_scan haruhidb_scan_t;

typedef enum haruhidb_status_t {
    HARUHIDB_STATUS_OK = 0,
    HARUHIDB_STATUS_END = 1,
    HARUHIDB_STATUS_ERROR = 2,
} haruhidb_status_t;

typedef enum haruhidb_error_code_t {
    HARUHIDB_ERROR_OK = 0,
    HARUHIDB_ERROR_INVALID_ARGUMENT = 1,
    HARUHIDB_ERROR_INVALID_HANDLE = 2,
    HARUHIDB_ERROR_NOT_FOUND = 3,
    HARUHIDB_ERROR_ALREADY_EXISTS = 4,
    HARUHIDB_ERROR_UNSUPPORTED = 5,
    HARUHIDB_ERROR_CONSTRAINT = 6,
    HARUHIDB_ERROR_IO = 7,
    HARUHIDB_ERROR_INTERNAL = 8,
} haruhidb_error_code_t;

typedef enum haruhidb_type_t {
    HARUHIDB_TYPE_INVALID = 0,
    HARUHIDB_TYPE_BOOLEAN = 1,
    HARUHIDB_TYPE_TINYINT = 2,
    HARUHIDB_TYPE_SMALLINT = 3,
    HARUHIDB_TYPE_INTEGER = 4,
    HARUHIDB_TYPE_BIGINT = 5,
    HARUHIDB_TYPE_FLOAT = 6,
    HARUHIDB_TYPE_DOUBLE = 7,
    HARUHIDB_TYPE_DECIMAL = 8,
    HARUHIDB_TYPE_VARCHAR = 9,
} haruhidb_type_t;

typedef struct haruhidb_open_options_t {
    size_t buffer_pool_size;
    size_t lru_k;
    bool enable_wal;
    const char* wal_path;
} haruhidb_open_options_t;

typedef struct haruhidb_column_def_t {
    const char* name;
    haruhidb_type_t type;
    uint32_t length;
    bool nullable;
} haruhidb_column_def_t;

typedef struct haruhidb_value_t {
    haruhidb_type_t type;
    bool is_null;
    bool boolean_v;
    int8_t int8_v;
    int16_t int16_v;
    int32_t int32_v;
    int64_t int64_v;
    float float_v;
    double double_v;
    const char* string_data;
    size_t string_len;
} haruhidb_value_t;

typedef struct haruhidb_row_t {
    haruhidb_value_t* values;
    size_t value_count;
} haruhidb_row_t;

typedef uint64_t haruhidb_capabilities_t;
enum {
    HARUHIDB_CAPABILITY_PRIMARY_INT_INDEX = 1ULL << 0,
    HARUHIDB_CAPABILITY_PRIMARY_INT_POINT_GET = 1ULL << 1,
    HARUHIDB_CAPABILITY_PRIMARY_INT_RANGE_SCAN = 1ULL << 2,
    HARUHIDB_CAPABILITY_METADATA_READ = 1ULL << 3,
    HARUHIDB_CAPABILITY_WAL_RUNTIME_OPTION = 1ULL << 4,
};

/*
 * 所有 out_error / out_name / out_index_name / out_column->name 返回的字符串都由库分配，
 * 调用方必须用 haruhidb_free_string() 释放。
 */
const char* haruhidb_api_version(void);
haruhidb_capabilities_t haruhidb_capabilities(void);

haruhidb_status_t haruhidb_open_ex(
    const char* db_path,
    const haruhidb_open_options_t* options,
    haruhidb_database_t** out_db,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_open(
    const char* db_path,
    const haruhidb_open_options_t* options,
    haruhidb_database_t** out_db,
    char** out_error);

/*
 * 句柄语义：
 * - close 后句柄不可继续使用
 * - 对同一 stale handle 重复 close 是 no-op
 */
void haruhidb_close(haruhidb_database_t* db);

haruhidb_status_t haruhidb_table_create_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_column_def_t* columns,
    size_t column_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_create(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_column_def_t* columns,
    size_t column_count,
    char** out_error);

haruhidb_status_t haruhidb_table_exists_ex(
    haruhidb_database_t* db,
    const char* table_name,
    bool* out_exists,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_exists(
    haruhidb_database_t* db,
    const char* table_name,
    bool* out_exists,
    char** out_error);

/*
 * 当前接口只承诺“首列 INTEGER NOT NULL”索引语义。
 */
haruhidb_status_t haruhidb_index_create_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_index_create_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    char** out_error);

haruhidb_status_t haruhidb_row_insert_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_value_t* values,
    size_t value_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_row_insert(
    haruhidb_database_t* db,
    const char* table_name,
    const haruhidb_value_t* values,
    size_t value_count,
    char** out_error);

haruhidb_status_t haruhidb_row_update_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    const haruhidb_value_t* values,
    size_t value_count,
    size_t* out_updated_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_row_update_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    const haruhidb_value_t* values,
    size_t value_count,
    size_t* out_updated_count,
    char** out_error);

haruhidb_status_t haruhidb_row_delete_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    size_t* out_deleted_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_row_delete_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    size_t* out_deleted_count,
    char** out_error);

haruhidb_status_t haruhidb_row_get_primary_int_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    bool* out_found,
    haruhidb_row_t* out_row,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_row_get_primary_int(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t key,
    bool* out_found,
    haruhidb_row_t* out_row,
    char** out_error);

haruhidb_status_t haruhidb_index_drop_ex(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_index_drop(
    haruhidb_database_t* db,
    const char* table_name,
    const char* index_name,
    char** out_error);

haruhidb_status_t haruhidb_table_drop_ex(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_drop(
    haruhidb_database_t* db,
    const char* table_name,
    char** out_error);

haruhidb_status_t haruhidb_scan_open_all_ex(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_scan_t** out_scan,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_scan_open_all(
    haruhidb_database_t* db,
    const char* table_name,
    haruhidb_scan_t** out_scan,
    char** out_error);

haruhidb_status_t haruhidb_scan_open_primary_int_range_ex(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t start_key,
    int32_t end_key,
    haruhidb_scan_t** out_scan,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_scan_open_primary_int_range(
    haruhidb_database_t* db,
    const char* table_name,
    int32_t start_key,
    int32_t end_key,
    haruhidb_scan_t** out_scan,
    char** out_error);

haruhidb_status_t haruhidb_table_count_ex(
    haruhidb_database_t* db,
    size_t* out_table_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_count(
    haruhidb_database_t* db,
    size_t* out_table_count,
    char** out_error);

haruhidb_status_t haruhidb_table_name_at_ex(
    haruhidb_database_t* db,
    size_t table_index,
    char** out_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_name_at(
    haruhidb_database_t* db,
    size_t table_index,
    char** out_name,
    char** out_error);

haruhidb_status_t haruhidb_table_column_count_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_column_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_column_count(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_column_count,
    char** out_error);

haruhidb_status_t haruhidb_table_column_at_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t column_index,
    haruhidb_column_def_t* out_column,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_column_at(
    haruhidb_database_t* db,
    const char* table_name,
    size_t column_index,
    haruhidb_column_def_t* out_column,
    char** out_error);

haruhidb_status_t haruhidb_table_index_count_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_index_count,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_index_count(
    haruhidb_database_t* db,
    const char* table_name,
    size_t* out_index_count,
    char** out_error);

haruhidb_status_t haruhidb_table_index_name_at_ex(
    haruhidb_database_t* db,
    const char* table_name,
    size_t index_pos,
    char** out_index_name,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_table_index_name_at(
    haruhidb_database_t* db,
    const char* table_name,
    size_t index_pos,
    char** out_index_name,
    char** out_error);

/*
 * 返回语义固定为：
 * - HARUHIDB_STATUS_OK: 拿到一行，此时 row 由调用方用 haruhidb_row_destroy() 释放
 * - HARUHIDB_STATUS_END: 扫描结束，此时 row 会被重置为空，不要求释放
 * - HARUHIDB_STATUS_ERROR: 执行失败，此时 row 会被重置为空，不要求释放
 *
 * 调用方复用同一个 row 变量时，scan_next 会先自动释放上一次 HARUHIDB_STATUS_OK 的内容；
 * 如果不再继续调用 scan_next，也可以显式调用 haruhidb_row_destroy() 提前释放。
 */
haruhidb_status_t haruhidb_scan_next_ex(
    haruhidb_scan_t* scan,
    haruhidb_row_t* row,
    haruhidb_error_code_t* out_error_code,
    char** out_error);

haruhidb_status_t haruhidb_scan_next(
    haruhidb_scan_t* scan,
    haruhidb_row_t* row,
    char** out_error);

void haruhidb_scan_close(haruhidb_scan_t* scan);

void haruhidb_row_destroy(haruhidb_row_t* row);
void haruhidb_free_string(char* text);

#ifdef __cplusplus
} // extern "C"
#endif
