/**
 * CXX/test/storage/page/b_plus_tree_internal_page_test.cxx
 */

#include "gtest/gtest.h"

#include "storage/page/b_plus_tree_internal_page.h"

namespace HaruhiDB::storage
{

TEST(BPlusTreeInternalPageTest, InitClampsOversizedMaxSize)
{
    Page page;
    page.InitBlank(31, PageType::INTERNAL);
    BPlusTreeInternalPage internal(&page);
    const uint16_t physical_max = internal.ComputeMaxSize();
    ASSERT_GT(physical_max, 0u);

    ASSERT_TRUE(
        internal.InitForNewInternal(
            static_cast<uint16_t>(physical_max + 32),
            7));
    EXPECT_EQ(internal.GetPageId(), 31u);
    EXPECT_EQ(internal.GetPageType(), PageType::INTERNAL);
    EXPECT_EQ(internal.GetParentPageId(), 7u);
    EXPECT_EQ(internal.GetSize(), 0u);
    EXPECT_EQ(internal.GetMaxSize(), physical_max);
    EXPECT_EQ(internal.GetLeftMostChild(), INVALID_PAGE_ID);
}

TEST(BPlusTreeInternalPageTest, PopulateRootLookupAndInsertAfter)
{
    Page page;
    page.InitBlank(100, PageType::INTERNAL);
    BPlusTreeInternalPage internal(&page);
    ASSERT_TRUE(internal.InitForNewInternal(8));
    ASSERT_TRUE(internal.PopulateNewRoot(10, 20, 20));

    EXPECT_EQ(internal.Lookup(5), 10u);
    EXPECT_EQ(internal.Lookup(20), 20u);

    ASSERT_TRUE(internal.InsertAfter(20, 40, 40));
    ASSERT_TRUE(internal.InsertAfter(10, 10, 15));
    EXPECT_EQ(internal.GetSize(), 3u);
    EXPECT_EQ(internal.KeyAt(0), 10);
    EXPECT_EQ(internal.KeyAt(1), 20);
    EXPECT_EQ(internal.KeyAt(2), 40);
    EXPECT_EQ(internal.ChildAt(0), 15u);
    EXPECT_EQ(internal.ChildAt(1), 20u);
    EXPECT_EQ(internal.ChildAt(2), 40u);

    EXPECT_EQ(internal.Lookup(9), 10u);
    EXPECT_EQ(internal.Lookup(10), 15u);
    EXPECT_EQ(internal.Lookup(19), 15u);
    EXPECT_EQ(internal.Lookup(20), 20u);
    EXPECT_EQ(internal.Lookup(39), 20u);
    EXPECT_EQ(internal.Lookup(40), 40u);
    EXPECT_EQ(internal.Lookup(99), 40u);
}

TEST(BPlusTreeInternalPageTest, InsertAfterRejectsInvalidCases)
{
    Page page;
    page.InitBlank(200, PageType::INTERNAL);
    BPlusTreeInternalPage internal(&page);
    ASSERT_TRUE(internal.InitForNewInternal(2));
    ASSERT_TRUE(internal.PopulateNewRoot(1, 10, 2));

    EXPECT_FALSE(internal.InsertAfter(999, 20, 3));
    EXPECT_FALSE(internal.InsertAfter(2, 20, INVALID_PAGE_ID));
    EXPECT_FALSE(internal.InsertAfter(1, 30, 3));
    EXPECT_TRUE(internal.InsertAfter(2, 20, 3));
    EXPECT_FALSE(internal.InsertAfter(3, 5, 4));
    EXPECT_FALSE(internal.InsertAfter(3, 30, 4));
}

TEST(BPlusTreeInternalPageTest, MoveHalfToSplitsWithCorrectLeftMost)
{
    Page src_page;
    Page dst_page;
    src_page.InitBlank(300, PageType::INTERNAL);
    dst_page.InitBlank(400, PageType::INTERNAL);
    BPlusTreeInternalPage src(&src_page);
    BPlusTreeInternalPage dst(&dst_page);

    ASSERT_TRUE(src.InitForNewInternal(8, 5));
    ASSERT_TRUE(dst.InitForNewInternal(8, 5));
    ASSERT_TRUE(src.PopulateNewRoot(10, 20, 20));
    ASSERT_TRUE(src.InsertAfter(20, 40, 40));
    ASSERT_TRUE(src.InsertAfter(40, 60, 60));
    ASSERT_TRUE(src.InsertAfter(60, 80, 80));
    ASSERT_TRUE(src.InsertAfter(80, 100, 100));

    src.MoveHalfTo(&dst);

    EXPECT_EQ(src.GetSize(), 2u);
    EXPECT_EQ(src.GetLeftMostChild(), 10u);
    EXPECT_EQ(src.KeyAt(0), 20);
    EXPECT_EQ(src.KeyAt(1), 40);
    EXPECT_EQ(src.ChildAt(0), 20u);
    EXPECT_EQ(src.ChildAt(1), 40u);

    EXPECT_EQ(dst.GetSize(), 3u);
    EXPECT_EQ(dst.GetLeftMostChild(), 40u);
    EXPECT_EQ(dst.KeyAt(0), 60);
    EXPECT_EQ(dst.KeyAt(1), 80);
    EXPECT_EQ(dst.KeyAt(2), 100);
    EXPECT_EQ(dst.ChildAt(0), 60u);
    EXPECT_EQ(dst.ChildAt(1), 80u);
    EXPECT_EQ(dst.ChildAt(2), 100u);
}

TEST(BPlusTreeInternalPageTest, MoveHalfToRejectsNonEmptyRecipient)
{
    Page src_page;
    Page dst_page;
    src_page.InitBlank(500, PageType::INTERNAL);
    dst_page.InitBlank(600, PageType::INTERNAL);
    BPlusTreeInternalPage src(&src_page);
    BPlusTreeInternalPage dst(&dst_page);

    ASSERT_TRUE(src.InitForNewInternal(8));
    ASSERT_TRUE(dst.InitForNewInternal(8));
    ASSERT_TRUE(src.PopulateNewRoot(10, 20, 20));
    ASSERT_TRUE(src.InsertAfter(20, 40, 40));
    ASSERT_TRUE(dst.PopulateNewRoot(70, 80, 90));

    src.MoveHalfTo(&dst);

    EXPECT_EQ(src.GetSize(), 2u);
    EXPECT_EQ(dst.GetSize(), 1u);
    EXPECT_EQ(dst.GetLeftMostChild(), 70u);
    EXPECT_EQ(dst.KeyAt(0), 80);
}

TEST(BPlusTreeInternalPageTest, RemoveChildAtMaintainsTreeOrder)
{
    Page page;
    page.InitBlank(700, PageType::INTERNAL);
    BPlusTreeInternalPage internal(&page);
    ASSERT_TRUE(internal.InitForNewInternal(8));
    ASSERT_TRUE(internal.PopulateNewRoot(1, 10, 2));
    ASSERT_TRUE(internal.InsertAfter(2, 20, 3));
    ASSERT_TRUE(internal.InsertAfter(3, 30, 4));

    ASSERT_TRUE(internal.RemoveChildAt(2));
    EXPECT_EQ(internal.GetSize(), 2u);
    EXPECT_EQ(internal.GetLeftMostChild(), 1u);
    EXPECT_EQ(internal.KeyAt(0), 10);
    EXPECT_EQ(internal.ChildAt(0), 2u);
    EXPECT_EQ(internal.KeyAt(1), 30);
    EXPECT_EQ(internal.ChildAt(1), 4u);

    ASSERT_TRUE(internal.RemoveChildAt(0));
    EXPECT_EQ(internal.GetSize(), 1u);
    EXPECT_EQ(internal.GetLeftMostChild(), 2u);
    EXPECT_EQ(internal.KeyAt(0), 30);
    EXPECT_EQ(internal.ChildAt(0), 4u);
}

TEST(BPlusTreeInternalPageTest, BorrowHelpersMoveBoundaryChildren)
{
    Page left_page;
    Page right_page;
    left_page.InitBlank(710, PageType::INTERNAL);
    right_page.InitBlank(711, PageType::INTERNAL);
    BPlusTreeInternalPage left(&left_page);
    BPlusTreeInternalPage right(&right_page);

    ASSERT_TRUE(left.InitForNewInternal(8));
    ASSERT_TRUE(right.InitForNewInternal(8));

    ASSERT_TRUE(left.PopulateNewRoot(1, 10, 2));
    ASSERT_TRUE(left.InsertAfter(2, 20, 3));

    ASSERT_TRUE(right.PopulateNewRoot(4, 40, 5));
    ASSERT_TRUE(right.InsertAfter(5, 50, 6));

    int32_t parent_key = 30;
    ASSERT_TRUE(right.MoveFirstToEndOf(&left, parent_key, &parent_key));
    EXPECT_EQ(parent_key, 40);
    EXPECT_EQ(left.GetSize(), 3u);
    EXPECT_EQ(left.KeyAt(2), 30);
    EXPECT_EQ(right.GetLeftMostChild(), 5u);
    EXPECT_EQ(right.GetSize(), 1u);
    EXPECT_EQ(right.KeyAt(0), 50);

    ASSERT_TRUE(left.MoveLastToFrontOf(&right, parent_key, &parent_key));
    EXPECT_EQ(parent_key, 30);
    EXPECT_EQ(left.GetSize(), 2u);
    EXPECT_EQ(right.GetLeftMostChild(), 4u);
    EXPECT_EQ(right.GetSize(), 2u);
    EXPECT_EQ(right.KeyAt(0), 40);
    EXPECT_EQ(right.KeyAt(1), 50);
}

TEST(BPlusTreeInternalPageTest, MoveAllToMergesWithMiddleKey)
{
    Page left_page;
    Page right_page;
    left_page.InitBlank(720, PageType::INTERNAL);
    right_page.InitBlank(721, PageType::INTERNAL);
    BPlusTreeInternalPage left(&left_page);
    BPlusTreeInternalPage right(&right_page);

    ASSERT_TRUE(left.InitForNewInternal(12));
    ASSERT_TRUE(right.InitForNewInternal(12));

    ASSERT_TRUE(left.PopulateNewRoot(1, 10, 2));
    ASSERT_TRUE(left.InsertAfter(2, 20, 3));

    ASSERT_TRUE(right.PopulateNewRoot(4, 40, 5));
    ASSERT_TRUE(right.InsertAfter(5, 50, 6));

    right.MoveAllTo(&left, 30);

    EXPECT_EQ(left.GetSize(), 5u);
    EXPECT_EQ(left.GetLeftMostChild(), 1u);
    EXPECT_EQ(left.KeyAt(0), 10);
    EXPECT_EQ(left.KeyAt(1), 20);
    EXPECT_EQ(left.KeyAt(2), 30);
    EXPECT_EQ(left.KeyAt(3), 40);
    EXPECT_EQ(left.KeyAt(4), 50);
    EXPECT_EQ(left.ChildAt(0), 2u);
    EXPECT_EQ(left.ChildAt(1), 3u);
    EXPECT_EQ(left.ChildAt(2), 4u);
    EXPECT_EQ(left.ChildAt(3), 5u);
    EXPECT_EQ(left.ChildAt(4), 6u);
    EXPECT_EQ(right.GetSize(), 0u);
    EXPECT_EQ(right.GetLeftMostChild(), INVALID_PAGE_ID);
}

TEST(BPlusTreeInternalPageTest, InitFailsWhenPageIdInvalid)
{
    Page page;
    BPlusTreeInternalPage internal(&page);
    EXPECT_FALSE(internal.InitForNewInternal(8));
}

} // namespace HaruhiDB::storage
