/**
 * CXX/test/storage/disk/disk_manager_test.cxx
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <cstdio>

#include "storage/disk/disk_manager.h"

namespace HaruhiDB {
namespace storage {
    class DiskManagerTest : public ::testing::Test {
    protected:
        std::filesystem::path test_path_ = "test_db_file.db";

        void SetUp() override {
            if (std::filesystem::exists(test_path_)) {
                std::filesystem::remove(test_path_);
            }
        }

        void TearDown() override {
            if (std::filesystem::exists(test_path_)) {
                std::filesystem::remove(test_path_);
            }
        }
    };
    TEST_F(DiskManagerTest, InitAndHeaderTest) {
        DiskManager dm(test_path_);

        ASSERT_TRUE(std::filesystem::exists(test_path_));

        // reopen to ensure header persisted
        {
            DiskManager dm2(test_path_);
        }

        SUCCEED();
    }
    TEST_F(DiskManagerTest, AllocateWriteReadTest) {
        DiskManager dm(test_path_);

        auto pid_exp = dm.AllocatePage();
        ASSERT_TRUE(pid_exp.has_value());
        page_id_t pid = pid_exp.value();

        page_data_t write_buf{};
        for (size_t i = 0; i < PAGE_SIZE; i++) {
            write_buf[i] = std::byte(i % 256);
        }

        ASSERT_TRUE(dm.WritePage(pid, write_buf).has_value());

        page_data_t read_buf{};
        ASSERT_TRUE(dm.ReadPage(pid, read_buf).has_value());

        for (size_t i = 0; i < PAGE_SIZE; i++) {
            ASSERT_EQ(write_buf[i], read_buf[i]);
        }
    }
    TEST_F(DiskManagerTest, MultipleAllocateTest) {
        DiskManager dm(test_path_);

        auto p1 = dm.AllocatePage();
        auto p2 = dm.AllocatePage();
        auto p3 = dm.AllocatePage();

        ASSERT_TRUE(p1.has_value());
        ASSERT_TRUE(p2.has_value());
        ASSERT_TRUE(p3.has_value());

        ASSERT_EQ(p2.value(), p1.value() + 1);
        ASSERT_EQ(p3.value(), p2.value() + 1);
    }
    TEST_F(DiskManagerTest, DeallocateAndReuseTest) {
        DiskManager dm(test_path_);

        auto p1 = dm.AllocatePage();
        auto p2 = dm.AllocatePage();

        ASSERT_TRUE(p1.has_value());
        ASSERT_TRUE(p2.has_value());

        page_id_t pid1 = p1.value();

        ASSERT_TRUE(dm.DeallocatePage(pid1).has_value());

        auto p3 = dm.AllocatePage();
        ASSERT_TRUE(p3.has_value());

        // 应该复用 pid1
        ASSERT_EQ(p3.value(), pid1);
    }
    TEST_F(DiskManagerTest, ReadOutOfRangeTest) {
        DiskManager dm(test_path_);

        page_data_t buf{};
        auto result = dm.ReadPage(9999, buf);

        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error().err_code, ErrorCode::ReadPageOutOfRange);
    }
    TEST_F(DiskManagerTest, PersistenceTest) {
        page_id_t pid;

        {
            DiskManager dm(test_path_);
            auto pid_exp = dm.AllocatePage();
            ASSERT_TRUE(pid_exp.has_value());
            pid = pid_exp.value();

            page_data_t buf{};
            buf[0] = std::byte{42};

            ASSERT_TRUE(dm.WritePage(pid, buf).has_value());
            ASSERT_TRUE(dm.Flush().has_value());
        }

        // reopen
        {
            DiskManager dm(test_path_);

            page_data_t read_buf{};
            ASSERT_TRUE(dm.ReadPage(pid, read_buf).has_value());
            ASSERT_EQ(read_buf[0], std::byte{42});
        }
    }
} // namespace storage
} // namespace HaruhiDB