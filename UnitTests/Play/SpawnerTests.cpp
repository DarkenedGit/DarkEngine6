#include <gtest/gtest.h>

#include "Play/Spawner.h"

#include <cmath>

using namespace Dark;

namespace
{

bool insideDisc(float x, float z, float ox, float oz, float radius)
{
    const float dx = x - ox;
    const float dz = z - oz;
    return dx * dx + dz * dz <= radius * radius + 1.0e-4f;
}

void fillAlive(bool alive[kSpawnerSlotCount], bool value)
{
    for (int i = 0; i < kSpawnerSlotCount; ++i)
        alive[i] = value;
}

} // namespace

TEST(Spawner, MaintainFillsThenReplacesAfterDelay)
{
    SpawnerDesc desc{};
    desc.mode         = SpawnerMode::Maintain;
    desc.count        = 2;
    desc.radius       = 0.0f;
    desc.respawnDelay = 1.5f;
    desc.enabled      = false;
    desc.seed         = 7;
    SpawnerRuntime runtime{};
    bool alive[kSpawnerSlotCount]{};
    SpawnerRequest reqs[kSpawnerSlotCount];

    EXPECT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 1.0f, alive, reqs, kSpawnerSlotCount), 0);

    desc.enabled = true;
    ASSERT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 2);
    EXPECT_EQ(reqs[0].slot, 0);
    EXPECT_EQ(reqs[1].slot, 1);
    EXPECT_EQ(reqs[0].unit, SpawnerUnit::Hunter);
    EXPECT_NEAR(reqs[0].x, 3.0f, 1.0e-4f);
    EXPECT_NEAR(reqs[0].z, 4.0f, 1.0e-4f);
    EXPECT_TRUE(runtime.slots[0].tracked);
    EXPECT_TRUE(runtime.slots[1].tracked);

    fillAlive(alive, true);
    EXPECT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 1.0f, alive, reqs, kSpawnerSlotCount), 0);

    alive[0] = false;
    EXPECT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 0.25f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_FALSE(runtime.slots[0].tracked);
    EXPECT_NEAR(runtime.slots[0].cooldown, 1.5f, 1.0e-4f);

    EXPECT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 1.4f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_GT(runtime.slots[0].cooldown, 0.0f);

    ASSERT_EQ(tickSpawner(desc, runtime, 3.0f, 4.0f, 0.1f, alive, reqs, kSpawnerSlotCount), 1);
    EXPECT_EQ(reqs[0].slot, 0);
    EXPECT_TRUE(runtime.slots[0].tracked);
    EXPECT_TRUE(runtime.slots[1].tracked);
}

TEST(Spawner, WaveWaitsUntilCleared)
{
    SpawnerDesc desc{};
    desc.mode     = SpawnerMode::Waves;
    desc.gate     = SpawnerWaveGate::Cleared;
    desc.count    = 2;
    desc.maxWaves = 2;
    desc.radius   = 0.0f;
    SpawnerRuntime runtime{};
    bool alive[kSpawnerSlotCount]{};
    SpawnerRequest reqs[kSpawnerSlotCount];

    ASSERT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 2);
    EXPECT_EQ(runtime.wavesSpawned, 1);

    fillAlive(alive, true);
    EXPECT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 5.0f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_EQ(runtime.wavesSpawned, 1);

    alive[0] = false;
    EXPECT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 1.0f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_EQ(runtime.wavesSpawned, 1);

    alive[1] = false;
    ASSERT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 2);
    EXPECT_EQ(runtime.wavesSpawned, 2);

    fillAlive(alive, true);
    EXPECT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 1.0f, alive, reqs, kSpawnerSlotCount), 0);
    fillAlive(alive, false);
    EXPECT_EQ(tickSpawner(desc, runtime, 0.0f, 0.0f, 1.0f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_EQ(runtime.wavesSpawned, 2);
}

TEST(Spawner, WaveWaitsForIntervalWhileUnitsLive)
{
    SpawnerDesc desc{};
    desc.mode         = SpawnerMode::Waves;
    desc.gate         = SpawnerWaveGate::Time;
    desc.count        = 1;
    desc.waveInterval = 4.0f;
    desc.radius       = 0.0f;
    desc.unit         = SpawnerUnit::Wolf;
    SpawnerRuntime runtime{};
    bool alive[kSpawnerSlotCount]{};
    SpawnerRequest reqs[kSpawnerSlotCount];

    ASSERT_EQ(tickSpawner(desc, runtime, 1.0f, 2.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 1);
    EXPECT_EQ(reqs[0].unit, SpawnerUnit::Wolf);
    EXPECT_EQ(runtime.wavesSpawned, 1);

    fillAlive(alive, true);
    EXPECT_EQ(tickSpawner(desc, runtime, 1.0f, 2.0f, 3.9f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_EQ(runtime.wavesSpawned, 1);

    ASSERT_EQ(tickSpawner(desc, runtime, 1.0f, 2.0f, 0.1f, alive, reqs, kSpawnerSlotCount), 1);
    EXPECT_EQ(runtime.wavesSpawned, 2);
    EXPECT_TRUE(alive[0]);

    SpawnerDesc held = desc;
    held.waveInterval = 0.0f;
    SpawnerRuntime once{};
    fillAlive(alive, false);
    ASSERT_EQ(tickSpawner(held, once, 0.0f, 0.0f, 0.0f, alive, reqs, kSpawnerSlotCount), 1);
    fillAlive(alive, true);
    EXPECT_EQ(tickSpawner(held, once, 0.0f, 0.0f, 30.0f, alive, reqs, kSpawnerSlotCount), 0);
    EXPECT_EQ(once.wavesSpawned, 1);
}

TEST(Spawner, DiscStaysInsideRadiusAndIsStable)
{
    SpawnerDesc desc{};
    desc.mode   = SpawnerMode::Maintain;
    desc.count  = 6;
    desc.radius = 5.0f;
    desc.seed   = 42;
    SpawnerRuntime a{};
    SpawnerRuntime b{};
    bool alive[kSpawnerSlotCount]{};
    SpawnerRequest first[kSpawnerSlotCount];
    SpawnerRequest second[kSpawnerSlotCount];
    ASSERT_EQ(tickSpawner(desc, a, 10.0f, -4.0f, 0.0f, alive, first, kSpawnerSlotCount), 6);
    ASSERT_EQ(tickSpawner(desc, b, 10.0f, -4.0f, 0.0f, alive, second, kSpawnerSlotCount), 6);

    bool differ = false;
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_TRUE(insideDisc(first[i].x, first[i].z, 10.0f, -4.0f, 5.0f));
        EXPECT_EQ(first[i].seed, 42u);
        EXPECT_NEAR(first[i].x, second[i].x, 1.0e-5f);
        EXPECT_NEAR(first[i].z, second[i].z, 1.0e-5f);
        if (first[i].x != first[0].x || first[i].z != first[0].z)
            differ = true;
    }
    EXPECT_TRUE(differ);
}
