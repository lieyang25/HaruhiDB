/**
 * CXX/test/storage/page/b_plus_tree_leaf_page_test.cxx
 */

#include "gtest/gtest.h"

#include "storage/page/b_plus_tree_leaf_page.h"

namespace HaruhiDB::storage
{
namespace
{
    struct Int32Comparator
    {
        int operator()(int32_t lhs, int32_t rhs) const noexcept
        {
            if (lhs < rhs) {
                return -1;
            }
            if (lhs > rhs) {
                return 1;
            }
            return 0;
        }
    };
} // namespace

TEST(BPlusTreeLeafPageTest, InitClampsOversizedMaxSize)
{
    Page page;
    BPlusTreeLeafPage leaf(&page);
    const uint16_t physical_max = leaf.ComputeMaxSize();
    ASSERT_GT(physical_max, 0u);

    ASSERT_TRUE(
        leaf.InitForNewLeaf(
            17,
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
    BPlusTreeLeafPage leaf(&page);
    ASSERT_TRUE(leaf.InitForNewLeaf(1, 8));
    Int32Comparator comparator;

    ASSERT_TRUE(leaf.Insert(30, record::RID(1, 30), comparator));
    ASSERT_TRUE(leaf.Insert(10, record::RID(1, 10), comparator));
    ASSERT_TRUE(leaf.Insert(20, record::RID(1, 20), comparator));
    EXPECT_FALSE(leaf.Insert(20, record::RID(1, 99), comparator));
    EXPECT_EQ(leaf.GetSize(), 3u);

    EXPECT_EQ(leaf.KeyAt(0), 10);
    EXPECT_EQ(leaf.KeyAt(1), 20);
    EXPECT_EQ(leaf.KeyAt(2), 30);

    record::RID out;
    ASSERT_TRUE(leaf.Lookup(20, &out, comparator));
    EXPECT_EQ(out, record::RID(1, 20));

    EXPECT_FALSE(leaf.Lookup(25, &out, comparator));
    EXPECT_FALSE(leaf.Lookup(30, nullptr, comparator));
}

TEST(BPlusTreeLeafPageTest, InsertRespectsMaxSize)
{
    Page page;
    BPlusTreeLeafPage leaf(&page);
    ASSERT_TRUE(leaf.InitForNewLeaf(2, 3));
    Int32Comparator comparator;

    EXPECT_TRUE(leaf.Insert(1, record::RID(2, 1), comparator));
    EXPECT_TRUE(leaf.Insert(2, record::RID(2, 2), comparator));
    EXPECT_TRUE(leaf.Insert(3, record::RID(2, 3), comparator));
    EXPECT_FALSE(leaf.Insert(4, record::RID(2, 4), comparator));
    EXPECT_EQ(leaf.GetSize(), 3u);
}

TEST(BPlusTreeLeafPageTest, MoveHalfToSplitsAndMaintainsLeafLinks)
{
    Page src_page;
    Page dst_page;
    BPlusTreeLeafPage src(&src_page);
    BPlusTreeLeafPage dst(&dst_page);
    Int32Comparator comparator;

    ASSERT_TRUE(src.InitForNewLeaf(100, 8, 7));
    ASSERT_TRUE(dst.InitForNewLeaf(200, 8, 7));
    src.SetNextPageId(777);

    ASSERT_TRUE(src.Insert(10, record::RID(10, 1), comparator));
    ASSERT_TRUE(src.Insert(20, record::RID(20, 1), comparator));
    ASSERT_TRUE(src.Insert(30, record::RID(30, 1), comparator));
    ASSERT_TRUE(src.Insert(40, record::RID(40, 1), comparator));
    ASSERT_TRUE(src.Insert(50, record::RID(50, 1), comparator));

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
    BPlusTreeLeafPage src(&src_page);
    BPlusTreeLeafPage dst(&dst_page);
    Int32Comparator comparator;

    ASSERT_TRUE(src.InitForNewLeaf(300, 8));
    ASSERT_TRUE(dst.InitForNewLeaf(400, 8));
    src.SetNextPageId(999);

    ASSERT_TRUE(src.Insert(1, record::RID(1, 1), comparator));
    ASSERT_TRUE(src.Insert(2, record::RID(1, 2), comparator));
    ASSERT_TRUE(src.Insert(3, record::RID(1, 3), comparator));
    ASSERT_TRUE(src.Insert(4, record::RID(1, 4), comparator));
    ASSERT_TRUE(dst.Insert(100, record::RID(2, 1), comparator));

    src.MoveHalfTo(&dst);

    EXPECT_EQ(src.GetSize(), 4u);
    EXPECT_EQ(dst.GetSize(), 1u);
    EXPECT_EQ(src.GetNextPageId(), 999u);
    EXPECT_EQ(dst.KeyAt(0), 100);
}

} // namespace HaruhiDB::storage
