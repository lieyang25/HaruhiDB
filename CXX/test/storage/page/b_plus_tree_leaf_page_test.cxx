/**
 * CXX/test/storage/page/b_plus_tree_leaf_page_test.cxx
 */

#include "gtest/gtest.h"

#include "storage/page/b_plus_tree_leaf_page.h"

namespace HaruhiDB::storage
{
TEST(BPlusTreeLeafPageTest, InitClampsOversizedMaxSize)
{
    Page page;
    page.InitBlank(17, PageType::LEAF);
    BPlusTreeLeafPage leaf(&page);
    const uint16_t physical_max = leaf.ComputeMaxSize();
    ASSERT_GT(physical_max, 0u);

    ASSERT_TRUE(
        leaf.InitForNewLeaf(
            static_cast<uint16_t>(physical_max + 100),
            9));
    EXPECT_EQ(leaf.GetPageId(), 17u);
    EXPECT_EQ(leaf.GetPageType(), PageType::LEAF);
    EXPECT_EQ(leaf.GetParentPageId(), 9u);
    EXPECT_EQ(leaf.GetSize(), 0u);
    EXPECT_EQ(leaf.GetMaxSize(), physical_max);
    EXPECT_EQ(leaf.GetNextPageId(), INVALID_PAGE_ID);
}

TEST(BPlusTreeLeafPageTest, InsertLookupAndOrder)
{
    Page page;
    page.InitBlank(1, PageType::LEAF);
    BPlusTreeLeafPage leaf(&page);
    ASSERT_TRUE(leaf.InitForNewLeaf(8));

    ASSERT_TRUE(leaf.Insert(30, record::RID(1, 30)));
    ASSERT_TRUE(leaf.Insert(10, record::RID(1, 10)));
    ASSERT_TRUE(leaf.Insert(20, record::RID(1, 20)));
    EXPECT_FALSE(leaf.Insert(20, record::RID(1, 99)));
    EXPECT_EQ(leaf.GetSize(), 3u);

    EXPECT_EQ(leaf.KeyAt(0), 10);
    EXPECT_EQ(leaf.KeyAt(1), 20);
    EXPECT_EQ(leaf.KeyAt(2), 30);

    record::RID out;
    ASSERT_TRUE(leaf.Lookup(20, &out));
    EXPECT_EQ(out, record::RID(1, 20));

    EXPECT_FALSE(leaf.Lookup(25, &out));
    EXPECT_FALSE(leaf.Lookup(30, nullptr));
}

TEST(BPlusTreeLeafPageTest, InsertRespectsMaxSize)
{
    Page page;
    page.InitBlank(2, PageType::LEAF);
    BPlusTreeLeafPage leaf(&page);
    ASSERT_TRUE(leaf.InitForNewLeaf(3));

    EXPECT_TRUE(leaf.Insert(1, record::RID(2, 1)));
    EXPECT_TRUE(leaf.Insert(2, record::RID(2, 2)));
    EXPECT_TRUE(leaf.Insert(3, record::RID(2, 3)));
    EXPECT_FALSE(leaf.Insert(4, record::RID(2, 4)));
    EXPECT_EQ(leaf.GetSize(), 3u);
}

TEST(BPlusTreeLeafPageTest, RemoveDeletesExistingAndKeepsOrder)
{
    Page page;
    page.InitBlank(3, PageType::LEAF);
    BPlusTreeLeafPage leaf(&page);
    ASSERT_TRUE(leaf.InitForNewLeaf(8));

    ASSERT_TRUE(leaf.Insert(10, record::RID(1, 10)));
    ASSERT_TRUE(leaf.Insert(20, record::RID(1, 20)));
    ASSERT_TRUE(leaf.Insert(30, record::RID(1, 30)));

    EXPECT_FALSE(leaf.Remove(99));
    ASSERT_TRUE(leaf.Remove(20));
    ASSERT_EQ(leaf.GetSize(), 2u);
    EXPECT_EQ(leaf.KeyAt(0), 10);
    EXPECT_EQ(leaf.KeyAt(1), 30);
}

TEST(BPlusTreeLeafPageTest, MoveHalfToSplitsAndMaintainsLeafLinks)
{
    Page src_page;
    Page dst_page;
    src_page.InitBlank(100, PageType::LEAF);
    dst_page.InitBlank(200, PageType::LEAF);
    BPlusTreeLeafPage src(&src_page);
    BPlusTreeLeafPage dst(&dst_page);

    ASSERT_TRUE(src.InitForNewLeaf(8, 7));
    ASSERT_TRUE(dst.InitForNewLeaf(8, 7));
    src.SetNextPageId(777);

    ASSERT_TRUE(src.Insert(10, record::RID(10, 1)));
    ASSERT_TRUE(src.Insert(20, record::RID(20, 1)));
    ASSERT_TRUE(src.Insert(30, record::RID(30, 1)));
    ASSERT_TRUE(src.Insert(40, record::RID(40, 1)));
    ASSERT_TRUE(src.Insert(50, record::RID(50, 1)));

    src.MoveHalfTo(&dst);

    EXPECT_EQ(src.GetSize(), 2u);
    EXPECT_EQ(dst.GetSize(), 3u);
    EXPECT_EQ(src.KeyAt(0), 10);
    EXPECT_EQ(src.KeyAt(1), 20);
    EXPECT_EQ(dst.KeyAt(0), 30);
    EXPECT_EQ(dst.KeyAt(1), 40);
    EXPECT_EQ(dst.KeyAt(2), 50);
    EXPECT_EQ(src.GetNextPageId(), 200u);
    EXPECT_EQ(dst.GetNextPageId(), 777u);
}

TEST(BPlusTreeLeafPageTest, MoveHalfToRejectsNonEmptyRecipient)
{
    Page src_page;
    Page dst_page;
    src_page.InitBlank(300, PageType::LEAF);
    dst_page.InitBlank(400, PageType::LEAF);
    BPlusTreeLeafPage src(&src_page);
    BPlusTreeLeafPage dst(&dst_page);

    ASSERT_TRUE(src.InitForNewLeaf(8));
    ASSERT_TRUE(dst.InitForNewLeaf(8));
    src.SetNextPageId(999);

    ASSERT_TRUE(src.Insert(1, record::RID(1, 1)));
    ASSERT_TRUE(src.Insert(2, record::RID(1, 2)));
    ASSERT_TRUE(src.Insert(3, record::RID(1, 3)));
    ASSERT_TRUE(src.Insert(4, record::RID(1, 4)));
    ASSERT_TRUE(dst.Insert(100, record::RID(2, 1)));

    src.MoveHalfTo(&dst);

    EXPECT_EQ(src.GetSize(), 4u);
    EXPECT_EQ(dst.GetSize(), 1u);
    EXPECT_EQ(src.GetNextPageId(), 999u);
    EXPECT_EQ(dst.KeyAt(0), 100);
}

TEST(BPlusTreeLeafPageTest, BorrowHelpersMoveBoundaryItems)
{
    Page left_page;
    Page right_page;
    left_page.InitBlank(700, PageType::LEAF);
    right_page.InitBlank(701, PageType::LEAF);
    BPlusTreeLeafPage left(&left_page);
    BPlusTreeLeafPage right(&right_page);

    ASSERT_TRUE(left.InitForNewLeaf(8));
    ASSERT_TRUE(right.InitForNewLeaf(8));

    ASSERT_TRUE(left.Insert(10, record::RID(1, 10)));
    ASSERT_TRUE(left.Insert(20, record::RID(1, 20)));
    ASSERT_TRUE(left.Insert(30, record::RID(1, 30)));
    ASSERT_TRUE(right.Insert(40, record::RID(1, 40)));
    ASSERT_TRUE(right.Insert(50, record::RID(1, 50)));

    ASSERT_TRUE(left.MoveLastToFrontOf(&right));
    EXPECT_EQ(left.GetSize(), 2u);
    EXPECT_EQ(right.GetSize(), 3u);
    EXPECT_EQ(right.KeyAt(0), 30);

    ASSERT_TRUE(right.MoveFirstToEndOf(&left));
    EXPECT_EQ(left.GetSize(), 3u);
    EXPECT_EQ(right.GetSize(), 2u);
    EXPECT_EQ(left.KeyAt(2), 30);
}

TEST(BPlusTreeLeafPageTest, MoveAllToMergesAndUpdatesNextPointer)
{
    Page left_page;
    Page right_page;
    left_page.InitBlank(800, PageType::LEAF);
    right_page.InitBlank(801, PageType::LEAF);
    BPlusTreeLeafPage left(&left_page);
    BPlusTreeLeafPage right(&right_page);

    ASSERT_TRUE(left.InitForNewLeaf(8));
    ASSERT_TRUE(right.InitForNewLeaf(8));
    left.SetNextPageId(801);
    right.SetNextPageId(999);

    ASSERT_TRUE(left.Insert(1, record::RID(1, 1)));
    ASSERT_TRUE(left.Insert(2, record::RID(1, 2)));
    ASSERT_TRUE(right.Insert(3, record::RID(1, 3)));
    ASSERT_TRUE(right.Insert(4, record::RID(1, 4)));

    right.MoveAllTo(&left);

    EXPECT_EQ(left.GetSize(), 4u);
    EXPECT_EQ(left.KeyAt(0), 1);
    EXPECT_EQ(left.KeyAt(1), 2);
    EXPECT_EQ(left.KeyAt(2), 3);
    EXPECT_EQ(left.KeyAt(3), 4);
    EXPECT_EQ(left.GetNextPageId(), 999u);
    EXPECT_EQ(right.GetSize(), 0u);
}

TEST(BPlusTreeLeafPageTest, InitFailsWhenPageIdInvalid)
{
    Page page;
    BPlusTreeLeafPage leaf(&page);
    EXPECT_FALSE(leaf.InitForNewLeaf(8));
}

} // namespace HaruhiDB::storage
