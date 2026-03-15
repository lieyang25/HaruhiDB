#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/table_page.h"
#include "storage/wal/wal_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace HaruhiDB::storage::wal
{

namespace
{
void WriteMarker(page_data_t* image, size_t offset, std::string_view marker)
{
    ASSERT_NE(image, nullptr);
    ASSERT_LE(offset + marker.size(), PAGE_SIZE);
    for (size_t i = 0; i < marker.size(); ++i) {
        (*image)[offset + i] = static_cast<std::byte>(marker[i]);
    }
}

page_data_t SnapshotPage(buffer::BufferPoolManager* bpm, page_id_t page_id)
{
    page_data_t snapshot{};
    auto page_exp = bpm->FetchPage(page_id);
    EXPECT_TRUE(page_exp.has_value());
    if (!page_exp.has_value()) {
        return snapshot;
    }

    Page* page = page_exp.value();
    page->RLock();
    snapshot = page->Data();
    page->RUnLock();
    EXPECT_TRUE(bpm->UnpinPage(page_id, false));
    return snapshot;
}

page_id_t CreateHeapPage(buffer::BufferPoolManager* bpm)
{
    page_id_t page_id = INVALID_PAGE_ID;
    auto page_exp = bpm->NewPage(&page_id, PageType::HEAP);
    EXPECT_TRUE(page_exp.has_value());
    if (!page_exp.has_value()) {
        return INVALID_PAGE_ID;
    }

    Page* page = page_exp.value();
    TablePage table_page(page);
    table_page.InitForNewPage(page_id);
    EXPECT_TRUE(bpm->UnpinPage(page_id, true));
    EXPECT_TRUE(bpm->FlushAllPages().has_value());
    return page_id;
}
} // namespace

class WalManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto stem = std::string("wal_mgr_") + info->test_suite_name() + "_" + info->name();
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

    std::filesystem::path db_path_;
    std::filesystem::path wal_path_;
};

TEST_F(WalManagerTest, AppendFlushAndRecoverRedoAppliesAfterImage)
{
    page_id_t page_id = INVALID_PAGE_ID;
    page_data_t expected{};

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        page_id = CreateHeapPage(&bpm);
        ASSERT_NE(page_id, INVALID_PAGE_ID);

        expected = SnapshotPage(&bpm, page_id);
        WriteMarker(&expected, 192, "redo-put");

        WalManager wal(wal_path_);
        LogRecord record{};
        record.type = LogRecordType::PUT;
        record.page_id = page_id;
        record.payload_len = WAL_PAYLOAD_LEN;
        record.after_image = expected;
        ASSERT_TRUE(wal.AppendLog(record));
        ASSERT_TRUE(wal.FlushLog());
    }

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        const page_data_t actual = SnapshotPage(&bpm, page_id);
        EXPECT_EQ(actual, expected);
    }
}

TEST_F(WalManagerTest, RecoverIgnoresTailPartialRecord)
{
    page_id_t page_id = INVALID_PAGE_ID;
    page_data_t expected{};

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        page_id = CreateHeapPage(&bpm);
        ASSERT_NE(page_id, INVALID_PAGE_ID);

        expected = SnapshotPage(&bpm, page_id);
        WriteMarker(&expected, 256, "redo-complete");

        WalManager wal(wal_path_);
        LogRecord record{};
        record.type = LogRecordType::PUT;
        record.page_id = page_id;
        record.payload_len = WAL_PAYLOAD_LEN;
        record.after_image = expected;
        ASSERT_TRUE(wal.AppendLog(record));
        ASSERT_TRUE(wal.FlushLog());
    }

    {
        std::ofstream append_tail(wal_path_, std::ios::binary | std::ios::app);
        ASSERT_TRUE(append_tail.is_open());
        const char tail_bytes[] = {'b', 'a', 'd', '!'};
        append_tail.write(tail_bytes, static_cast<std::streamsize>(sizeof(tail_bytes)));
        ASSERT_TRUE(static_cast<bool>(append_tail));
    }

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        const page_data_t actual = SnapshotPage(&bpm, page_id);
        EXPECT_EQ(actual, expected);
    }
}

TEST_F(WalManagerTest, RecoverReplaysInOrderAndSupportsDeleteType)
{
    page_id_t page_id = INVALID_PAGE_ID;
    page_data_t second_image{};

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        page_id = CreateHeapPage(&bpm);
        ASSERT_NE(page_id, INVALID_PAGE_ID);

        page_data_t first_image = SnapshotPage(&bpm, page_id);
        second_image = first_image;
        WriteMarker(&first_image, 64, "first");
        WriteMarker(&second_image, 64, "second");

        WalManager wal(wal_path_);
        LogRecord put_record{};
        put_record.type = LogRecordType::PUT;
        put_record.page_id = page_id;
        put_record.payload_len = WAL_PAYLOAD_LEN;
        put_record.after_image = first_image;

        LogRecord delete_record{};
        delete_record.type = LogRecordType::DELETE;
        delete_record.page_id = page_id;
        delete_record.payload_len = WAL_PAYLOAD_LEN;
        delete_record.after_image = second_image;

        ASSERT_TRUE(wal.AppendLog(put_record));
        ASSERT_TRUE(wal.AppendLog(delete_record));
        ASSERT_TRUE(wal.FlushLog());
    }

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        const page_data_t actual = SnapshotPage(&bpm, page_id);
        EXPECT_EQ(actual, second_image);
    }
}

TEST_F(WalManagerTest, RecoverTruncatesWalFileOnSuccess)
{
    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        const page_id_t page_id = CreateHeapPage(&bpm);
        ASSERT_NE(page_id, INVALID_PAGE_ID);

        page_data_t image = SnapshotPage(&bpm, page_id);
        WriteMarker(&image, 160, "truncate");

        WalManager wal(wal_path_);
        LogRecord record{};
        record.type = LogRecordType::PUT;
        record.page_id = page_id;
        record.payload_len = WAL_PAYLOAD_LEN;
        record.after_image = image;
        ASSERT_TRUE(wal.AppendLog(record));
        ASSERT_TRUE(wal.FlushLog());
    }

    ASSERT_TRUE(std::filesystem::exists(wal_path_));
    ASSERT_GT(std::filesystem::file_size(wal_path_), 0U);

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));
    }

    ASSERT_TRUE(std::filesystem::exists(wal_path_));
    EXPECT_EQ(std::filesystem::file_size(wal_path_), 0U);
}

} // namespace HaruhiDB::storage::wal
