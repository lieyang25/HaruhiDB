/**
 * CXX/test/storage/page/page_test.cxx
 */
#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include <thread>
#include <future>
#include <algorithm>

#include "storage/page/page.h"

namespace HaruhiDB
{
namespace storage
{

// Helper: 生成测试数据
static std::vector<std::byte> MakeData(size_t n, uint8_t pattern = 0xCC) {
    std::vector<std::byte> v(n);
    std::fill(v.begin(), v.end(), static_cast<std::byte>(pattern));
    return v;
}

// ---------------------------------------------------------
// 1. 结构与初始化测试 (Header & Init)
// ---------------------------------------------------------

TEST(PageTest, PersistentHeaderLayout) {
    // 验证关键字段位置，确保磁盘布局符合预期
    EXPECT_EQ(offsetof(PersistentHeader, lsn), 0);
    EXPECT_EQ(offsetof(PersistentHeader, page_id), 8);
    EXPECT_LE(sizeof(PersistentHeader), HEADER_SIZE);
}

TEST(PageTest, InitBlankState) {
    Page p;
    page_id_t pid = 42;
    p.InitBlank(pid, PageType::HEAP);

    auto* header = p.Header();
    EXPECT_EQ(header->page_id, pid);
    EXPECT_EQ(header->page_type, PageType::HEAP);
    EXPECT_EQ(header->slot_count, 0);
    EXPECT_EQ(header->lsn, 0);
    // 初始空闲空间偏移应在页面最末尾
    EXPECT_EQ(header->free_space_offset, PAGE_SIZE);
    EXPECT_EQ(p.PinCount(), 0);
    EXPECT_FALSE(p.IsDirty());
    
    // 验证初始可用空间 = 页面大小 - Header 占用的空间
    EXPECT_EQ(p.FreeSpace(), PAGE_SIZE - sizeof(PersistentHeader));
}

// ---------------------------------------------------------
// 2. 插入逻辑与 Slotted Page 布局测试
// ---------------------------------------------------------



TEST(PageTest, InsertRecordGrowthDirection) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    
    auto data1 = MakeData(100, 0x11);
    bool ok1 = p.InsertRecord(data1);
    ASSERT_TRUE(ok1);

    auto* header = p.Header();
    auto* slots = p.SlotArray();

    // 验证第一个 Slot
    EXPECT_EQ(header->slot_count, 1);
    EXPECT_EQ(slots[0].length, 100);
    // 数据应存放在页面尾部
    EXPECT_EQ(slots[0].offset, PAGE_SIZE - 100);
    EXPECT_EQ(header->free_space_offset, PAGE_SIZE - 100);

    // 验证第二个记录
    auto data2 = MakeData(200, 0x22);
    ASSERT_TRUE(p.InsertRecord(data2));
    
    EXPECT_EQ(header->slot_count, 2);
    EXPECT_EQ(slots[1].length, 200);
    // 第二个记录应存放在第一个记录的前面（地址更低）
    EXPECT_EQ(slots[1].offset, PAGE_SIZE - 100 - 200);
    EXPECT_EQ(header->free_space_offset, PAGE_SIZE - 300);
}

TEST(PageTest, GetSlotValidation) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    p.InsertRecord(MakeData(10));

    // 合法获取
    auto res = p.GetSlot(0);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res.value()->length, 10);

    // 越界获取
    auto res_invalid = p.GetSlot(1);
    EXPECT_FALSE(res_invalid.has_value());
}

TEST(PageTest, RecordDataIntegrity) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    auto original = MakeData(50, 0xAB);
    p.InsertRecord(original);

    Slot* slot = p.GetSlot(0).value();
    std::byte* raw_ptr = p.RawData() + slot->offset;
    
    EXPECT_EQ(0, std::memcmp(raw_ptr, original.data(), 50));
}

// ---------------------------------------------------------
// 3. 边界与容量测试
// ---------------------------------------------------------

TEST(PageTest, PageFullBoundary) {
    Page p;
    p.InitBlank(1, PageType::HEAP);

    // 计算能插入的最大记录大小：
    // 剩余空间 = PAGE_SIZE - HeaderSize - 1个SlotSize
    size_t space_for_data = PAGE_SIZE - sizeof(PersistentHeader) - sizeof(Slot);
    
    EXPECT_TRUE(p.InsertRecord(MakeData(space_for_data)));
    EXPECT_EQ(p.FreeSpace(), 0);
    
    // 此时再插入哪怕 1 字节也会失败，因为连存放 Slot 的空间都没了
    EXPECT_FALSE(p.InsertRecord(MakeData(1)));
}

// ---------------------------------------------------------
// 4. Buffer Pool 元数据测试 (Pin/Dirty)
// ---------------------------------------------------------

TEST(PageTest, BufferPoolMetadataLifecycle) {
    Page p;
    p.Pin();
    p.Pin();
    EXPECT_EQ(p.PinCount(), 2);
    
    p.UnPin();
    EXPECT_EQ(p.PinCount(), 1);
    
    EXPECT_FALSE(p.IsDirty());
    p.MarkDirty();
    EXPECT_TRUE(p.IsDirty());
    p.ClearDirty();
    EXPECT_FALSE(p.IsDirty());
}

TEST(PageTest, ResetMetaDataPreservesPhysicalData) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    p.InsertRecord(MakeData(100));
    p.MarkDirty();
    p.Pin();

    // 模拟 Buffer Pool 复用该 Page 对象给另一个物理页 ID
    p.ResetMetaData(999);

    EXPECT_EQ(p.PageId(), 999);
    EXPECT_EQ(p.PinCount(), 0);
    EXPECT_FALSE(p.IsDirty());
    // 注意：ResetMetaData 往往只重置管理状态，物理内存中的 SlotCount 通常不应被抹除
    // 除非逻辑要求彻底 InitBlank
    EXPECT_EQ(p.Header()->slot_count, 1); 
}

// ---------------------------------------------------------
// 5. 并发控制测试 (Latch Logic)
// ---------------------------------------------------------

TEST(PageTest, SharedLatchConcurrency) {
    Page p;
    
    // 测试：多个读者可以同时持有锁
    p.RLock();
    std::atomic<bool> second_reader_success{false};
    
    std::thread t1([&]() {
        p.RLock();
        second_reader_success = true;
        p.RUnLock();
    });
    
    t1.join();
    EXPECT_TRUE(second_reader_success);
    p.RUnLock();
}

TEST(PageTest, ExclusiveLatchBlocking) {
    Page p;
    p.WLock(); // 持有写锁
    
    std::promise<void> ready_promise;
    std::future<void> ready_future = ready_promise.get_future();
    std::atomic<bool> reader_entered{false};

    std::thread reader([&]() {
        ready_promise.set_value();
        p.RLock(); // 应该被阻塞
        reader_entered = true;
        p.RUnLock();
    });

    ready_future.wait();
    // 给线程一点点启动时间
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(reader_entered); // 读者必须被写者阻塞
    
    p.WUnLock(); // 释放写锁
    reader.join();
    EXPECT_TRUE(reader_entered);
}
// ---------------------------------------------------------
// 15) 联动测试：插入与脏页标记的生命周期 (Insert + Dirty Linkage)
// ---------------------------------------------------------
TEST(PageLinkage, InsertAndDirtyCycle) {
    Page p;
    p.InitBlank(100, PageType::HEAP);
    
    // 逻辑联动：在 BufferPool 中，写入操作必须伴随 MarkDirty
    auto data = std::vector<std::byte>(20, std::byte{0x01});
    
    p.WLock();
    if (p.InsertRecord(data)) {
        p.MarkDirty();
    }
    p.WUnLock();
    
    EXPECT_TRUE(p.IsDirty());
    EXPECT_EQ(p.Header()->slot_count, 1);

    // 模拟磁盘刷新 (Flush)
    p.ClearDirty();
    EXPECT_FALSE(p.IsDirty());
    
    // 验证：清除脏标记不应影响物理数据
    ASSERT_TRUE(p.GetSlot(0).has_value());
    EXPECT_EQ(p.GetSlot(0).value()->length, 20);
}

// ---------------------------------------------------------
// 16) 联动测试：Slotted Page 空间挤压 (Slot Array vs Record Space)
// ---------------------------------------------------------
TEST(PageLinkage, PincerMovementStress) {
    Page p;
    p.InitBlank(500, PageType::HEAP);
    
    // 这是一个关键的联动：Slot 数组向后长，数据向前长，它们在中间“会师”
    // 我们不断插入极小记录，直到空间不足
    size_t count = 0;
    while (true) {
        auto tiny_rec = std::vector<std::byte>(1, std::byte{0xEE});
        // 每次插入消耗：1字节数据 + sizeof(Slot)字节的目录项
        if (!p.InsertRecord(tiny_rec)) {
            break; 
        }
        count++;
    }
    
    // 验证联动一致性：FreeSpace 应该不足以再容纳 (1字节数据 + 1个Slot)
    EXPECT_LT(p.FreeSpace(), sizeof(Slot) + 1);
    EXPECT_EQ(p.Header()->slot_count, count);
    
    // 检查最后一条记录的完整性
    auto last_slot = p.GetSlot(count - 1);
    ASSERT_TRUE(last_slot.has_value());
    EXPECT_EQ(*(p.RawData() + last_slot.value()->offset), std::byte{0xEE});
}

// ---------------------------------------------------------
// 17) 联动测试：并发 Pin 与 ResetMetaData 的安全性
// ---------------------------------------------------------
TEST(PageLinkage, ConcurrencyPinAndReset_Fixed) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    
    const int readers = 10;
    std::vector<std::thread> threads;
    std::atomic<bool> stop{false};

    for (int i = 0; i < readers; ++i) {
        threads.emplace_back([&p, &stop]() {
            while (!stop) {
                p.Pin();
                std::this_thread::yield();
                p.UnPin();
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 1. 先发停止信号
    stop = true;
    // 2. 等待所有读线程退出，此时 PinCount 理论上应该回到 0（因为 Pin/UnPin 配对）
    for (auto &t : threads) t.join();

    // 3. 此时再 Reset 才是安全的
    p.ResetMetaData(200);
    
    EXPECT_EQ(p.PinCount(), 0);
    EXPECT_EQ(p.PageId(), 200);
}

// ---------------------------------------------------------
// 18) 联动测试：RawData 修改与 Header 映射一致性
// ---------------------------------------------------------
TEST(PageLinkage, RawMemoryVsHeaderInterplay) {
    Page p;
    p.InitBlank(777, PageType::HEAP);
    
    // 直接操作原始内存
    std::byte* raw = p.RawData();
    
    // 联动 1：通过 RawData 修改 LSN
    lsn_t new_lsn = 0x12345678;
    std::memcpy(raw + offsetof(PersistentHeader, lsn), &new_lsn, sizeof(lsn_t));
    EXPECT_EQ(p.Header()->lsn, new_lsn);
    
    // 联动 2：通过 Header 修改 PageType
    p.Header()->page_type = PageType::LEAF;
    EXPECT_EQ(static_cast<PageType>(*(raw + offsetof(PersistentHeader, page_type))), PageType::LEAF);
}

// ---------------------------------------------------------
// 19) 联动测试：复杂操作序列 (Insert -> Reset -> Re-Init)
// ---------------------------------------------------------
TEST(PageLinkage, ObjectReuseLifecycle) {
    Page p;
    
    // 第一阶段：作为 HEAP 页使用
    p.InitBlank(1, PageType::HEAP);
    p.InsertRecord(std::vector<std::byte>(100, std::byte{0x11}));
    p.MarkDirty();
    p.Pin();
    
    // 第二阶段：被淘汰并重用于 B+Tree Leaf 页
    p.ResetMetaData(2); // BPM 准备重用该对象
    EXPECT_EQ(p.PinCount(), 0);
    EXPECT_FALSE(p.IsDirty());
    
    p.InitBlank(2, PageType::LEAF); // 重新格式化
    EXPECT_EQ(p.Header()->slot_count, 0);
    EXPECT_EQ(p.FreeSpace(), PAGE_SIZE - sizeof(PersistentHeader));
    
    // 验证新页面的插入联动
    EXPECT_TRUE(p.InsertRecord(std::vector<std::byte>(50, std::byte{0x22})));
    EXPECT_EQ(p.Header()->slot_count, 1);
}

// ---------------------------------------------------------
// 20) 极端联动：大量变长记录并发读写锁竞争
// ---------------------------------------------------------
TEST(PageLinkage, MultithreadedReadWriteInterplay) {
    Page p;
    p.InitBlank(99, PageType::HEAP);
    std::atomic<int> success_count{0};
    const int num_workers = 4;

    auto worker = [&](int id) {
        for (int i = 0; i < 50; ++i) {
            auto data = MakeData(10, static_cast<uint8_t>(id));
            p.WLock();
            if (p.InsertRecord(data)) {
                p.MarkDirty();
                success_count++;
            }
            p.WUnLock();
            
            p.RLock();
            if (p.Header()->slot_count > 0) {
                auto s = p.GetSlot(0);
                (void)s; // 模拟读取
            }
            p.RUnLock();
        }
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < num_workers; ++i) workers.emplace_back(worker, i);
    for (auto &t : workers) t.join();

    EXPECT_EQ(p.Header()->slot_count, success_count.load());
}
} // namespace storage
} // namespace HaruhiDB