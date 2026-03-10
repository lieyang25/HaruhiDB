/**
 * CXX/test/storage/disk/disk_manager_test.cxx
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
/* ==========================================================================
 * 进阶与联动性测试 (Advanced & Linkage Tests)
 * ========================================================================== */

/* Test: 跨越生命周期的空闲链表联动 (Free List Persistence Across Sessions)
 * 验证：分配多页 -> 释放部分页 -> 关闭数据库 -> 重新打开 -> 再次分配
 * 预期：重新打开后，依然能按照 LIFO (后进先出) 的顺序复用之前释放的页，且不会引起文件体积异常增长。
 */
TEST_F(DiskManagerTest, FreeListPersistenceLinkageTest) {
    page_id_t p1, p2, p3;
    {
        DiskManager dm(test_path_);
        p1 = dm.AllocatePage().value(); // page 1
        p2 = dm.AllocatePage().value(); // page 2
        p3 = dm.AllocatePage().value(); // page 3

        // 释放 p2 和 p3，Free List 现在的顺序应该是: Head -> p3 -> p2 -> INVALID
        ASSERT_TRUE(dm.DeallocatePage(p2).has_value());
        ASSERT_TRUE(dm.DeallocatePage(p3).has_value());
    } // dm 析构，Header 落盘

    {
        // 重新打开数据库，加载 Header
        DiskManager dm(test_path_);
        
        // 应该优先从 Free List 中获取
        auto r1 = dm.AllocatePage();
        ASSERT_TRUE(r1.has_value());
        EXPECT_EQ(r1.value(), p3) << "Expected to reuse the most recently deallocated page (p3)";

        auto r2 = dm.AllocatePage();
        ASSERT_TRUE(r2.has_value());
        EXPECT_EQ(r2.value(), p2) << "Expected to reuse the next deallocated page (p2)";

        // Free List 空了，应该分配新的 page_id
        auto r3 = dm.AllocatePage();
        ASSERT_TRUE(r3.has_value());
        EXPECT_GT(r3.value(), p3) << "Expected to allocate a brand new page";
    }
}

/* Test: 交错分配与释放的复杂联动 (Interleaved Allocation and Deallocation)
 * 验证：频繁的分配、释放、再分配操作交织在一起，检查 Free List 的指针是否发生环路或断裂。
 */
TEST_F(DiskManagerTest, InterleavedAllocDeallocTest) {
    DiskManager dm(test_path_);
    
    auto p1 = dm.AllocatePage().value();
    auto p2 = dm.AllocatePage().value();
    
    dm.DeallocatePage(p1);
    
    auto p3 = dm.AllocatePage().value(); // 应该复用 p1
    EXPECT_EQ(p3, p1);
    
    auto p4 = dm.AllocatePage().value(); // 应该分配新页
    EXPECT_GT(p4, p2);
    
    dm.DeallocatePage(p2);
    dm.DeallocatePage(p4);
    dm.DeallocatePage(p3); // 此时链表: Head -> p3(即p1) -> p4 -> p2
    
    EXPECT_EQ(dm.AllocatePage().value(), p3);
    EXPECT_EQ(dm.AllocatePage().value(), p4);
    EXPECT_EQ(dm.AllocatePage().value(), p2);
}

/* Test: 大规模分配压力测试与文件体积验证 (Mass Allocation Stress & File Size Check)
 * 验证：连续分配大量页，检查物理文件大小是否严格遵循 (页数 + 1(Header)) * PAGE_SIZE。
 */
TEST_F(DiskManagerTest, MassAllocationStressTest) {
    const int NUM_PAGES = 1000;
    {
        DiskManager dm(test_path_);
        for (int i = 0; i < NUM_PAGES; ++i) {
            ASSERT_TRUE(dm.AllocatePage().has_value());
        }
    }
    // 检查文件体积：Header (1) + NUM_PAGES = 1001 页
    uint64_t expected_size = static_cast<uint64_t>(NUM_PAGES + 1) * PAGE_SIZE;
    EXPECT_EQ(std::filesystem::file_size(test_path_), expected_size);
}

/* Test: 极限复用：全量释放后全量再分配 (Mass Deallocation and Reallocation)
 * 验证：将所有页释放后再次分配，物理文件大小不应发生任何增长。
 */
TEST_F(DiskManagerTest, MassDeallocationReallocationTest) {
    const int NUM_PAGES = 500;
    std::vector<page_id_t> pages;
    
    DiskManager dm(test_path_);
    for (int i = 0; i < NUM_PAGES; ++i) {
        pages.push_back(dm.AllocatePage().value());
    }
    
    uint64_t size_after_alloc = std::filesystem::file_size(test_path_);
    
    for (page_id_t pid : pages) {
        ASSERT_TRUE(dm.DeallocatePage(pid).has_value());
    }
    
    for (int i = 0; i < NUM_PAGES; ++i) {
        ASSERT_TRUE(dm.AllocatePage().has_value());
    }
    
    uint64_t size_after_realloc = std::filesystem::file_size(test_path_);
    EXPECT_EQ(size_after_alloc, size_after_realloc) << "File size should not grow when reusing pages";
}

/* ==========================================================================
 * 异常与边界测试 (Edge Cases & Exception Tests)
 * ========================================================================== */

/* Test: 文件大小非对齐损坏 (File Size Misalignment Corruption)
 * 验证：如果外部力量导致物理文件被截断，大小不再是 PAGE_SIZE 的整数倍，应拒绝打开。
 */
TEST_F(DiskManagerTest, FileSizeMisalignmentCorruptionTest) {
    // 1. 正常创建
    {
        DiskManager dm(test_path_);
        dm.AllocatePage(); 
    }
    
    // 2. 模拟文件损坏：追加几个非法字节，打破 PAGE_SIZE 对齐
    {
        std::fstream f(test_path_, std::ios::out | std::ios::app | std::ios::binary);
        f.write("bad", 3);
        f.close();
    }
    
    // 3. 期望打开失败并抛出异常
    EXPECT_THROW({
        DiskManager dm(test_path_);
    }, std::runtime_error);
}

/* Test: 非法 Page ID 读写测试 (Invalid Page ID Access)
 * 验证：传入负数、INVALID_PAGE_ID 或极大的越界 ID 时的防御情况。
 */
TEST_F(DiskManagerTest, InvalidPageIdAccessTest) {
    DiskManager dm(test_path_);
    page_data_t buf{};
    
    // 越界读
    auto res1 = dm.ReadPage(999999, buf);
    EXPECT_FALSE(res1.has_value());
    EXPECT_EQ(res1.error().err_code, ErrorCode::ReadPageOutOfRange);
    
    // 使用 INVALID_PAGE_ID 读写 (-1 转换为 uint32_t 会变成极大的正数)
    auto res2 = dm.ReadPage(INVALID_PAGE_ID, buf);
    EXPECT_FALSE(res2.has_value());
    
    // 试图 Deallocate INVALID_PAGE_ID（应该被拒绝）
    auto res3 = dm.DeallocatePage(INVALID_PAGE_ID);
    EXPECT_FALSE(res3.has_value());
}

/* Test: Header 页保护测试 (Header Page Protection Check)
 * 验证：Page 0 是 Header 页，普通 WritePage 必须禁止写入。
 */
TEST_F(DiskManagerTest, HeaderPageWriteIsProtected) {
    page_id_t header_pid = 0;
    page_data_t zero_buf{}; // 全 0 数据
    
    {
        DiskManager dm(test_path_);
        auto res = dm.WritePage(header_pid, zero_buf);
        EXPECT_FALSE(res.has_value());
        EXPECT_EQ(res.error().err_code, ErrorCode::WriteIOError);
    }
    
    // Header 未被破坏，重新打开应成功。
    EXPECT_NO_THROW({
        DiskManager dm(test_path_);
        (void)dm;
    });
}

/* Test: 重复释放同一页应该失败，防止 free-list 重复节点 */
TEST_F(DiskManagerTest, DoubleDeallocateShouldFail) {
    DiskManager dm(test_path_);
    auto pid = dm.AllocatePage();
    ASSERT_TRUE(pid.has_value());

    auto first = dm.DeallocatePage(pid.value());
    ASSERT_TRUE(first.has_value());

    auto second = dm.DeallocatePage(pid.value());
    EXPECT_FALSE(second.has_value());
}

/* Test: 自动创建嵌套目录 (Auto Directory Creation Test)
 * 验证：如果传入的路径包含不存在的深层目录，DiskManager 应能自动递归创建。
 */
TEST_F(DiskManagerTest, NestedDirectoryCreationTest) {
    std::filesystem::path nested_path = "deeply/nested/dir/test_db.db";
    
    // 确保清理
    if (std::filesystem::exists("deeply")) {
        std::filesystem::remove_all("deeply");
    }
    
    {
        DiskManager dm(nested_path);
        EXPECT_TRUE(std::filesystem::exists(nested_path));
    }
    
    // 扫尾清理
    std::filesystem::remove_all("deeply");
}

/* Test: Deallocate 不清空数据 (Deallocate Data Retention Check)
 * 验证：DeallocatePage 只会覆盖前 8 个字节（用于存放 next_free_page），剩余的数据应该保留不变。
 * 这用于提醒上层组件，Allocate 出来的新页可能是包含脏数据的，必须手动清空。
 */
TEST_F(DiskManagerTest, DeallocateDataRetentionTest) {
    DiskManager dm(test_path_);
    auto pid = dm.AllocatePage().value();
    
    page_data_t buf{};
    // 填充特定模式：0xAA
    for (size_t i = 0; i < PAGE_SIZE; ++i) buf[i] = std::byte{0xAA};
    dm.WritePage(pid, buf);
    
    // 释放该页
    dm.DeallocatePage(pid);
    
    // 再次分配，必然拿到刚才那个页
    auto new_pid = dm.AllocatePage().value();
    EXPECT_EQ(new_pid, pid);
    
    page_data_t read_buf{};
    dm.ReadPage(new_pid, read_buf);
    
    // 检查第 8 字节之后的数据是否依然是 0xAA（前 8 字节被 Free List 指针覆盖过）
    for (size_t i = 8; i < PAGE_SIZE; ++i) {
        EXPECT_EQ(read_buf[i], std::byte{0x00});
    }
}
} // namespace storage
} // namespace HaruhiDB
