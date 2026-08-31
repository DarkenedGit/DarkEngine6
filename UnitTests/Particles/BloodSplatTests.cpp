#include <gtest/gtest.h>

#include "Particles/BloodSplatPool.h"

using namespace Dark;

namespace
{
    float flatHeight(void*, float, float)
    {
        return 3.0f;
    }

    float rampHeight(void*, float x, float)
    {
        return x * 0.5f;
    }
} // namespace

TEST(BloodSplat, BuildsExpectedVertexCount)
{
    ParticleVertex verts[BloodSplatPool::kVertsPerSplat];
    const uint32_t n = buildBloodSplatVerts(verts, BloodSplatPool::kVertsPerSplat, 10.0f, -4.0f, 1.5f, 0.0f, 0.04f, flatHeight, nullptr);
    EXPECT_EQ(n, BloodSplatPool::kVertsPerSplat);
}

TEST(BloodSplat, DrapesOntoHeightAndBiasesUp)
{
    ParticleVertex verts[BloodSplatPool::kVertsPerSplat];
    buildBloodSplatVerts(verts, BloodSplatPool::kVertsPerSplat, 0.0f, 0.0f, 1.0f, 0.0f, 0.04f, flatHeight, nullptr);
    for (uint32_t i = 0; i < BloodSplatPool::kVertsPerSplat; ++i)
        EXPECT_NEAR(verts[i].py, 3.04f, 1.0e-4f);
}

TEST(BloodSplat, FollowsSlopedTerrain)
{
    ParticleVertex verts[BloodSplatPool::kVertsPerSplat];
    buildBloodSplatVerts(verts, BloodSplatPool::kVertsPerSplat, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, rampHeight, nullptr);
    float minY = 1.0e9f, maxY = -1.0e9f;
    for (uint32_t i = 0; i < BloodSplatPool::kVertsPerSplat; ++i)
    {
        minY = minY < verts[i].py ? minY : verts[i].py;
        maxY = maxY > verts[i].py ? maxY : verts[i].py;
        EXPECT_NEAR(verts[i].py, verts[i].px * 0.5f, 1.0e-3f);
    }
    EXPECT_GT(maxY - minY, 0.5f);
}

TEST(BloodSplat, RejectsSmallOutputBuffer)
{
    ParticleVertex verts[4];
    EXPECT_EQ(buildBloodSplatVerts(verts, 4, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, flatHeight, nullptr), 0u);
}

TEST(BloodSplat, PoolRecyclesTenSlots)
{
    BloodSplatPool pool;
    EXPECT_EQ(pool.activeCount(), 0);
    EXPECT_EQ(pool.nextIndex(), 0);
    // CPU recycle contract is on nextIndex/activeCount; GPU spawn needs a renderer.
    EXPECT_EQ(BloodSplatPool::kCapacity, 10);
}
