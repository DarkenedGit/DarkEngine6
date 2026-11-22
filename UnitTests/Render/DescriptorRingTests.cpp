#include "Render/DescriptorRing.h"

#include <gtest/gtest.h>

#include <cstdint>

// Phase 6 (plan §3.9): the shader-visible descriptor ring's persistent region and per-frame ring.

using namespace Dark;

TEST(DescriptorRing, InitSplitsTheHeap)
{
    DescriptorRing ring;
    EXPECT_FALSE(ring.init(64, 64)); // no frame region
    ASSERT_TRUE(ring.init(256, 64));
    EXPECT_EQ(ring.capacity(), 256u);
    EXPECT_EQ(ring.persistentCount(), 64u);
    EXPECT_EQ(ring.frameCount(), 192u);
    EXPECT_EQ(ring.persistentFree(), 64u);
}

TEST(DescriptorRing, PersistentFirstFitAndMergeOnFree)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(256, 16));
    const uint32_t a = ring.allocatePersistent(4);
    const uint32_t b = ring.allocatePersistent(4);
    const uint32_t c = ring.allocatePersistent(8);
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 4u);
    EXPECT_EQ(c, 8u);
    EXPECT_EQ(ring.allocatePersistent(1), DescriptorRing::kInvalid);
    ring.freePersistent(a, 4);
    ring.freePersistent(c, 8);
    EXPECT_EQ(ring.allocatePersistent(8), 8u); // first fit: [0,4) is too small
    ring.freePersistent(8, 8);
    ring.freePersistent(b, 4); // merges [0,4) [4,8) [8,16)
    EXPECT_EQ(ring.persistentFree(), 16u);
    EXPECT_EQ(ring.allocatePersistent(16), 0u);
}

TEST(DescriptorRing, FreeOutsideThePersistentRegionIsIgnored)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(256, 16));
    ring.freePersistent(16, 4);
    ring.freePersistent(10, 8);
    EXPECT_EQ(ring.persistentFree(), 16u);
}

TEST(DescriptorRing, FrameRangesStartAfterThePersistentRegion)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(128, 32));
    EXPECT_EQ(ring.allocateFrame(8), 32u);
    EXPECT_EQ(ring.allocateFrame(8), 40u);
    EXPECT_EQ(ring.frameUsed(), 16u);
}

TEST(DescriptorRing, FramesComeBackWhenTheirFenceCompletes)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(64 + 32, 64)); // 32 frame descriptors
    EXPECT_NE(ring.allocateFrame(12), DescriptorRing::kInvalid);
    ring.endFrame(1);
    EXPECT_NE(ring.allocateFrame(12), DescriptorRing::kInvalid);
    ring.endFrame(2);
    EXPECT_EQ(ring.framesInFlight(), 2u);
    // 8 left, and a range never wraps: the 12 do not fit until frame 1 retires.
    EXPECT_EQ(ring.allocateFrame(12), DescriptorRing::kInvalid);
    EXPECT_EQ(ring.failures(), 1u);
    ring.retire(0);
    EXPECT_EQ(ring.framesInFlight(), 2u);
    ring.retire(1);
    EXPECT_EQ(ring.framesInFlight(), 1u);
    EXPECT_EQ(ring.frameUsed(), 12u);
    // Head is at 24, tail at 12: the 8 at the end are skipped and the range starts at 0.
    EXPECT_EQ(ring.allocateFrame(12), 64u);
    EXPECT_EQ(ring.frameUsed(), 12u + 8u + 12u);
    ring.endFrame(3);
    ring.retire(3);
    EXPECT_EQ(ring.frameUsed(), 0u);
    EXPECT_EQ(ring.framesInFlight(), 0u);
    EXPECT_EQ(ring.highWater(), 32u);
}

TEST(DescriptorRing, WrappedRangeDoesNotOverrunTheTail)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(32, 0)); // all frame
    ASSERT_NE(ring.allocateFrame(20), DescriptorRing::kInvalid);
    ring.endFrame(1);
    ASSERT_NE(ring.allocateFrame(10), DescriptorRing::kInvalid); // 20..30
    ring.endFrame(2);
    ring.retire(1); // tail 20, head 30
    // 2 at the end + 20 at the front are free, but a wrapped range must stay below the tail.
    EXPECT_EQ(ring.allocateFrame(21), DescriptorRing::kInvalid);
    EXPECT_EQ(ring.allocateFrame(20), 0u);
    EXPECT_EQ(ring.frameUsed(), 32u);
    EXPECT_EQ(ring.allocateFrame(1), DescriptorRing::kInvalid);
}

TEST(DescriptorRing, EmptyFrameAddsNoFence)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(32, 0));
    ring.endFrame(1);
    EXPECT_EQ(ring.framesInFlight(), 0u);
    EXPECT_EQ(ring.allocateFrame(0), DescriptorRing::kInvalid);
    EXPECT_EQ(ring.allocateFrame(33), DescriptorRing::kInvalid);
}

TEST(DescriptorRing, SteadyStateCyclesWithoutFailures)
{
    DescriptorRing ring;
    ASSERT_TRUE(ring.init(1024 + 300, 1024));
    uint64_t fence = 0;
    for (int frame = 0; frame < 1000; ++frame)
    {
        // Two frames in flight plus the one recording, 7 tables of 13 descriptors each: 273, plus
        // at most 12 skipped at the region end.
        ring.retire(fence >= 2 ? fence - 2 : 0);
        for (int t = 0; t < 7; ++t)
            ASSERT_NE(ring.allocateFrame(13), DescriptorRing::kInvalid) << "frame " << frame;
        ring.endFrame(++fence);
    }
    EXPECT_EQ(ring.failures(), 0u);
    EXPECT_LE(ring.highWater(), 300u);
}
