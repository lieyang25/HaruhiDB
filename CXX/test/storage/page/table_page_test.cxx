/**
 * CXX/test/storage/page/table_page_test.cxx
 */
#include "gtest/gtest.h"

#include "storage/page/table_page.h"
#include "storage/page/page.h"
#include "storage/record/tuple.h"
#include "common/config.h"

#include <vector>
#include <cstring>
#include <optional>

using namespace HaruhiDB;
using namespace HaruhiDB::storage;

/**
 * Helper utilities for tests (adapted to Tuple(std::span<std::byte>)).
 *
 * - NewPageForTest(): create a fresh Page instance for tests. If your Page
 *   construction API differs, adjust here.
 * - MakeTupleFromBytes(bytes): create an owned buffer (kept in fixture) and
 *   return a record::Tuple referencing that buffer via std::span.
 *
 * Note: record::Tuple stores a span only — the backing storage must outlive the
 * tuple. We keep all owned buffers in the test fixture's owned_buffers_ vector.
 */

// NOTE: adapt this if your Page constructor signature is different.
static Page* NewPageForTest() {
    // 如果 Page 的构造函数需要 page_id 或其他参数，请在这里改写。
    return new Page(); // 若无默认构造请告知我 page.h 的构造签名
}

class TablePageTest : public ::testing::Test {
protected:
    void SetUp() override {
        page_ = NewPageForTest();
        ASSERT_NE(page_, nullptr);
        page_->InitBlank(1, PageType::HEAP);
        table_page_ = new TablePage(page_);
        ASSERT_NE(table_page_, nullptr);
        table_page_->InitForNewPage(1);
        owned_buffers_.clear();
    }

    void TearDown() override {
        delete table_page_;
        delete page_;
        table_page_ = nullptr;
        page_ = nullptr;
        owned_buffers_.clear();
    }

    // Create a Tuple by copying bytes into an owned buffer and returning a Tuple
    // that references that buffer via std::span. The owned buffer is stored in
    // owned_buffers_ so it remains alive during the test.
    record::Tuple MakeTupleFromBytes(const std::vector<std::byte>& bytes) {
        owned_buffers_.push_back(bytes); // copy into owned storage
        auto &ref = owned_buffers_.back();
        std::span<std::byte> sp(ref.data(), ref.size());
        return record::Tuple(sp);
    }

    // Convenience: create tuple from std::string payload
    record::Tuple MakeTupleFromString(const std::string &s) {
        std::vector<std::byte> buf(s.size());
        std::memcpy(buf.data(), s.data(), s.size());
        return MakeTupleFromBytes(buf);
    }

    Page *page_{nullptr};
    TablePage *table_page_{nullptr};

    // keep owned buffers alive for the duration of each test
    std::vector<std::vector<std::byte>> owned_buffers_;
};

/* =========================================================================
 * Basic insert / get tests
 * ========================================================================= */

// Insert a small tuple and read it back
TEST_F(TablePageTest, InsertAndGetSimpleTuple) {
    auto t = MakeTupleFromString("hello");
    auto res = table_page_->InsertTuple(t);
    ASSERT_TRUE(bool(res)); // success expected
    slot_id_t sid = res.value();

    // Get back
    record::Tuple out;
    auto g = table_page_->GetTuple(sid, out);
    ASSERT_TRUE(bool(g));

    ASSERT_EQ(out.Size(), t.Size());
    ASSERT_EQ(std::memcmp(out.Data(), t.Data(), out.Size()), 0);
}

// Insert multiple tuples, ensure distinct slots and free space decreases
TEST_F(TablePageTest, MultipleInsertDistinctSlotsAndFreeSpace) {
    size_t before_free = table_page_->FreeSpace();
    std::vector<slot_id_t> slots;
    const int N = 5;
    for (int i = 0; i < N; ++i) {
        auto t = MakeTupleFromString("rec_" + std::to_string(i));
        auto r = table_page_->InsertTuple(t);
        ASSERT_TRUE(bool(r));
        slots.push_back(r.value());
    }
    // Distinct slot ids
    for (int i = 1; i < N; ++i) EXPECT_NE(slots[i-1], slots[i]);

    size_t after_free = table_page_->FreeSpace();
    EXPECT_LT(after_free, before_free);
}

/* =========================================================================
 * Boundary and error cases
 * ========================================================================= */

// Getting a tuple with out-of-range slot should return SlotOutOfRange error
TEST_F(TablePageTest, GetTupleSlotOutOfRange) {
    slot_id_t bad = static_cast<slot_id_t>(UINT16_MAX - 1);
    record::Tuple out;
    auto g = table_page_->GetTuple(bad, out);
    ASSERT_FALSE(bool(g));
    auto err = g.error();
    EXPECT_EQ(err.err_code, TablePageErrCode::SlotOutOfRange);
}

// Insert too large tuple should return InsufficientSpace
TEST_F(TablePageTest, InsertTupleInsufficientSpace) {

    while (true) {
        auto t = MakeTupleFromString("abcd");
        auto r = table_page_->InsertTuple(t);

        if (!r) {
            EXPECT_EQ(r.error().err_code,
                      TablePageErrCode::InsufficientSpace);
            break;
        }
    }

    auto extra = MakeTupleFromString("efgh");
    auto r2 = table_page_->InsertTuple(extra);

    ASSERT_FALSE(r2);
    EXPECT_EQ(r2.error().err_code,
              TablePageErrCode::InsufficientSpace);
}

/* =========================================================================
 * Delete (MarkDel) and subsequent behavior
 * ========================================================================= */

// Mark delete then ensure GetTuple returns error
TEST_F(TablePageTest, MarkDeleteAndGet) {
    auto t = MakeTupleFromString("to_be_deleted");
    auto r = table_page_->InsertTuple(t);
    ASSERT_TRUE(bool(r));
    slot_id_t sid = r.value();

    // Mark deleted
    auto m = table_page_->MarkDelTuple(sid);
    ASSERT_TRUE(bool(m));

    // Now GetTuple should return an error
    record::Tuple out;
    auto g = table_page_->GetTuple(sid, out);
    ASSERT_FALSE(bool(g));
    auto err = g.error();
    EXPECT_TRUE(err.err_code == TablePageErrCode::SlotAlreadyDeleted ||
                err.err_code == TablePageErrCode::InvalidSlotContent);
}

// Mark delete on already deleted slot should return SlotAlreadyDeleted
TEST_F(TablePageTest, MarkDeleteAlreadyDeleted) {
    auto t = MakeTupleFromString("dupdel");
    auto r = table_page_->InsertTuple(t);
    ASSERT_TRUE(bool(r));
    slot_id_t sid = r.value();

    auto m1 = table_page_->MarkDelTuple(sid);
    ASSERT_TRUE(bool(m1));

    auto m2 = table_page_->MarkDelTuple(sid);
    ASSERT_FALSE(bool(m2));
    EXPECT_EQ(m2.error().err_code, TablePageErrCode::SlotAlreadyDeleted);
}

/* =========================================================================
 * Update tests: in-place and require-move
 * ========================================================================= */

// In-place update when new tuple size <= old tuple size
TEST_F(TablePageTest, UpdateTupleInPlace) {
    auto t = MakeTupleFromString(std::string(64, 'A'));
    auto r = table_page_->InsertTuple(t);
    ASSERT_TRUE(bool(r));
    slot_id_t sid = r.value();

    auto small = MakeTupleFromString(std::string(16, 'B'));
    auto u = table_page_->UpdateTuple(sid, small);
    ASSERT_TRUE(bool(u));

    record::Tuple out;
    auto g = table_page_->GetTuple(sid, out);
    ASSERT_TRUE(bool(g));
    ASSERT_EQ(out.Size(), small.Size());
    ASSERT_EQ(std::memcmp(out.Data(), small.Data(), out.Size()), 0);
}

// Update to a bigger size when page cannot accommodate move -> accept either InsufficientSpace or success
TEST_F(TablePageTest, UpdateTupleToBiggerMayRequireMove) {
    std::vector<slot_id_t> ids;
    // insert several medium tuples to reduce free space
    for (int i = 0; i < 8; ++i) {
        auto t = MakeTupleFromString(std::string(128, 'x'));
        auto r = table_page_->InsertTuple(t);
        if (!bool(r)) break;
        ids.push_back(r.value());
    }
    auto small = MakeTupleFromString("small");
    auto rsmall = table_page_->InsertTuple(small);
    if (!bool(rsmall)) {
        GTEST_SKIP() << "Could not insert initial small tuple; skip test.";
        return;
    }
    slot_id_t sid = rsmall.value();

    size_t free = table_page_->FreeSpace();
    std::string bigstr((free > 0 ? free : 1024) + 64, 'Z');
    // create big tuple and keep its buffer alive in owned_buffers_
    std::vector<std::byte> bigbuf(bigstr.size());
    std::memcpy(bigbuf.data(), bigstr.data(), bigstr.size());
    auto big = MakeTupleFromBytes(bigbuf);

    auto u = table_page_->UpdateTuple(sid, big);
    if (!bool(u)) {
        auto err = u.error();
        EXPECT_EQ(err.err_code, TablePageErrCode::InsufficientSpace);
    } else {
        record::Tuple out;
        auto g = table_page_->GetTuple(sid, out);
        ASSERT_TRUE(bool(g));
        ASSERT_EQ(out.Size(), big.Size());
        ASSERT_EQ(std::memcmp(out.Data(), big.Data(), out.Size()), 0);
    }
}

/* =========================================================================
 * Slot array and slot flags behavior
 * ========================================================================= */

TEST_F(TablePageTest, SlotArrayAndSlotFlags) {
    auto t = MakeTupleFromString("slot_test");
    auto r = table_page_->InsertTuple(t);
    ASSERT_TRUE(bool(r));
    slot_id_t sid = r.value();

    Slot* arr = table_page_->SlotArray();
    ASSERT_NE(arr, nullptr);

    Slot* s = table_page_->GetSlot(sid);
    ASSERT_NE(s, nullptr);
    EXPECT_GT(s->GetLength(), 0u);
    EXPECT_FALSE(s->IsDeleted());
}

/* =========================================================================
 * Combined scenario: insert->update->delete->attempt-get -> ensure others ok
 * ========================================================================= */

TEST_F(TablePageTest, CombinedInsertUpdateDeleteFlow) {
    auto a = MakeTupleFromString("A");
    auto b = MakeTupleFromString("BBBB");
    auto c = MakeTupleFromString("CCCCCCCC");

    auto ra = table_page_->InsertTuple(a); ASSERT_TRUE(bool(ra));
    auto rb = table_page_->InsertTuple(b); ASSERT_TRUE(bool(rb));
    auto rc = table_page_->InsertTuple(c); ASSERT_TRUE(bool(rc));

    slot_id_t sa = ra.value(), sb = rb.value(), sc = rc.value();

    auto small = MakeTupleFromString("b");
    ASSERT_TRUE(bool(table_page_->UpdateTuple(sb, small)));

    ASSERT_TRUE(bool(table_page_->MarkDelTuple(sa)));

    record::Tuple out;
    auto g1 = table_page_->GetTuple(sa, out);
    EXPECT_FALSE(bool(g1));

    record::Tuple outb, outc;
    ASSERT_TRUE(bool(table_page_->GetTuple(sb, outb)));
    ASSERT_EQ(outb.Size(), small.Size());
    ASSERT_TRUE(bool(table_page_->GetTuple(sc, outc)));
    ASSERT_EQ(outc.Size(), c.Size());
}

TEST_F(TablePageTest, TupleCountersTrackInsertDeleteAndReuse) {
    auto t1 = MakeTupleFromString("tuple_1");
    auto t2 = MakeTupleFromString("tuple_2");
    auto t3 = MakeTupleFromString("tuple_3");

    auto r1 = table_page_->InsertTuple(t1);
    auto r2 = table_page_->InsertTuple(t2);
    auto r3 = table_page_->InsertTuple(t3);
    ASSERT_TRUE(bool(r1));
    ASSERT_TRUE(bool(r2));
    ASSERT_TRUE(bool(r3));

    const auto* h1 = table_page_->HeaderData();
    EXPECT_EQ(h1->slot_count, 3);
    EXPECT_EQ(h1->alive_tuple_count, 3);
    EXPECT_EQ(h1->deleted_tuple_count, 0);

    ASSERT_TRUE(bool(table_page_->MarkDelTuple(r2.value())));
    const auto* h2 = table_page_->HeaderData();
    EXPECT_EQ(h2->slot_count, 3);
    EXPECT_EQ(h2->alive_tuple_count, 2);
    EXPECT_EQ(h2->deleted_tuple_count, 1);

    auto reused = table_page_->InsertTuple(MakeTupleFromString("tuple_reuse"));
    ASSERT_TRUE(bool(reused));
    EXPECT_EQ(reused.value(), r2.value());

    const auto* h3 = table_page_->HeaderData();
    EXPECT_EQ(h3->slot_count, 3);
    EXPECT_EQ(h3->alive_tuple_count, 3);
    EXPECT_EQ(h3->deleted_tuple_count, 0);
}

TEST_F(TablePageTest, LegacyCounterFallbackAndRepair) {
    auto r1 = table_page_->InsertTuple(MakeTupleFromString("alpha"));
    auto r2 = table_page_->InsertTuple(MakeTupleFromString("beta"));
    ASSERT_TRUE(bool(r1));
    ASSERT_TRUE(bool(r2));
    ASSERT_TRUE(bool(table_page_->MarkDelTuple(r1.value())));

    auto* header = table_page_->HeaderData();
    header->alive_tuple_count = 0;
    header->deleted_tuple_count = 0;

    EXPECT_FALSE(table_page_->TupleCountersConsistent());
    EXPECT_EQ(table_page_->AliveTupleCount(), 1);
    EXPECT_EQ(table_page_->DeletedTupleCount(), 1);

    ASSERT_TRUE(bool(table_page_->MarkDelTuple(r2.value())));
    EXPECT_TRUE(table_page_->TupleCountersConsistent());
    EXPECT_EQ(header->alive_tuple_count, 0);
    EXPECT_EQ(header->deleted_tuple_count, header->slot_count);
}
