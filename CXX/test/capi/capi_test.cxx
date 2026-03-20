#include "gtest/gtest.h"

#include "capi/haruhidb.h"

#include <filesystem>
#include <string>
#include <vector>

namespace
{

std::string TakeError(char** error)
{
    if (error == nullptr || *error == nullptr) {
        return {};
    }

    std::string out(*error);
    haruhidb_free_string(*error);
    *error = nullptr;
    return out;
}

haruhidb_column_def_t MakeIntColumn(const char* name)
{
    return haruhidb_column_def_t{
        .name = name,
        .type = HARUHIDB_TYPE_INTEGER,
        .length = 0,
        .nullable = false,
    };
}

haruhidb_column_def_t MakeVarcharColumn(const char* name, uint32_t length)
{
    return haruhidb_column_def_t{
        .name = name,
        .type = HARUHIDB_TYPE_VARCHAR,
        .length = length,
        .nullable = false,
    };
}

haruhidb_value_t MakeInt32Value(int32_t value)
{
    haruhidb_value_t out{};
    out.type = HARUHIDB_TYPE_INTEGER;
    out.int32_v = value;
    return out;
}

haruhidb_value_t MakeVarcharValue(const char* value)
{
    haruhidb_value_t out{};
    out.type = HARUHIDB_TYPE_VARCHAR;
    out.string_data = value;
    out.string_len = value == nullptr ? 0U : std::char_traits<char>::length(value);
    return out;
}

class CApiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto stem = std::string("capi_") + info->test_suite_name() + "_" + info->name();
        db_path_ = std::filesystem::temp_directory_path() / (stem + ".db");
        wal_path_ = std::filesystem::temp_directory_path() / (stem + ".wal");

        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(wal_path_, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
        std::filesystem::remove(wal_path_, ec);
    }

    haruhidb_database_t* Open()
    {
        haruhidb_open_options_t options{};
        options.buffer_pool_size = 64;
        options.lru_k = 2;
        options.enable_wal = true;
        options.wal_path = wal_path_.c_str();

        haruhidb_database_t* db = nullptr;
        char* error = nullptr;
        const auto status = haruhidb_open(db_path_.c_str(), &options, &db, &error);
        EXPECT_EQ(status, HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(error, nullptr);
        EXPECT_NE(db, nullptr);
        return db;
    }

    std::filesystem::path db_path_;
    std::filesystem::path wal_path_;
};

TEST_F(CApiTest, OpenCreateInsertScanAndRecover)
{
    {
        haruhidb_database_t* db = Open();
        char* error = nullptr;

        const haruhidb_column_def_t columns[] = {
            MakeIntColumn("id"),
            MakeVarcharColumn("name", 32),
        };
        EXPECT_EQ(
            haruhidb_table_create(db, "student", columns, 2, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(error, nullptr);

        bool exists = false;
        EXPECT_EQ(
            haruhidb_table_exists(db, "student", &exists, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_TRUE(exists);

        EXPECT_EQ(
            haruhidb_index_create_primary_int(db, "student", "idx_student_id", &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(error, nullptr);

        const haruhidb_value_t row1[] = {
            MakeInt32Value(1),
            MakeVarcharValue("haruhi"),
        };
        const haruhidb_value_t row2[] = {
            MakeInt32Value(2),
            MakeVarcharValue("mio"),
        };

        EXPECT_EQ(
            haruhidb_row_insert(db, "student", row1, 2, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(
            haruhidb_row_insert(db, "student", row2, 2, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);

        haruhidb_scan_t* scan = nullptr;
        EXPECT_EQ(
            haruhidb_scan_open_all(db, "student", &scan, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_NE(scan, nullptr);

        std::vector<int32_t> ids;
        std::vector<std::string> names;

        while (true) {
            haruhidb_row_t row{};
            const auto status = haruhidb_scan_next(scan, &row, &error);
            if (status == HARUHIDB_STATUS_END) {
                EXPECT_EQ(row.values, nullptr);
                EXPECT_EQ(row.value_count, 0U);
                break;
            }
            ASSERT_EQ(status, HARUHIDB_STATUS_OK) << TakeError(&error);
            ASSERT_EQ(row.value_count, 2U);
            ASSERT_NE(row.values, nullptr);

            ids.push_back(row.values[0].int32_v);
            names.emplace_back(row.values[1].string_data, row.values[1].string_len);
            haruhidb_row_destroy(&row);
        }

        EXPECT_EQ(ids, (std::vector<int32_t>{1, 2}));
        EXPECT_EQ(names, (std::vector<std::string>{"haruhi", "mio"}));

        haruhidb_scan_close(scan);
        haruhidb_close(db);
    }

    {
        haruhidb_database_t* db = Open();
        char* error = nullptr;
        haruhidb_scan_t* scan = nullptr;

        EXPECT_EQ(
            haruhidb_scan_open_all(db, "student", &scan, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_NE(scan, nullptr);

        haruhidb_row_t row{};
        ASSERT_EQ(
            haruhidb_scan_next(scan, &row, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_EQ(row.value_count, 2U);
        EXPECT_EQ(row.values[0].int32_v, 1);
        EXPECT_EQ(std::string(row.values[1].string_data, row.values[1].string_len), "haruhi");
        haruhidb_row_destroy(&row);

        haruhidb_scan_close(scan);
        haruhidb_close(db);
    }
}

TEST_F(CApiTest, InsertTypeMismatchReturnsError)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t invalid_row[] = {
        MakeVarcharValue("oops"),
        MakeVarcharValue("haruhi"),
    };

    EXPECT_EQ(
        haruhidb_row_insert(db, "student", invalid_row, 2, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("value cannot cast"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, ScanNextUsesFixedStatusAndDoesNotRequireDestroyOnEndOrError)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 1, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row_value[] = {MakeInt32Value(7)};
    ASSERT_EQ(
        haruhidb_row_insert(db, "student", row_value, 1, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    haruhidb_scan_t* scan = nullptr;
    ASSERT_EQ(
        haruhidb_scan_open_all(db, "student", &scan, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    haruhidb_row_t row{};
    ASSERT_EQ(
        haruhidb_scan_next(scan, &row, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    haruhidb_row_destroy(&row);

    EXPECT_EQ(
        haruhidb_scan_next(scan, &row, &error),
        HARUHIDB_STATUS_END);
    EXPECT_EQ(error, nullptr);
    EXPECT_EQ(row.values, nullptr);
    EXPECT_EQ(row.value_count, 0U);

    EXPECT_EQ(
        haruhidb_scan_next(nullptr, &row, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("invalid or already closed"), std::string::npos);
    EXPECT_EQ(row.values, nullptr);
    EXPECT_EQ(row.value_count, 0U);

    haruhidb_scan_close(scan);
    haruhidb_close(db);
}

TEST_F(CApiTest, CreateTableRejectsNullableColumnsInFirstVersion)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        haruhidb_column_def_t{
            .name = "id",
            .type = HARUHIDB_TYPE_INTEGER,
            .length = 0,
            .nullable = true,
        },
    };

    EXPECT_EQ(
        haruhidb_table_create(db, "student", columns, 1, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("nullable columns"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, UpdateAndDeleteByPrimaryIntFallbackWithoutIndex)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row1[] = {
        MakeInt32Value(1),
        MakeVarcharValue("a"),
    };
    const haruhidb_value_t row2[] = {
        MakeInt32Value(2),
        MakeVarcharValue("b"),
    };
    ASSERT_EQ(haruhidb_row_insert(db, "student", row1, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(haruhidb_row_insert(db, "student", row2, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);

    size_t updated_count = 0;
    const haruhidb_value_t updated_row[] = {
        MakeInt32Value(1),
        MakeVarcharValue("updated"),
    };
    EXPECT_EQ(
        haruhidb_row_update_primary_int(
            db,
            "student",
            1,
            updated_row,
            2,
            &updated_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(updated_count, 1u);

    size_t updated_missing_count = 0;
    const haruhidb_value_t missing_row[] = {
        MakeInt32Value(9),
        MakeVarcharValue("missing"),
    };
    EXPECT_EQ(
        haruhidb_row_update_primary_int(
            db,
            "student",
            9,
            missing_row,
            2,
            &updated_missing_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(updated_missing_count, 0u);

    size_t deleted_count = 0;
    EXPECT_EQ(
        haruhidb_row_delete_primary_int(
            db,
            "student",
            2,
            &deleted_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(deleted_count, 1u);

    size_t deleted_missing_count = 0;
    EXPECT_EQ(
        haruhidb_row_delete_primary_int(
            db,
            "student",
            2,
            &deleted_missing_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(deleted_missing_count, 0u);

    haruhidb_scan_t* scan = nullptr;
    ASSERT_EQ(
        haruhidb_scan_open_all(db, "student", &scan, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_NE(scan, nullptr);

    haruhidb_row_t row{};
    ASSERT_EQ(haruhidb_scan_next(scan, &row, &error), HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(row.value_count, 2u);
    EXPECT_EQ(row.values[0].int32_v, 1);
    EXPECT_EQ(std::string(row.values[1].string_data, row.values[1].string_len), "updated");
    haruhidb_row_destroy(&row);
    EXPECT_EQ(haruhidb_scan_next(scan, &row, &error), HARUHIDB_STATUS_END);

    haruhidb_scan_close(scan);
    haruhidb_close(db);
}

TEST_F(CApiTest, UpdateAndDeleteByPrimaryIntUseIndexPathWhenAvailable)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(
        haruhidb_index_create_primary_int(db, "student", "idx_student_id", &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row1[] = {
        MakeInt32Value(1),
        MakeVarcharValue("a"),
    };
    const haruhidb_value_t row2[] = {
        MakeInt32Value(2),
        MakeVarcharValue("b"),
    };
    ASSERT_EQ(haruhidb_row_insert(db, "student", row1, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(haruhidb_row_insert(db, "student", row2, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);

    size_t updated_count = 0;
    const haruhidb_value_t updated_row[] = {
        MakeInt32Value(1),
        MakeVarcharValue("updated"),
    };
    EXPECT_EQ(
        haruhidb_row_update_primary_int(
            db,
            "student",
            1,
            updated_row,
            2,
            &updated_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(updated_count, 1u);

    size_t deleted_count = 0;
    EXPECT_EQ(
        haruhidb_row_delete_primary_int(
            db,
            "student",
            2,
            &deleted_count,
            &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(deleted_count, 1u);

    bool found = false;
    haruhidb_row_t row{};
    EXPECT_EQ(
        haruhidb_row_get_primary_int(db, "student", 1, &found, &row, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_TRUE(found);
    ASSERT_EQ(row.value_count, 2u);
    EXPECT_EQ(row.values[0].int32_v, 1);
    EXPECT_EQ(std::string(row.values[1].string_data, row.values[1].string_len), "updated");
    haruhidb_row_destroy(&row);

    haruhidb_close(db);
}

TEST_F(CApiTest, GetByPrimaryIntAndRangeScan)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(
        haruhidb_index_create_primary_int(db, "student", "idx_student_id", &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row1[] = {MakeInt32Value(1), MakeVarcharValue("a")};
    const haruhidb_value_t row2[] = {MakeInt32Value(2), MakeVarcharValue("b")};
    const haruhidb_value_t row3[] = {MakeInt32Value(3), MakeVarcharValue("c")};
    ASSERT_EQ(haruhidb_row_insert(db, "student", row1, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(haruhidb_row_insert(db, "student", row2, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(haruhidb_row_insert(db, "student", row3, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);

    bool found = false;
    haruhidb_row_t row{};
    EXPECT_EQ(
        haruhidb_row_get_primary_int(db, "student", 2, &found, &row, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_TRUE(found);
    ASSERT_EQ(row.value_count, 2u);
    EXPECT_EQ(row.values[0].int32_v, 2);
    EXPECT_EQ(std::string(row.values[1].string_data, row.values[1].string_len), "b");
    haruhidb_row_destroy(&row);

    EXPECT_EQ(
        haruhidb_row_get_primary_int(db, "student", 9, &found, &row, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_FALSE(found);
    EXPECT_EQ(row.values, nullptr);
    EXPECT_EQ(row.value_count, 0u);

    haruhidb_scan_t* scan = nullptr;
    EXPECT_EQ(
        haruhidb_scan_open_primary_int_range(db, "student", 2, 3, &scan, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_NE(scan, nullptr);

    std::vector<int32_t> ids;
    while (true) {
        haruhidb_row_t scan_row{};
        const auto status = haruhidb_scan_next(scan, &scan_row, &error);
        if (status == HARUHIDB_STATUS_END) {
            break;
        }
        ASSERT_EQ(status, HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_EQ(scan_row.value_count, 2u);
        ids.push_back(scan_row.values[0].int32_v);
        haruhidb_row_destroy(&scan_row);
    }
    EXPECT_EQ(ids, (std::vector<int32_t>{2, 3}));
    haruhidb_scan_close(scan);

    EXPECT_EQ(
        haruhidb_scan_open_primary_int_range(db, "student", 3, 2, &scan, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("start_key"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, GetByPrimaryIntAndRangeScanRejectTableWithoutIndex)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row[] = {
        MakeInt32Value(1),
        MakeVarcharValue("a"),
    };
    ASSERT_EQ(haruhidb_row_insert(db, "student", row, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);

    bool found = false;
    haruhidb_row_t out_row{};
    EXPECT_EQ(
        haruhidb_row_get_primary_int(db, "student", 1, &found, &out_row, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("index"), std::string::npos);

    haruhidb_scan_t* scan = nullptr;
    EXPECT_EQ(
        haruhidb_scan_open_primary_int_range(db, "student", 1, 3, &scan, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("index"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, UpdateByPrimaryIntRejectsPayloadKeyMismatch)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    const haruhidb_value_t row[] = {
        MakeInt32Value(1),
        MakeVarcharValue("a"),
    };
    ASSERT_EQ(haruhidb_row_insert(db, "student", row, 2, &error), HARUHIDB_STATUS_OK) << TakeError(&error);

    size_t updated_count = 0;
    const haruhidb_value_t bad_payload[] = {
        MakeInt32Value(2),
        MakeVarcharValue("bad"),
    };
    EXPECT_EQ(
        haruhidb_row_update_primary_int(
            db,
            "student",
            1,
            bad_payload,
            2,
            &updated_count,
            &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("must equal key"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, DropIndexAndDropTableFollowStrictSemantics)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 2, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_EQ(
        haruhidb_index_create_primary_int(db, "student", "idx_student_id", &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    EXPECT_EQ(
        haruhidb_index_drop(db, "student", "idx_student_id", &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    EXPECT_EQ(
        haruhidb_index_drop(db, "student", "idx_student_id", &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("index not found"), std::string::npos);

    EXPECT_EQ(
        haruhidb_table_drop(db, "student", &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    bool exists = true;
    ASSERT_EQ(
        haruhidb_table_exists(db, "student", &exists, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_FALSE(exists);

    EXPECT_EQ(
        haruhidb_table_drop(db, "student", &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("table not found"), std::string::npos);

    haruhidb_close(db);
}

TEST_F(CApiTest, ClosedDatabaseHandleIsRejectedAndCloseIsIdempotent)
{
    haruhidb_database_t* db = Open();
    haruhidb_close(db);

    bool exists = false;
    char* error = nullptr;
    EXPECT_EQ(
        haruhidb_table_exists(db, "student", &exists, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("invalid or already closed"), std::string::npos);

    // Second close on the same stale pointer should be a no-op.
    haruhidb_close(db);
}

TEST_F(CApiTest, ClosedScanHandleIsRejectedAndCloseIsIdempotent)
{
    haruhidb_database_t* db = Open();
    char* error = nullptr;

    const haruhidb_column_def_t columns[] = {
        MakeIntColumn("id"),
    };
    ASSERT_EQ(
        haruhidb_table_create(db, "student", columns, 1, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);

    haruhidb_scan_t* scan = nullptr;
    ASSERT_EQ(
        haruhidb_scan_open_all(db, "student", &scan, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    ASSERT_NE(scan, nullptr);

    haruhidb_scan_close(scan);

    haruhidb_row_t row{};
    EXPECT_EQ(
        haruhidb_scan_next(scan, &row, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_NE(TakeError(&error).find("invalid or already closed"), std::string::npos);
    EXPECT_EQ(row.values, nullptr);
    EXPECT_EQ(row.value_count, 0U);

    // Second close on the same stale pointer should be a no-op.
    haruhidb_scan_close(scan);
    haruhidb_close(db);
}

TEST_F(CApiTest, ApiVersionAndCapabilitiesAreStable)
{
    ASSERT_NE(haruhidb_api_version(), nullptr);
    EXPECT_EQ(std::string(haruhidb_api_version()), "1.1.0");

    const auto caps = haruhidb_capabilities();
    EXPECT_NE(caps & HARUHIDB_CAPABILITY_PRIMARY_INT_INDEX, 0ULL);
    EXPECT_NE(caps & HARUHIDB_CAPABILITY_PRIMARY_INT_POINT_GET, 0ULL);
    EXPECT_NE(caps & HARUHIDB_CAPABILITY_PRIMARY_INT_RANGE_SCAN, 0ULL);
    EXPECT_NE(caps & HARUHIDB_CAPABILITY_METADATA_READ, 0ULL);
    EXPECT_NE(caps & HARUHIDB_CAPABILITY_WAL_RUNTIME_OPTION, 0ULL);
}

TEST_F(CApiTest, MetadataInterfacesTrackLifecycleAndRecovery)
{
    {
        haruhidb_database_t* db = Open();
        char* error = nullptr;

        const haruhidb_column_def_t student_columns[] = {
            MakeIntColumn("id"),
            MakeVarcharColumn("name", 32),
        };
        const haruhidb_column_def_t course_columns[] = {
            MakeIntColumn("id"),
        };
        ASSERT_EQ(
            haruhidb_table_create(db, "student", student_columns, 2, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_EQ(
            haruhidb_table_create(db, "course", course_columns, 1, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_EQ(
            haruhidb_index_create_primary_int(db, "student", "idx_student_id", &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);

        size_t table_count = 0;
        ASSERT_EQ(
            haruhidb_table_count(db, &table_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(table_count, 2U);

        char* table_name = nullptr;
        ASSERT_EQ(
            haruhidb_table_name_at(db, 0, &table_name, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(TakeError(&table_name), "student");
        ASSERT_EQ(
            haruhidb_table_name_at(db, 1, &table_name, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(TakeError(&table_name), "course");

        size_t column_count = 0;
        ASSERT_EQ(
            haruhidb_table_column_count(db, "student", &column_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(column_count, 2U);

        haruhidb_column_def_t column{};
        ASSERT_EQ(
            haruhidb_table_column_at(db, "student", 0, &column, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_NE(column.name, nullptr);
        EXPECT_EQ(std::string(column.name), "id");
        haruhidb_free_string(const_cast<char*>(column.name));
        column.name = nullptr;
        EXPECT_EQ(column.type, HARUHIDB_TYPE_INTEGER);
        EXPECT_EQ(column.length, 0U);
        EXPECT_FALSE(column.nullable);

        ASSERT_EQ(
            haruhidb_table_column_at(db, "student", 1, &column, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_NE(column.name, nullptr);
        EXPECT_EQ(std::string(column.name), "name");
        haruhidb_free_string(const_cast<char*>(column.name));
        column.name = nullptr;
        EXPECT_EQ(column.type, HARUHIDB_TYPE_VARCHAR);
        EXPECT_EQ(column.length, 32U);
        EXPECT_FALSE(column.nullable);

        size_t index_count = 0;
        ASSERT_EQ(
            haruhidb_table_index_count(db, "student", &index_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(index_count, 1U);

        char* index_name = nullptr;
        ASSERT_EQ(
            haruhidb_table_index_name_at(db, "student", 0, &index_name, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(TakeError(&index_name), "idx_student_id");

        ASSERT_EQ(
            haruhidb_index_drop(db, "student", "idx_student_id", &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        ASSERT_EQ(
            haruhidb_table_drop(db, "course", &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);

        ASSERT_EQ(
            haruhidb_table_count(db, &table_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(table_count, 1U);

        haruhidb_close(db);
    }

    {
        haruhidb_database_t* db = Open();
        char* error = nullptr;

        size_t table_count = 0;
        ASSERT_EQ(
            haruhidb_table_count(db, &table_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(table_count, 1U);

        char* table_name = nullptr;
        ASSERT_EQ(
            haruhidb_table_name_at(db, 0, &table_name, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(TakeError(&table_name), "student");

        size_t index_count = 0;
        ASSERT_EQ(
            haruhidb_table_index_count(db, "student", &index_count, &error),
            HARUHIDB_STATUS_OK) << TakeError(&error);
        EXPECT_EQ(index_count, 0U);

        haruhidb_close(db);
    }
}

TEST_F(CApiTest, ExInterfacesReturnStableErrorCodes)
{
    {
        haruhidb_database_t* db = nullptr;
        char* error = nullptr;
        haruhidb_error_code_t code = HARUHIDB_ERROR_OK;
        EXPECT_EQ(
            haruhidb_open_ex("/proc/haruhidb_capi_io_failure.db", nullptr, &db, &code, &error),
            HARUHIDB_STATUS_ERROR);
        EXPECT_EQ(code, HARUHIDB_ERROR_IO);
        EXPECT_NE(TakeError(&error).find("open"), std::string::npos);
        EXPECT_EQ(db, nullptr);
    }

    haruhidb_database_t* db = Open();
    char* error = nullptr;
    haruhidb_error_code_t code = HARUHIDB_ERROR_OK;

    bool exists = false;
    EXPECT_EQ(
        haruhidb_table_exists_ex(db, nullptr, &exists, &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_INVALID_ARGUMENT);
    EXPECT_NE(TakeError(&error).find("must not"), std::string::npos);

    const haruhidb_column_def_t student_columns[] = {
        MakeIntColumn("id"),
        MakeVarcharColumn("name", 32),
    };
    ASSERT_EQ(
        haruhidb_table_create_ex(db, "student", student_columns, 2, &code, &error),
        HARUHIDB_STATUS_OK) << TakeError(&error);
    EXPECT_EQ(code, HARUHIDB_ERROR_OK);
    EXPECT_EQ(error, nullptr);

    EXPECT_EQ(
        haruhidb_table_create_ex(db, "student", student_columns, 2, &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_ALREADY_EXISTS);
    EXPECT_NE(TakeError(&error).find("already exists"), std::string::npos);

    EXPECT_EQ(
        haruhidb_table_drop_ex(db, "missing_table", &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_NOT_FOUND);
    EXPECT_NE(TakeError(&error).find("not found"), std::string::npos);

    const haruhidb_column_def_t unsupported_columns[] = {
        haruhidb_column_def_t{
            .name = "nullable_col",
            .type = HARUHIDB_TYPE_INTEGER,
            .length = 0,
            .nullable = true,
        },
    };
    EXPECT_EQ(
        haruhidb_table_create_ex(db, "unsupported_t", unsupported_columns, 1, &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_UNSUPPORTED);
    EXPECT_NE(TakeError(&error).find("not supported"), std::string::npos);

    const haruhidb_value_t bad_insert[] = {
        MakeVarcharValue("bad"),
        MakeVarcharValue("name"),
    };
    EXPECT_EQ(
        haruhidb_row_insert_ex(db, "student", bad_insert, 2, &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_CONSTRAINT);
    EXPECT_NE(TakeError(&error).find("value cannot cast"), std::string::npos);

    haruhidb_close(db);

    EXPECT_EQ(
        haruhidb_table_count_ex(db, nullptr, &code, &error),
        HARUHIDB_STATUS_ERROR);
    EXPECT_EQ(code, HARUHIDB_ERROR_INVALID_HANDLE);
    EXPECT_NE(TakeError(&error).find("invalid or already closed"), std::string::npos);
}

TEST_F(CApiTest, RowDestroyIgnoresForeignBufferPointers)
{
    haruhidb_value_t foreign_values[1]{};
    foreign_values[0].type = HARUHIDB_TYPE_INTEGER;

    haruhidb_row_t row{
        .values = foreign_values,
        .value_count = 1,
    };

    haruhidb_row_destroy(&row);
    EXPECT_EQ(row.values, nullptr);
    EXPECT_EQ(row.value_count, 0U);
}

} // namespace
