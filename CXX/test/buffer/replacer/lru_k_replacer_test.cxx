/**
 * CXX/test/buffer/replacer/lru_k_replacer_test.cxx
 */
    // test/replacer/lru_k_replacer_test.cpp
//
// Comprehensive GoogleTest suite for HaruhiDB::replacer::LruKReplacer.
//
// Test strategy:
//
// 1. Each public method gets at least two focused tests.
// 2. Many tests chain multiple method calls to verify interactions (RecordAccess -> SetEvictable -> Victim -> Remove -> Size).
// 3. Concurrent RecordAccess is exercised to detect basic thread-safety regressions.
// 4. Tests avoid depending on private implementation details; they assert observable invariants.
//
// Note: comments are in English as requested.

#include "gtest/gtest.h"

#include "buffer/replacer/lru_k_replacer.h"
#include "common/config.h" // make sure this path matches your project layout

#include <thread>
#include <vector>
#include <set>
#include <atomic>
#include <algorithm>
namespace HaruhiDB
{
namespace replacer
{
    static frame_id_t FID(int x) {
        return static_cast<frame_id_t>(x);
    }

    // Helper: repeatedly call Victim until none left, collect victims into vector.
    static std::vector<frame_id_t> DrainAllVictims(LruKReplacer &replacer) {
        std::vector<frame_id_t> victims;
        frame_id_t fid;
        while (replacer.Victim(fid)) {
            victims.push_back(fid);
        }
        return victims;
    }

    /*
    * Test fixture to centralize setup for multiple tests.
    * Each test creates its own replacer instance to avoid cross-test interference.
    */
    class LruKReplacerTest : public ::testing::Test {
    protected:
        void TearDown() override {
            // no-op
        }
    };

    //
    // Constructor / Size tests
    //

    TEST_F(LruKReplacerTest, ConstructorInitializesEmpty) {
        // Verify constructor sets up an empty replacer (Size == 0).
        LruKReplacer r(8, 2);
        EXPECT_EQ(r.Size(), 0u);
    }

    TEST_F(LruKReplacerTest, ConstructorWithDifferentKAndPool) {
        // Construct with different k and pool size; should not throw and size remains 0.
        LruKReplacer r1(1, 1);
        EXPECT_EQ(r1.Size(), 0u);

        LruKReplacer r2(16, 5);
        EXPECT_EQ(r2.Size(), 0u);
    }

    //
    // RecordAccess tests (at least two cases)
    //

    TEST_F(LruKReplacerTest, RecordAccessThenSetEvictableIncreasesSize) {
        // RecordAccess should not change evictable size until SetEvictable(true) is called.
        LruKReplacer r(4, 2);

        r.RecordAccess(FID(0));
        EXPECT_EQ(r.Size(), 0u); // not evictable yet

        r.SetEvictable(FID(0), true);
        EXPECT_EQ(r.Size(), 1u);

        // Re-recording access should not change the evictable count.
        r.RecordAccess(FID(0));
        EXPECT_EQ(r.Size(), 1u);
    }

    TEST_F(LruKReplacerTest, MultipleRecordAccessesMaintainCorrectSize) {
        // Multiple distinct frames recorded and set evictable should reflect in Size().
        LruKReplacer r(10, 3);

        for (int i = 0; i < 5; ++i) {
            r.RecordAccess(FID(i));
            r.SetEvictable(FID(i), true);
        }
        EXPECT_EQ(r.Size(), 5u);

        // Access some frames again; size stays the same.
        r.RecordAccess(FID(2));
        r.RecordAccess(FID(4));
        EXPECT_EQ(r.Size(), 5u);
    }

    TEST_F(LruKReplacerTest, RecordAccessConcurrencyBasicSafety) {
        // Spawn several threads that perform RecordAccess concurrently,
        // then mark them evictable and ensure Size() matches distinct frames.
        constexpr int nthreads = 8;
        constexpr int frames_per_thread = 20;
        LruKReplacer r(nthreads * frames_per_thread, 2);

        std::vector<std::thread> ths;
        for (int t = 0; t < nthreads; ++t) {
            ths.emplace_back([t, frames_per_thread, &r]() {
                int base = t * frames_per_thread;
                for (int i = 0; i < frames_per_thread; ++i) {
                    r.RecordAccess(FID(base + i));
                }
            });
        }
        for (auto &th : ths) th.join();

        // Now set all those frames evictable and check size.
        for (int i = 0; i < nthreads * frames_per_thread; ++i) {
            r.SetEvictable(FID(i), true);
        }
        EXPECT_EQ(r.Size(), static_cast<size_t>(nthreads * frames_per_thread));
    }

    //
    // SetEvictable tests (at least two cases)
    //

    TEST_F(LruKReplacerTest, SetEvictableToggleAffectsSize) {
        // Turning evictable on increases Size, turning it off decreases Size.
        LruKReplacer r(4, 2);

        r.RecordAccess(FID(1));
        r.SetEvictable(FID(1), true);
        EXPECT_EQ(r.Size(), 1u);

        r.SetEvictable(FID(1), false);
        EXPECT_EQ(r.Size(), 0u);

        r.SetEvictable(FID(1), true);
        EXPECT_EQ(r.Size(), 1u);
    }

    TEST_F(LruKReplacerTest, SetEvictableOnUnknownFrameAddsIt) {
        // If a previously unseen frame is SetEvictable(true), it should be tracked and increase Size.
        LruKReplacer r(6, 2);

        // Do not call RecordAccess first; directly mark evictable.
        r.SetEvictable(FID(3), true);
        EXPECT_EQ(r.Size(), 1u);

        // Make it non-evictable again.
        r.SetEvictable(FID(3), false);
        EXPECT_EQ(r.Size(), 0u);
    }

    //
    // Victim tests (at least two cases)
    //

    TEST_F(LruKReplacerTest, VictimReturnsFalseWhenNoEvictable) {
        // When no frames are evictable, Victim should return false.
        LruKReplacer r(3, 2);

        r.RecordAccess(FID(0));
        r.RecordAccess(FID(1));
        // No SetEvictable(true) called => Size() == 0
        EXPECT_EQ(r.Size(), 0u);

        frame_id_t victim;
        EXPECT_FALSE(r.Victim(victim));
    }

    TEST_F(LruKReplacerTest, VictimEvictsFramesAndReducesSize) {
        // Add several evictable frames, then call Victim repeatedly and ensure unique victims
        // are returned until none remain.
        LruKReplacer r(5, 2);

        std::set<frame_id_t> expected;
        for (int i = 0; i < 4; ++i) {
            r.RecordAccess(FID(i));
            r.SetEvictable(FID(i), true);
            expected.insert(FID(i));
        }
        EXPECT_EQ(r.Size(), 4u);

        std::vector<frame_id_t> victims;
        frame_id_t fid;
        while (r.Victim(fid)) {
            victims.push_back(fid);
        }

        // All victims must have been among the expected set, and count should match.
        EXPECT_EQ(victims.size(), 4u);
        for (auto v : victims) {
            EXPECT_TRUE(expected.find(v) != expected.end());
            // Each victim should no longer be present (subsequent Size uses already tested)
        }
        EXPECT_EQ(r.Size(), 0u);
    }

    //
    // Remove tests (at least two cases)
    //

    TEST_F(LruKReplacerTest, RemoveEvictableFrameDecreasesSize)
    {
        // pool_size = 4 -> valid frame_id: 0..3
        LruKReplacer r(4, 2);

        frame_id_t fid = 1;

        // Access the frame so it exists in replacer
        r.RecordAccess(fid);

        // Make it evictable
        r.SetEvictable(fid, true);

        // Verify size increased
        EXPECT_EQ(r.Size(), 1u);

        // Remove the frame
        r.Remove(fid);

        // Evictable size should decrease
        EXPECT_EQ(r.Size(), 0u);

        // Victim should now fail since nothing is evictable
        frame_id_t victim;
        EXPECT_FALSE(r.Victim(victim));

        // Removing again should be safe (should not crash or change state)
        r.Remove(fid);

        EXPECT_EQ(r.Size(), 0u);
    }

    TEST_F(LruKReplacerTest, RemoveNonEvictableFrameNoEffectOnSize) {
        // Removing a non-evictable frame should not change evictable size.
        LruKReplacer r(4, 2);
        r.RecordAccess(FID(20));
        // not set evictable
        EXPECT_EQ(r.Size(), 0u);

        r.Remove(FID(20));
        EXPECT_EQ(r.Size(), 0u);
    }

    TEST_F(LruKReplacerTest, RemoveAfterVictimDoesNotReappear) {
        // After a frame is returned by Victim, Remove on it should be safe and size unaffected.
        LruKReplacer r(3, 2);
        r.RecordAccess(FID(1));
        r.SetEvictable(FID(1), true);

        frame_id_t v;
        ASSERT_TRUE(r.Victim(v));
        // v should equal 1 (or some tracked id). Now Remove should not change size (already removed).
        r.Remove(v);
        EXPECT_EQ(r.Size(), 0u);
    }

    //
    // Interaction / Integration tests (chain operations, at least two cases)
    //

    TEST_F(LruKReplacerTest, ChainAccessSetEvictableVictimAndReuse) {
        // Full interaction chain: access frames, mark evictable, evict one, then reuse a freed frame id.
        LruKReplacer r(5, 2);

        // Access and mark frames 0,1,2 evictable.
        for (int i = 0; i < 3; ++i) {
            r.RecordAccess(FID(i));
            r.SetEvictable(FID(i), true);
        }
        EXPECT_EQ(r.Size(), 3u);

        // Evict one frame.
        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));
        EXPECT_GE(static_cast<int>(victim), 0);

        // After eviction, size decreases.
        EXPECT_EQ(r.Size(), 2u);

        // Reuse the victim id: record access and set evictable again.
        r.RecordAccess(victim);
        r.SetEvictable(victim, true);
        EXPECT_EQ(r.Size(), 3u);

        // Drain remaining victims to ensure no crash and correct final size.
        auto all = DrainAllVictims(r);
        EXPECT_EQ(r.Size(), 0u);
        // The re-used frame should appear among final victims.
        EXPECT_TRUE(std::find(all.begin(), all.end(), victim) != all.end());
    }

    TEST_F(LruKReplacerTest, ComplexSequenceTogglesAndEvictions)
    {
        LruKReplacer r(6, 3);

        // Step 1: access several frames
        for (frame_id_t i = 0; i < 5; i++)
        {
            r.RecordAccess(i);
        }

        // Step 2: mark some frames evictable
        r.SetEvictable(0, true);
        r.SetEvictable(1, true);

        EXPECT_EQ(r.Size(), 2u);

        // Step 3: re-access frame 1 to increase recency
        r.RecordAccess(1);
        r.RecordAccess(1);

        // Step 4: add another evictable frame
        r.SetEvictable(2, true);

        EXPECT_EQ(r.Size(), 3u);

        // Step 5: victim selection
        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));

        // Victim must be from the evictable set
        std::set<frame_id_t> possible = {0, 1, 2};
        EXPECT_TRUE(possible.count(victim));

        // After eviction, size decreases
        EXPECT_EQ(r.Size(), 2u);

        // Step 6: toggle evictable state
        r.SetEvictable(0, false);

        EXPECT_LE(r.Size(), 2u);

        // Step 7: drain remaining victims
        std::vector<frame_id_t> victims;

        frame_id_t v;
        while (r.Victim(v))
        {
            victims.push_back(v);
        }

        // After draining, replacer must be empty
        EXPECT_EQ(r.Size(), 0u);

        // Victims must all come from the original evictable frames
        for (auto id : victims)
        {
            EXPECT_TRUE(id == 0 || id == 1 || id == 2);
        }
    }

} // namespace replacer
} // namespace HaruhiDB
