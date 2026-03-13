/**
 * CXX/test/storage/index/b_plus_tree_test.cxx
 */

#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "storage/disk/disk_manager.h"
#include "storage/index/b_plus_tree.h"

#include <filesystem>
#include <string>
#include <vector>

namespace HaruhiDB::storage
{

class BPlusTreeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto filename = std::string("bpt_") + info->test_suite_name() + "_" + info->name() + ".db";
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

TEST_F(BPlusTreeTest, EmptyAndStartNewTree)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    BPlusTree tree(&bpm);

    EXPECT_TRUE(tree.IsEmpty());
    record::RID out;
    EXPECT_FALSE(tree.GetValue(7, &out));

    const record::RID rid(9, 2);
    EXPECT_TRUE(tree.Insert(7, rid));
    EXPECT_FALSE(tree.IsEmpty());
    EXPECT_NE(tree.RootPageId(), INVALID_PAGE_ID);

    ASSERT_TRUE(tree.GetValue(7, &out));
    EXPECT_EQ(out, rid);
}

TEST_F(BPlusTreeTest, RejectDuplicateKey)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(32, &dm);
    BPlusTree tree(&bpm);

    const record::RID rid1(1, 1);
    const record::RID rid2(2, 2);
    ASSERT_TRUE(tree.Insert(100, rid1));
    EXPECT_FALSE(tree.Insert(100, rid2));

    record::RID out;
    ASSERT_TRUE(tree.GetValue(100, &out));
    EXPECT_EQ(out, rid1);
}

TEST_F(BPlusTreeTest, LeafSplitPromotesRootToInternal)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(256, &dm);
    BPlusTree tree(&bpm);

    Page scratch;
    scratch.InitBlank(1, PageType::LEAF);
    BPlusTreeLeafPage probe_leaf(&scratch);
    const uint16_t leaf_max = probe_leaf.ComputeMaxSize();
    ASSERT_GT(leaf_max, 2u);

    for (uint16_t i = 0; i <= leaf_max; ++i) {
        ASSERT_TRUE(tree.Insert(static_cast<int32_t>(i), record::RID(static_cast<page_id_t>(1000 + i), i)));
    }

    const page_id_t root_page_id = tree.RootPageId();
    ASSERT_NE(root_page_id, INVALID_PAGE_ID);

    auto root_page_exp = bpm.FetchPage(root_page_id);
    ASSERT_TRUE(root_page_exp.has_value());
    Page* root_page = root_page_exp.value();
    root_page->RLock();
    BPlusTreePage root_node(root_page);
    EXPECT_EQ(root_node.GetPageType(), PageType::INTERNAL);
    EXPECT_GE(root_node.GetSize(), 1u);
    root_page->RUnLock();
    EXPECT_TRUE(bpm.UnpinPage(root_page_id, false));

    for (uint16_t i = 0; i <= leaf_max; ++i) {
        record::RID out;
        ASSERT_TRUE(tree.GetValue(static_cast<int32_t>(i), &out));
        EXPECT_EQ(out, record::RID(static_cast<page_id_t>(1000 + i), i));
    }
}

TEST_F(BPlusTreeTest, InsertManyAndLookup)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    constexpr int32_t kCount = 3000;
    for (int32_t i = 0; i < kCount; ++i) {
        ASSERT_TRUE(tree.Insert(i, record::RID(static_cast<page_id_t>(20000 + i), static_cast<slot_id_t>(i))));
    }

    for (int32_t i = 0; i < kCount; i += 7) {
        record::RID out;
        ASSERT_TRUE(tree.GetValue(i, &out));
        EXPECT_EQ(out, record::RID(static_cast<page_id_t>(20000 + i), static_cast<slot_id_t>(i)));
    }

    record::RID out;
    EXPECT_FALSE(tree.GetValue(-1, &out));
    EXPECT_FALSE(tree.GetValue(kCount + 1, &out));
}

TEST_F(BPlusTreeTest, RemoveTriggersRebalanceAndKeepsSearchCorrect)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    constexpr int32_t kCount = 2500;
    for (int32_t i = 0; i < kCount; ++i) {
        ASSERT_TRUE(tree.Insert(i, record::RID(static_cast<page_id_t>(30000 + i), static_cast<slot_id_t>(i))));
    }

    for (int32_t i = 0; i < kCount; ++i) {
        if (i % 3 == 0) {
            ASSERT_TRUE(tree.Remove(i));
        }
    }

    record::RID out;
    for (int32_t i = 0; i < kCount; ++i) {
        if (i % 3 == 0) {
            EXPECT_FALSE(tree.GetValue(i, &out));
        } else {
            ASSERT_TRUE(tree.GetValue(i, &out));
            EXPECT_EQ(out, record::RID(static_cast<page_id_t>(30000 + i), static_cast<slot_id_t>(i)));
        }
    }

    EXPECT_FALSE(tree.Remove(-1));
    EXPECT_FALSE(tree.Remove(kCount + 10));
}

TEST_F(BPlusTreeTest, RemoveAllShrinksTreeToEmpty)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    constexpr int32_t kCount = 1800;
    for (int32_t i = 0; i < kCount; ++i) {
        ASSERT_TRUE(tree.Insert(i, record::RID(static_cast<page_id_t>(40000 + i), static_cast<slot_id_t>(i))));
    }

    for (int32_t i = 0; i < kCount; ++i) {
        ASSERT_TRUE(tree.Remove(i));
    }

    EXPECT_TRUE(tree.IsEmpty());
    EXPECT_EQ(tree.RootPageId(), INVALID_PAGE_ID);

    record::RID out;
    EXPECT_FALSE(tree.GetValue(10, &out));
}

TEST_F(BPlusTreeTest, HeaderPagePersistsRootAndRecoversTree)
{
    page_id_t header_page_id = INVALID_PAGE_ID;
    page_id_t root_page_id_before = INVALID_PAGE_ID;

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(512, &dm);
        BPlusTree tree(&bpm);
        header_page_id = tree.HeaderPageId();
        ASSERT_NE(header_page_id, INVALID_PAGE_ID);

        constexpr int32_t kCount = 1500;
        for (int32_t i = 0; i < kCount; ++i) {
            ASSERT_TRUE(tree.Insert(i, record::RID(static_cast<page_id_t>(50000 + i), static_cast<slot_id_t>(i))));
        }
        for (int32_t i = 0; i < kCount; i += 4) {
            ASSERT_TRUE(tree.Remove(i));
        }

        root_page_id_before = tree.RootPageId();
        ASSERT_NE(root_page_id_before, INVALID_PAGE_ID);
        ASSERT_TRUE(bpm.FlushAllPages().has_value());
    }

    {
        DiskManager dm(db_path_);
        buffer::BufferPoolManager bpm(512, &dm);
        BPlusTree recovered(&bpm, header_page_id);

        EXPECT_EQ(recovered.HeaderPageId(), header_page_id);
        EXPECT_EQ(recovered.RootPageId(), root_page_id_before);

        record::RID out;
        EXPECT_FALSE(recovered.GetValue(0, &out));
        ASSERT_TRUE(recovered.GetValue(1, &out));
        EXPECT_EQ(out, record::RID(50001, 1));
        ASSERT_TRUE(recovered.GetValue(1499, &out));
        EXPECT_EQ(out, record::RID(51499, 1499));
    }
}

TEST_F(BPlusTreeTest, RangeScanIteratorReturnsOrderedKeys)
{
    DiskManager dm(db_path_);
    buffer::BufferPoolManager bpm(512, &dm);
    BPlusTree tree(&bpm);

    constexpr int32_t kCount = 1200;
    for (int32_t i = kCount - 1; i >= 0; --i) {
        ASSERT_TRUE(tree.Insert(i, record::RID(static_cast<page_id_t>(60000 + i), static_cast<slot_id_t>(i))));
    }

    std::vector<int32_t> all_keys;
    for (auto it = tree.Begin(); it != tree.End(); ++it) {
        const auto [key, rid] = *it;
        all_keys.push_back(key);
        EXPECT_EQ(rid, record::RID(static_cast<page_id_t>(60000 + key), static_cast<slot_id_t>(key)));
    }
    ASSERT_EQ(all_keys.size(), static_cast<size_t>(kCount));
    for (int32_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(all_keys[static_cast<size_t>(i)], i);
    }

    int32_t expected = 777;
    for (auto it = tree.Begin(expected); it != tree.End(); ++it) {
        const auto [key, rid] = *it;
        EXPECT_EQ(key, expected);
        EXPECT_EQ(rid, record::RID(static_cast<page_id_t>(60000 + expected), static_cast<slot_id_t>(expected)));
        ++expected;
    }
    EXPECT_EQ(expected, kCount);
}

} // namespace HaruhiDB::storage
