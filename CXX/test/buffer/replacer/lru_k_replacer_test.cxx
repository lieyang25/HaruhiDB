/**
 * CXX/test/buffer/replacer/lru_k_replacer_test.cxx
 */
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
//
    // LRU-K Core Logic Tests
    //

    TEST_F(LruKReplacerTest, InfDistancePriorityTest) {
        // 验证：访问次数少于 K 的 frame（Inf距离）优先级高于访问次数满 K 的 frame
        // K = 3
        LruKReplacer r(10, 3);

        // Frame 1: 访问 2 次 ( < 3, 属于 Inf 距离)
        r.RecordAccess(FID(1));
        r.RecordAccess(FID(1));
        r.SetEvictable(FID(1), true);

        // Frame 2: 访问 3 次 ( >= 3, 属于有限距离)
        // 即使它的最近访问时间比 Frame 1 更晚，也应该先淘汰 Frame 1
        r.RecordAccess(FID(2));
        r.RecordAccess(FID(2));
        r.RecordAccess(FID(2));
        r.SetEvictable(FID(2), true);

        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));
        EXPECT_EQ(victim, FID(1)); // 应当先淘汰访问次数不足 K 的
    }

    TEST_F(LruKReplacerTest, InfDistanceLruTieBreakTest) {
        // 验证：当多个 frame 都是 Inf 距离时，按最早访问时间（LRU）淘汰
        LruKReplacer r(10, 2);

        // Frame 1 最早访问
        r.RecordAccess(FID(1));
        // Frame 2 稍后访问
        r.RecordAccess(FID(2));

        r.SetEvictable(FID(1), true);
        r.SetEvictable(FID(2), true);

        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));
        EXPECT_EQ(victim, FID(1)); // 1 比 2 更早进入系统
    }

    TEST_F(LruKReplacerTest, KDistanceComparisonTest) {
        // 验证：当所有 frame 访问都满 K 次时，比较第 K 次访问的时间戳
        // 算法应当选择 $t_{k}(p)$ 最小（即最久远）的 frame
        LruKReplacer r(10, 2);

        // Frame 1: 访问时间线 [10, 100] -> 第 2 次访问是 10
        r.RecordAccess(FID(1)); // t=10 (假设)
        r.RecordAccess(FID(1)); // t=100

        // Frame 2: 访问时间线 [20, 30] -> 第 2 次访问是 20
        r.RecordAccess(FID(2)); // t=20
        r.RecordAccess(FID(2)); // t=30

        r.SetEvictable(FID(1), true);
        r.SetEvictable(FID(2), true);

        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));
        // 比较的是倒数第 K 次访问：Frame 1 的倒数第2次是 t=10，Frame 2 是 t=20
        // 10 < 20，所以 Frame 1 的 backward K-distance 更大
        EXPECT_EQ(victim, FID(1));
    }

    TEST_F(LruKReplacerTest, RemoveResetsHistoryTest) {
        // 验证：Remove 操作会清除该 frame 的所有访问历史
        LruKReplacer r(10, 2);

        // 让 Frame 1 获得“由于访问次数多而更安全”的地位
        r.RecordAccess(FID(1));
        r.RecordAccess(FID(1));
        r.RecordAccess(FID(1)); 

        r.Remove(FID(1)); // 彻底移除

        // 重新加入系统，此时它应该被视为全新 frame（0次访问历史）
        r.RecordAccess(FID(1));
        r.SetEvictable(FID(1), true);

        // 加入另一个访问了 2 次的 Frame 2
        r.RecordAccess(FID(2));
        r.RecordAccess(FID(2));
        r.SetEvictable(FID(2), true);

        frame_id_t victim;
        ASSERT_TRUE(r.Victim(victim));
        // 如果历史没清空，1 访问了 4 次应比 2 安全；
        // 如果历史清空了，1 只有 1 次访问 (Inf距离)，应先被淘汰。
        EXPECT_EQ(victim, FID(1));
    }

    TEST_F(LruKReplacerTest, HeavyConcurrencyStressTest) {
        // 压力测试：多线程频繁存取、设置状态与淘汰
        constexpr int num_threads = 10;
        constexpr int num_ops = 1000;
        LruKReplacer r(100, 2);

        std::vector<std::thread> workers;
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([&r, i]() {
                for (int j = 0; j < num_ops; ++j) {
                    frame_id_t fid = (i * 10 + j % 10);
                    r.RecordAccess(fid);
                    r.SetEvictable(fid, (j % 2 == 0));
                    if (j % 5 == 0) {
                        frame_id_t v;
                        r.Victim(v);
                    }
                }
            });
        }
        for (auto &t : workers) t.join();
        // 只要不 crash 且 latch_ 保护正常，即通过基本并发测试
        SUCCEED();
    }
} // namespace replacer
} // namespace HaruhiDB
