/**
 * CXX/test/storage/page/b_plus_tree_page_test.cxx
 */

#include "gtest/gtest.h"

#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/page.h"

#include <cstdint>
#include <limits>

namespace HaruhiDB::storage
{

TEST(BPlusTreePageTest, InitLeafRootPage)
{
    Page page;
    BPlusTreePage tree_page(&page);

    ASSERT_TRUE(tree_page.InitForNewPage(7, PageType::LEAF, 128));
    EXPECT_EQ(tree_page.GetPageId(), 7u);
    EXPECT_EQ(tree_page.GetPageType(), PageType::LEAF);
    EXPECT_TRUE(tree_page.IsLeafPage());
    EXPECT_FALSE(tree_page.IsInternalPage());
    EXPECT_TRUE(tree_page.IsRootPage());
    EXPECT_EQ(tree_page.GetParentPageId(), INVALID_PAGE_ID);
    EXPECT_EQ(tree_page.GetSize(), 0u);
    EXPECT_EQ(tree_page.GetMaxSize(), 128u);
    EXPECT_EQ(tree_page.GetMinSize(), 1u);
}

TEST(BPlusTreePageTest, InitInternalNonRootPage)
{
    Page page;
    BPlusTreePage tree_page(&page);

    ASSERT_TRUE(tree_page.InitForNewPage(11, PageType::INTERNAL, 64, 3));
    EXPECT_EQ(tree_page.GetPageId(), 11u);
    EXPECT_EQ(tree_page.GetPageType(), PageType::INTERNAL);
    EXPECT_FALSE(tree_page.IsLeafPage());
    EXPECT_TRUE(tree_page.IsInternalPage());
    EXPECT_FALSE(tree_page.IsRootPage());
    EXPECT_EQ(tree_page.GetParentPageId(), 3u);
    EXPECT_EQ(tree_page.GetMinSize(), 32u);
}

TEST(BPlusTreePageTest, IncreaseSizeClampsAndMutatorsMarkDirty)
{
    Page page;
    BPlusTreePage tree_page(&page);
    ASSERT_TRUE(tree_page.InitForNewPage(13, PageType::LEAF, 256));

    page.ClearDirty();
    tree_page.SetParentPageId(9);
    EXPECT_TRUE(page.IsDirty());
    EXPECT_EQ(tree_page.GetParentPageId(), 9u);

    page.ClearDirty();
    tree_page.SetSize(10);
    EXPECT_TRUE(page.IsDirty());
    EXPECT_EQ(tree_page.GetSize(), 10u);

    page.ClearDirty();
    tree_page.IncreaseSize(-100);
    EXPECT_TRUE(page.IsDirty());
    EXPECT_EQ(tree_page.GetSize(), 0u);

    tree_page.SetSize(static_cast<uint16_t>(std::numeric_limits<uint16_t>::max() - 1));
    tree_page.IncreaseSize(100);
    EXPECT_EQ(tree_page.GetSize(), std::numeric_limits<uint16_t>::max());
}

TEST(BPlusTreePageTest, RejectInvalidPageTypeForInit)
{
    Page page;
    BPlusTreePage tree_page(&page);

    EXPECT_FALSE(tree_page.InitForNewPage(5, PageType::HEAP, 32));
}

TEST(BPlusTreePageTest, BPlusTreeLayoutAlignment)
{
    Page page;

    const auto raw = reinterpret_cast<std::uintptr_t>(page.RawData());
    EXPECT_EQ(raw % alignof(PersistentHeader), 0u);

    const auto opaque = reinterpret_cast<std::uintptr_t>(page.Header()->opaque);
    EXPECT_EQ(opaque % alignof(BPlusTreeOpaqueHeader), 0u);

    const auto leaf_array = raw + sizeof(PersistentHeader);
    EXPECT_EQ(leaf_array % alignof(BPlusTreeLeafPage::MappingType), 0u);

    const auto internal_array = raw + sizeof(PersistentHeader);
    EXPECT_EQ(internal_array % alignof(BPlusTreeInternalPage::MappingType), 0u);
}

} // namespace HaruhiDB::storage
