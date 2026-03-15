#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/disk/disk_manager.h"
#include "storage/wal/wal_manager.h"
#include "table/table_heap.h"
#include "table/table_iterator.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace HaruhiDB::table
{

namespace
{
record::Tuple MakeTupleFromString(const std::string& text)
{
    std::vector<std::byte> bytes(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(text[i]);
    }
    return record::Tuple(std::move(bytes));
}

std::string TupleToString(const record::Tuple& tuple)
{
    const auto* begin = reinterpret_cast<const char*>(tuple.Data());
    return std::string(begin, begin + tuple.Size());
}
} // namespace

class TableHeapWalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto stem = std::string("table_heap_wal_") + info->test_suite_name() + "_" + info->name();
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

TEST_F(TableHeapWalTest, InsertThenRecover)
{
    page_id_t first_page_id = INVALID_PAGE_ID;
    record::RID rid;

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        auto heap_exp = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_exp.has_value());
        auto heap = std::move(heap_exp.value());
        first_page_id = heap->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);

        storage::wal::WalManager wal(wal_path_);
        heap->SetWalManager(&wal);
        ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString("wal_insert"), &rid));
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        TableHeap heap(&bpm, first_page_id);
        record::Tuple out;
        ASSERT_TRUE(heap.GetTuple(rid, &out));
        EXPECT_EQ(TupleToString(out), "wal_insert");
    }
}

TEST_F(TableHeapWalTest, DeleteThenRecover)
{
    page_id_t first_page_id = INVALID_PAGE_ID;
    record::RID rid;

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        auto heap_exp = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_exp.has_value());
        auto heap = std::move(heap_exp.value());
        first_page_id = heap->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);

        storage::wal::WalManager wal(wal_path_);
        heap->SetWalManager(&wal);
        ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString("to_delete"), &rid));
        ASSERT_TRUE(heap->DeleteTuple(rid));
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        TableHeap heap(&bpm, first_page_id);
        record::Tuple out;
        EXPECT_FALSE(heap.GetTuple(rid, &out));
    }
}

TEST_F(TableHeapWalTest, UpdateInPlaceThenRecover)
{
    page_id_t first_page_id = INVALID_PAGE_ID;
    record::RID rid;

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        auto heap_exp = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_exp.has_value());
        auto heap = std::move(heap_exp.value());
        first_page_id = heap->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);

        storage::wal::WalManager wal(wal_path_);
        heap->SetWalManager(&wal);
        ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString("abc"), &rid));

        record::RID updated_rid;
        ASSERT_TRUE(heap->UpdateTuple(rid, MakeTupleFromString("xy"), &updated_rid));
        EXPECT_EQ(updated_rid, rid);
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(16, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        TableHeap heap(&bpm, first_page_id);
        record::Tuple out;
        ASSERT_TRUE(heap.GetTuple(rid, &out));
        EXPECT_EQ(TupleToString(out), "xy");
    }
}

TEST_F(TableHeapWalTest, UpdateMoveThenRecover)
{
    page_id_t first_page_id = INVALID_PAGE_ID;
    record::RID old_rid;
    record::RID new_rid;
    const std::string moved_value = std::string(1800, 'z');

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        auto heap_exp = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_exp.has_value());
        auto heap = std::move(heap_exp.value());
        first_page_id = heap->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);

        storage::wal::WalManager wal(wal_path_);
        heap->SetWalManager(&wal);
        ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString("move_source"), &old_rid));
        for (int i = 0; i < 200; ++i) {
            const std::string payload = std::string(900, static_cast<char>('a' + (i % 26))) + "#" + std::to_string(i);
            record::RID rid;
            ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString(payload), &rid));
        }

        ASSERT_TRUE(heap->UpdateTuple(old_rid, MakeTupleFromString(moved_value), &new_rid));
        ASSERT_NE(new_rid, old_rid);
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        TableHeap heap(&bpm, first_page_id);
        record::Tuple old_tuple;
        record::Tuple new_tuple;
        EXPECT_FALSE(heap.GetTuple(old_rid, &old_tuple));
        ASSERT_TRUE(heap.GetTuple(new_rid, &new_tuple));
        EXPECT_EQ(TupleToString(new_tuple), moved_value);
    }
}

TEST_F(TableHeapWalTest, NewPageLinkingSurvivesRecover)
{
    page_id_t first_page_id = INVALID_PAGE_ID;
    std::set<std::string> expected_payloads;

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        auto heap_exp = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_exp.has_value());
        auto heap = std::move(heap_exp.value());
        first_page_id = heap->FirstPageId();
        ASSERT_NE(first_page_id, INVALID_PAGE_ID);

        storage::wal::WalManager wal(wal_path_);
        heap->SetWalManager(&wal);
        for (int i = 0; i < 320; ++i) {
            const std::string payload = std::string(850, static_cast<char>('a' + (i % 26))) + "#" + std::to_string(i);
            record::RID rid;
            ASSERT_TRUE(heap->InsertTuple(MakeTupleFromString(payload), &rid));
            expected_payloads.insert(payload);
        }
    }

    {
        storage::DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(32, &dm);
        storage::wal::WalManager wal(wal_path_);
        ASSERT_TRUE(wal.Recover(&bpm));

        TableHeap heap(&bpm, first_page_id);
        std::set<std::string> scanned_payloads;
        for (auto it = heap.Begin(); it != heap.End(); ++it) {
            scanned_payloads.insert(TupleToString(*it));
        }

        EXPECT_EQ(scanned_payloads, expected_payloads);
    }
}

} // namespace HaruhiDB::table
