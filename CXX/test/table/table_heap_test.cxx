/**
 * CXX/test/table/table_heap_test.cxx
 */

#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/disk/disk_manager.h"
#include "table/table_heap.h"
#include "table/table_iterator.h"

#include <filesystem>
#include <memory>
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

class TableHeapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto filename = std::string("table_heap_") + info->test_suite_name() + "_" + info->name() + ".db";
        db_path_ = std::filesystem::temp_directory_path() / filename;
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::filesystem::path db_path_;
};

TEST_F(TableHeapTest, InsertGetAndUpdateTuple)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(3, &dm);
    TableHeap heap(&bpm);

    record::RID rid;
    ASSERT_TRUE(heap.InsertTuple(MakeTupleFromString("haruhi"), &rid));

    record::Tuple fetched;
    ASSERT_TRUE(heap.GetTuple(rid, &fetched));
    EXPECT_EQ(TupleToString(fetched), "haruhi");

    record::RID updated_rid;
    ASSERT_TRUE(heap.UpdateTuple(rid, MakeTupleFromString("db"), &updated_rid));
    EXPECT_EQ(updated_rid.GetPageId(), rid.GetPageId());
    EXPECT_EQ(updated_rid.GetSlotId(), rid.GetSlotId());

    record::Tuple fetched2;
    ASSERT_TRUE(heap.GetTuple(updated_rid, &fetched2));
    EXPECT_EQ(TupleToString(fetched2), "db");
}

TEST_F(TableHeapTest, IteratorSkipsDeletedTuples)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(3, &dm);
    TableHeap heap(&bpm);

    record::RID r1;
    record::RID r2;
    record::RID r3;
    ASSERT_TRUE(heap.InsertTuple(MakeTupleFromString("a"), &r1));
    ASSERT_TRUE(heap.InsertTuple(MakeTupleFromString("b"), &r2));
    ASSERT_TRUE(heap.InsertTuple(MakeTupleFromString("c"), &r3));
    ASSERT_TRUE(heap.DeleteTuple(r2));

    std::vector<std::string> scanned;
    for (auto it = heap.Begin(); it != heap.End(); ++it) {
        const record::Tuple tuple = *it;
        scanned.push_back(TupleToString(tuple));
    }

    ASSERT_EQ(scanned.size(), 2u);
    EXPECT_EQ(scanned[0], "a");
    EXPECT_EQ(scanned[1], "c");
}

TEST_F(TableHeapTest, ReclaimMiddlePageKeepsChainConsistent)
{
    storage::DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(8, &dm);
    TableHeap heap(&bpm);

    struct Row {
        record::RID rid;
        std::string payload;
    };

    std::vector<Row> rows;
    std::set<page_id_t> pages;
    for (int i = 0; i < 1000 && pages.size() < 3; ++i) {
        const std::string payload = std::string(900, static_cast<char>('a' + (i % 26))) + "#" + std::to_string(i);
        record::RID rid;
        ASSERT_TRUE(heap.InsertTuple(MakeTupleFromString(payload), &rid));
        rows.push_back(Row{rid, payload});
        pages.insert(rid.GetPageId());
    }
    ASSERT_GE(pages.size(), 3u);

    auto it_page = pages.begin();
    ++it_page;
    const page_id_t victim_page = *it_page;

    size_t deleted_rows = 0;
    std::set<std::string> expected_payloads;
    for (const auto &row : rows) {
        if (row.rid.GetPageId() == victim_page) {
            ASSERT_TRUE(heap.DeleteTuple(row.rid));
            deleted_rows++;
        } else {
            expected_payloads.insert(row.payload);
        }
    }
    ASSERT_GT(deleted_rows, 0u);

    std::set<std::string> scanned_payloads;
    for (auto it = heap.Begin(); it != heap.End(); ++it) {
        scanned_payloads.insert(TupleToString(*it));
    }

    EXPECT_EQ(scanned_payloads, expected_payloads);
}

} // namespace HaruhiDB::table
