/**
 * CXX/test/storage/disk/disk_manager_test.cxx
 *
 * Expanded unit tests for DiskManager with short English comments on each test.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
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

    // helper: corrupt header magic number (write zeros to first 4 bytes)
    void CorruptHeaderMagic() {
        std::fstream f(test_path_, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        uint32_t zero = 0;
        f.seekp(0, std::ios::beg);
        f.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        f.flush();
        f.close();
    }
};

/* Test: creation and header written (verify file exists and header can be reloaded) */
TEST_F(DiskManagerTest, InitAndHeaderTest) {
    DiskManager dm(test_path_);

    ASSERT_TRUE(std::filesystem::exists(test_path_));

    // reopen to ensure header persisted
    {
        DiskManager dm2(test_path_);
    }

    SUCCEED();
}

/* Test: allocate a page, write a pattern and read it back */
TEST_F(DiskManagerTest, AllocateWriteReadTest) {
    DiskManager dm(test_path_);

    auto pid_exp = dm.AllocatePage();
    ASSERT_TRUE(pid_exp.has_value());
    page_id_t pid = pid_exp.value();

    page_data_t write_buf{};
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        write_buf[i] = std::byte(static_cast<unsigned char>(i % 256));
    }

    ASSERT_TRUE(dm.WritePage(pid, write_buf).has_value());

    page_data_t read_buf{};
    ASSERT_TRUE(dm.ReadPage(pid, read_buf).has_value());

    for (size_t i = 0; i < PAGE_SIZE; i++) {
        ASSERT_EQ(write_buf[i], read_buf[i]);
    }
}

/* Test: allocate multiple pages; ensure page ids increase sequentially */
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

/* Test: deallocate a page and verify it is reused on next allocation */
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

    // should reuse pid1 (LIFO free list expected behavior)
    ASSERT_EQ(p3.value(), pid1);
}

/* Test: deallocate multiple pages and ensure reuse order (LIFO) */
TEST_F(DiskManagerTest, FreeListMultipleReuseTest) {
    DiskManager dm(test_path_);

    auto a = dm.AllocatePage();
    auto b = dm.AllocatePage();
    auto c = dm.AllocatePage();
    ASSERT_TRUE(a.has_value() && b.has_value() && c.has_value());

    page_id_t pa = a.value();
    page_id_t pb = b.value();
    page_id_t pc = c.value();

    // deallocate in order: pa, pb, pc -> free list head should be pc then pb then pa
    ASSERT_TRUE(dm.DeallocatePage(pa).has_value());
    ASSERT_TRUE(dm.DeallocatePage(pb).has_value());
    ASSERT_TRUE(dm.DeallocatePage(pc).has_value());

    auto r1 = dm.AllocatePage(); ASSERT_TRUE(r1.has_value()); // expect pc
    auto r2 = dm.AllocatePage(); ASSERT_TRUE(r2.has_value()); // expect pb
    auto r3 = dm.AllocatePage(); ASSERT_TRUE(r3.has_value()); // expect pa

    ASSERT_EQ(r1.value(), pc);
    ASSERT_EQ(r2.value(), pb);
    ASSERT_EQ(r3.value(), pa);
}

/* Test: overwrite an existing page and make sure new content persists */
TEST_F(DiskManagerTest, OverwritePageTest) {
    DiskManager dm(test_path_);

    auto p = dm.AllocatePage();
    ASSERT_TRUE(p.has_value());
    page_id_t pid = p.value();

    page_data_t buf1{};
    buf1[0] = std::byte{1};
    ASSERT_TRUE(dm.WritePage(pid, buf1).has_value());

    page_data_t buf2{};
    buf2[0] = std::byte{2};
    ASSERT_TRUE(dm.WritePage(pid, buf2).has_value());

    page_data_t read_buf{};
    ASSERT_TRUE(dm.ReadPage(pid, read_buf).has_value());
    ASSERT_EQ(read_buf[0], std::byte{2});
}

/* Test: attempt to write a page far beyond end (not append) — expect error */
TEST_F(DiskManagerTest, WriteBeyondEndTest) {
    DiskManager dm(test_path_);

    // choose a page id well beyond next_page (for a fresh DB next_page == 1)
    page_id_t bad_id = 5;
    page_data_t buf{};
    auto res = dm.WritePage(bad_id, buf);

    ASSERT_FALSE(res.has_value());
    ASSERT_EQ(res.error().err_code, ErrorCode::WriteIOError);
}

/* Test: reading a non-existent page returns a proper error code */
TEST_F(DiskManagerTest, ReadOutOfRangeTest) {
    DiskManager dm(test_path_);

    page_data_t buf{};
    auto result = dm.ReadPage(9999, buf);

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().err_code, ErrorCode::ReadPageOutOfRange);
}

/* Test: persistence across reopen — written byte should survive close/reopen */
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

/* Test: corrupt header magic and expect DiskManager to fail to initialize (exception) */
TEST_F(DiskManagerTest, HeaderCorruptionTest) {
    // create DB normally
    {
        DiskManager dm(test_path_);
        (void)dm;
    }

    // Corrupt header magic bytes
    CorruptHeaderMagic();

    // Now constructing DiskManager should fail (header magic mismatch -> runtime error thrown in ctor)
    EXPECT_THROW({
        DiskManager dm(test_path_);
        (void)dm;
    }, std::runtime_error);
}

} // namespace storage
} // namespace HaruhiDB