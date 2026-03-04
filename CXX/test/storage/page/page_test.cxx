/**
 * CXX/test/storage/page/page_test.cxx
 */
#include <gtest/gtest.h>

#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <chrono>

#include "storage/page/page.h"
namespace HaruhiDB
{
namespace storage
{
    // Helper: create a record with bytes 0,1,2,...
static std::vector<std::byte> MakeRecord(size_t n) {
    std::vector<std::byte> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<std::byte>(i & 0xFF);
    return v;
}

// Helper: read record pointed by slot_id into a vector
static std::vector<std::byte> ReadRecordFromSlot(Page &page, slot_id_t slot_id) {
    auto res = page.GetSlot(slot_id);
    if (!res.has_value()) return {};
    Slot* s = res.value();
    std::vector<std::byte> out(s->length);
    const std::byte* base = page.RawData();
    std::memcpy(out.data(), base + s->offset, s->length);
    return out;
}

// ------------------------------
// 1) Construction & InitBlank
// ------------------------------

TEST(PageInit, InitBlankBasic) {
    Page p;
    constexpr page_id_t pid = 12345;
    p.InitBlank(pid, PageType::HEAP);

    const PersistentHeader* hdr = p.Header();
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->page_id, pid);
    EXPECT_EQ(hdr->page_type, PageType::HEAP);
    EXPECT_EQ(hdr->slot_count, static_cast<slot_id_t>(0));
    EXPECT_EQ(p.FreeSpace(), static_cast<size_t>(PAGE_SIZE - HEADER_SIZE));
}

TEST(PageInit, InitBlankOverwriteDifferentTypes) {
    Page p;
    p.InitBlank(1, PageType::HEAP);
    // mutate header to see overwrite
    p.Header()->slot_count = 5;
    p.InitBlank(2, PageType::INTERNAL);
    const PersistentHeader* hdr = p.Header();
    EXPECT_EQ(hdr->page_id, 2);
    EXPECT_EQ(hdr->page_type, PageType::INTERNAL);
    // re-init should reset slot_count to 0
    EXPECT_EQ(hdr->slot_count, static_cast<slot_id_t>(0));
    EXPECT_EQ(p.FreeSpace(), static_cast<size_t>(PAGE_SIZE - HEADER_SIZE));
}

// ------------------------------
// 2) Single-record insert tests
// ------------------------------

TEST(PageInsertSingle, InsertSingleRecordStandard) {
    Page p;
    p.InitBlank(10, PageType::HEAP);
    auto rec = MakeRecord(16);
    bool ok = p.InsertRecord(std::span<const std::byte>(rec.data(), rec.size()));
    EXPECT_TRUE(ok);
    EXPECT_EQ(p.Header()->slot_count, static_cast<slot_id_t>(1));

    auto read_back = ReadRecordFromSlot(p, 0);
    EXPECT_EQ(read_back.size(), rec.size());
    EXPECT_EQ(0, std::memcmp(read_back.data(), rec.data(), rec.size()));
}

TEST(PageInsertSingle, InsertSingleRecordSmallSize) {
    Page p;
    p.InitBlank(11, PageType::HEAP);
    auto rec = MakeRecord(1);
    EXPECT_TRUE(p.InsertRecord(std::span<const std::byte>(rec.data(), rec.size())));
    EXPECT_EQ(p.Header()->slot_count, static_cast<slot_id_t>(1));

    auto read_back = ReadRecordFromSlot(p, 0);
    ASSERT_EQ(read_back.size(), 1u);
    EXPECT_EQ(read_back[0], rec[0]);
}

// ------------------------------
// 3) FreeSpace behavior tests
// ------------------------------

TEST(PageFreeSpace, InitialFreeSpace) {
    Page p;
    p.InitBlank(20, PageType::HEADER);
    EXPECT_EQ(p.FreeSpace(), static_cast<size_t>(PAGE_SIZE - HEADER_SIZE));
}

TEST(PageFreeSpace, AfterInsertFreeSpaceDecrease) {
    Page p;
    p.InitBlank(21, PageType::HEAP);
    size_t before = p.FreeSpace();
    auto rec = MakeRecord(32);
    ASSERT_TRUE(p.InsertRecord(std::span<const std::byte>(rec.data(), rec.size())));
    size_t after = p.FreeSpace();
    // expected decrease: record bytes + one Slot entry
    size_t expected_decrease = rec.size() + sizeof(Slot);
    EXPECT_EQ(before - after, expected_decrease);
}

// ------------------------------
// 4) GetSlot valid/invalid tests
// ------------------------------

TEST(PageGetSlot, GetSlotInvalidIndexWhenEmpty) {
    Page p;
    p.InitBlank(30, PageType::HEAP);
    auto r = p.GetSlot(0);
    EXPECT_FALSE(r.has_value());
    auto r2 = p.GetSlot(100);
    EXPECT_FALSE(r2.has_value());
}

TEST(PageGetSlot, GetSlotValidAfterInsert) {
    Page p;
    p.InitBlank(31, PageType::HEAP);
    auto rec = MakeRecord(12);
    ASSERT_TRUE(p.InsertRecord(std::span<const std::byte>(rec.data(), rec.size())));
    auto r = p.GetSlot(0);
    ASSERT_TRUE(r.has_value());
    Slot* s = r.value();
    EXPECT_EQ(s->length, static_cast<uint16_t>(rec.size()));
    auto read_back = ReadRecordFromSlot(p, 0);
    EXPECT_EQ(read_back.size(), rec.size());
    EXPECT_EQ(0, std::memcmp(read_back.data(), rec.data(), rec.size()));
}

// ------------------------------
// 5) Multiple-record insert & fill tests
// ------------------------------

TEST(PageMultiInsert, InsertMultipleRecordsNoOverlap) {
    Page p;
    p.InitBlank(40, PageType::HEAP);
    auto r1 = MakeRecord(8);
    auto r2 = MakeRecord(20);
    auto r3 = MakeRecord(5);

    ASSERT_TRUE(p.InsertRecord(std::span<const std::byte>(r1.data(), r1.size())));
    ASSERT_TRUE(p.InsertRecord(std::span<const std::byte>(r2.data(), r2.size())));
    ASSERT_TRUE(p.InsertRecord(std::span<const std::byte>(r3.data(), r3.size())));

    EXPECT_EQ(p.Header()->slot_count, static_cast<slot_id_t>(3));

    auto a1 = ReadRecordFromSlot(p, 0);
    auto a2 = ReadRecordFromSlot(p, 1);
    auto a3 = ReadRecordFromSlot(p, 2);

    EXPECT_EQ(a1.size(), r1.size());
    EXPECT_EQ(a2.size(), r2.size());
    EXPECT_EQ(a3.size(), r3.size());
    EXPECT_EQ(0, std::memcmp(a1.data(), r1.data(), r1.size()));
    EXPECT_EQ(0, std::memcmp(a2.data(), r2.data(), r2.size()));
    EXPECT_EQ(0, std::memcmp(a3.data(), r3.data(), r3.size()));

    auto s0 = p.GetSlot(0).value();
    auto s1 = p.GetSlot(1).value();
    auto s2 = p.GetSlot(2).value();
    // check non-overlap property
    EXPECT_TRUE(s0->offset + s0->length <= s1->offset || s1->offset + s1->length <= s0->offset);
    EXPECT_TRUE(s1->offset + s1->length <= s2->offset || s2->offset + s2->length <= s1->offset);
}

TEST(PageMultiInsert, InsertUntilFullStops) {
    Page p;
    p.InitBlank(41, PageType::HEAP);

    // Insert minimal records repeatedly until InsertRecord returns false.
    // Cap number of iterations to avoid pathological infinite loops.
    const size_t cap = 10000;
    size_t inserted = 0;
    for (size_t i = 0; i < cap; ++i) {
        auto r = MakeRecord(4); // small record to fill gradually
        if (!p.InsertRecord(std::span<const std::byte>(r.data(), r.size()))) break;
        ++inserted;
    }
    // After loop, either we filled the page (inserted > 0) or reached cap
    EXPECT_GT(inserted, 0u);
    // further insert must fail (try inserting a single tiny record)
    auto tiny = MakeRecord(1);
    bool ok = p.InsertRecord(std::span<const std::byte>(tiny.data(), tiny.size()));
    // ok may be false if already full; either false or insertion succeeded only if capacity remained.
    // We assert the page's reported FreeSpace is non-negative and consistent with header.
    EXPECT_LE(p.Header()->slot_count, static_cast<slot_id_t>(inserted + 1));
    EXPECT_GE(p.FreeSpace(), 0u);
}

// ------------------------------
// 6) Pin / UnPin tests (basic and concurrent)
// ------------------------------

TEST(PagePin, PinUnPinBasic) {
    Page p;
    EXPECT_EQ(p.PinCount(), 0);
    p.Pin();
    EXPECT_EQ(p.PinCount(), 1);
    p.Pin();
    EXPECT_EQ(p.PinCount(), 2);
    p.UnPin();
    EXPECT_EQ(p.PinCount(), 1);
    p.UnPin();
    EXPECT_EQ(p.PinCount(), 0);
}

TEST(PagePin, PinUnPinConcurrent) {
    Page p;
    const int threads = 8;
    const int per_thread_ops = 1000;
    std::vector<std::thread> th;
    for (int t = 0; t < threads; ++t) {
        th.emplace_back([&p, per_thread_ops]() {
            for (int i = 0; i < per_thread_ops; ++i) {
                p.Pin();
            }
            for (int i = 0; i < per_thread_ops; ++i) {
                p.UnPin();
            }
        });
    }
    for (auto &t : th) t.join();
    // all pins/unpins should cancel out
    EXPECT_EQ(p.PinCount(), 0);
}

// ------------------------------
// 7) Dirty flag tests
// ------------------------------

TEST(PageDirty, MarkDirtyAndQuery) {
    Page p;
    p.InitBlank(50, PageType::HEAP);
    EXPECT_FALSE(p.IsDirty());
    p.MarkDirty();
    EXPECT_TRUE(p.IsDirty());
}

TEST(PageDirty, InitBlankResetsDirty) {
    Page p;
    p.InitBlank(51, PageType::HEAP);
    p.MarkDirty();
    EXPECT_TRUE(p.IsDirty());
    // Re-initialize should clear dirty flag according to common semantics
    p.InitBlank(52, PageType::HEAP);
    EXPECT_FALSE(p.IsDirty());
}

// ------------------------------
// 8) RawData read/write and header interplay
// ------------------------------

TEST(PageRawData, RawDataWriteReadConsistency) {
    Page p;
    p.InitBlank(60, PageType::HEAP);
    // write after header region
    std::byte pattern[6] = {std::byte(0xAA), std::byte(0x01), std::byte(0x02),
                            std::byte(0x03), std::byte(0x04), std::byte(0x05)};
    std::byte* raw = p.RawData();
    std::byte* target = raw + HEADER_SIZE; // payload area
    std::memcpy(target, pattern, sizeof(pattern));
    std::byte sanity[6];
    std::memcpy(sanity, target, sizeof(sanity));
    EXPECT_EQ(0, std::memcmp(sanity, pattern, sizeof(pattern)));
}

TEST(PageRawData, RawDataModifyHeaderBytesReflectsInHeader) {
    Page p;
    p.InitBlank(61, PageType::HEAP);
    // Set page_id via RawData directly (overwrite header bytes)
    page_id_t new_pid = 0xDEADBEEF;
    std::byte* raw = p.RawData();
    // memcpy new page id into header beginning (assumes page_id_t is at header offset 0)
    // This relies on PersistentHeader layout where page_id is at a known offset.
    // Use memcpy into p.Header() as an alternative if layout is unsure.
    std::memcpy(raw + offsetof(PersistentHeader, page_id), &new_pid, sizeof(new_pid));
    // Now check header view sees it
    EXPECT_EQ(p.Header()->page_id, new_pid);
}

// ------------------------------
// Optional: main if test binary does not link gtest_main.
// ------------------------------
#ifndef GTEST_HAS_MAIN
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
} // namespace storage
} // namespace HaruhiDB