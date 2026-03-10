/**
 * CXX/test/buffer/buffer_pool_manager/buffer_pool_manager_test.cxx
 */

#include "gtest/gtest.h"

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"

#include <filesystem>
#include <memory>
#include <string>

namespace HaruhiDB::buffer {

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto filename = std::string("bpm_") + info->test_suite_name() + "_" + info->name() + ".db";
        db_path_ = std::filesystem::temp_directory_path() / filename;
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_path_, ec);
    }

    std::unique_ptr<storage::DiskManager> MakeDiskManager() { return std::make_unique<storage::DiskManager>(db_path_); }

    std::filesystem::path db_path_;
};

TEST_F(BufferPoolManagerTest, NewPageFetchAndUnpinLifecycle) {
    auto dm = MakeDiskManager();
    BufferPoolManager bpm(2, dm.get());

    page_id_t pid = INVALID_PAGE_ID;
    auto new_page = bpm.NewPage(&pid);
    ASSERT_TRUE(new_page.has_value());
    ASSERT_NE(new_page.value(), nullptr);
    ASSERT_EQ(new_page.value()->PageId(), pid);

    new_page.value()->Data()[0] = std::byte{0x7A};

    auto fetched = bpm.FetchPage(pid);
    ASSERT_TRUE(fetched.has_value());
    ASSERT_EQ(fetched.value(), new_page.value());
    ASSERT_EQ(fetched.value()->Data()[0], std::byte{0x7A});

    EXPECT_TRUE(bpm.UnpinPage(pid, false));
    EXPECT_TRUE(bpm.UnpinPage(pid, true));
}

TEST_F(BufferPoolManagerTest, NewPageFailsWhenPoolIsFullyPinned) {
    auto dm = MakeDiskManager();
    BufferPoolManager bpm(1, dm.get());

    page_id_t pid1 = INVALID_PAGE_ID;
    auto p1 = bpm.NewPage(&pid1);
    ASSERT_TRUE(p1.has_value());

    page_id_t pid2 = INVALID_PAGE_ID;
    auto p2 = bpm.NewPage(&pid2);
    EXPECT_FALSE(p2.has_value());

    EXPECT_TRUE(bpm.UnpinPage(pid1, false));
}

TEST_F(BufferPoolManagerTest, EvictDirtyPageAndReadBackData) {
    auto dm = MakeDiskManager();
    BufferPoolManager bpm(1, dm.get());

    page_id_t pid1 = INVALID_PAGE_ID;
    auto p1 = bpm.NewPage(&pid1);
    ASSERT_TRUE(p1.has_value());
    p1.value()->Data()[0] = std::byte{0x2A};
    ASSERT_TRUE(bpm.UnpinPage(pid1, true));

    page_id_t pid2 = INVALID_PAGE_ID;
    auto p2 = bpm.NewPage(&pid2);
    ASSERT_TRUE(p2.has_value());
    ASSERT_TRUE(bpm.UnpinPage(pid2, false));

    auto p1_reload = bpm.FetchPage(pid1);
    ASSERT_TRUE(p1_reload.has_value());
    EXPECT_EQ(p1_reload.value()->Data()[0], std::byte{0x2A});
    EXPECT_TRUE(bpm.UnpinPage(pid1, false));
}

TEST_F(BufferPoolManagerTest, DeletePinnedPageFailsThenSucceedsAfterUnpin) {
    auto dm = MakeDiskManager();
    BufferPoolManager bpm(2, dm.get());

    page_id_t pid = INVALID_PAGE_ID;
    auto p = bpm.NewPage(&pid);
    ASSERT_TRUE(p.has_value());

    EXPECT_FALSE(bpm.DeletePage(pid));
    ASSERT_TRUE(bpm.UnpinPage(pid, false));
    EXPECT_TRUE(bpm.DeletePage(pid));

    EXPECT_FALSE(bpm.UnpinPage(pid, false));
    EXPECT_FALSE(bpm.DeletePage(INVALID_PAGE_ID));
}

TEST_F(BufferPoolManagerTest, InvalidAndMissingPageOperationsReturnExpectedSignals) {
    auto dm = MakeDiskManager();
    BufferPoolManager bpm(2, dm.get());

    auto null_new = bpm.NewPage(nullptr);
    EXPECT_FALSE(null_new.has_value());

    auto invalid_fetch = bpm.FetchPage(INVALID_PAGE_ID);
    EXPECT_FALSE(invalid_fetch.has_value());

    auto flush_missing = bpm.FlushPage(777777);
    ASSERT_FALSE(flush_missing.has_value());
    EXPECT_EQ(flush_missing.error().err_code, BufferPoolErrCode::PageNotFound);
}

TEST_F(BufferPoolManagerTest, DataPersistsAcrossManagerRestart) {
    page_id_t persisted_pid = INVALID_PAGE_ID;

    {
        auto dm = MakeDiskManager();
        BufferPoolManager bpm(2, dm.get());

        auto p = bpm.NewPage(&persisted_pid);
        ASSERT_TRUE(p.has_value());
        p.value()->Data()[123] = std::byte{0x55};
        ASSERT_TRUE(bpm.UnpinPage(persisted_pid, true));

        auto flush_all = bpm.FlushAllPages();
        ASSERT_TRUE(flush_all.has_value());
    }

    {
        auto dm = MakeDiskManager();
        BufferPoolManager bpm(2, dm.get());

        auto fetched = bpm.FetchPage(persisted_pid);
        ASSERT_TRUE(fetched.has_value());
        EXPECT_EQ(fetched.value()->Data()[123], std::byte{0x55});
        EXPECT_TRUE(bpm.UnpinPage(persisted_pid, false));
    }
}

} // namespace HaruhiDB::buffer
