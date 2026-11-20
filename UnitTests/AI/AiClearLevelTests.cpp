#include "AI/AiSystem.h"
#include "AI/Walkability.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"
#include "Terrain/HeightMap.h"

#include <gtest/gtest.h>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    Terrain::HeightMap makeFlat(int samples)
    {
        Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        return hm;
    }

} // namespace

TEST(AiClearLevel, DropsBakeAndQueuedLevelState)
{
    Terrain::HeightMap  hm = makeFlat(9);
    AiSystem            ai;
    AI::WalkabilityDesc desc;
    desc.heightMap  = &hm;
    desc.waterLevel = -10.0f;
    ASSERT_TRUE(ai.bake(desc));
    ASSERT_TRUE(ai.walkability().valid());
    const uint32_t serial = ai.walkability().bakeSerial();

    World world;
    ai.addNoise(Vector3f{ 1.0f, 0.0f, 1.0f }, 10.0f);
    ai.setJumpAttackToken(world.createEntity());
    ai.setSimTime(12.0f);
    ASSERT_EQ(ai.noiseCount(), 1);

    ai.clearLevel();
    EXPECT_FALSE(ai.walkability().valid());
    EXPECT_EQ(ai.walkability().heightMap(), nullptr);
    EXPECT_NE(ai.walkability().bakeSerial(), serial);
    EXPECT_EQ(ai.noiseCount(), 0);
    EXPECT_FALSE(ai.jumpAttackToken().valid());
    EXPECT_FLOAT_EQ(ai.simTime(), 0.0f);

    // The next level bakes as usual.
    ASSERT_TRUE(ai.bake(desc));
    EXPECT_TRUE(ai.walkability().valid());
}

TEST(Walkability, FailedBakeInvalidatesOldBake)
{
    Terrain::HeightMap  hm = makeFlat(5);
    AI::Walkability     walk;
    AI::WalkabilityDesc desc;
    desc.heightMap  = &hm;
    desc.waterLevel = -10.0f;
    ASSERT_TRUE(walk.bake(desc));
    const uint32_t      serial = walk.bakeSerial();
    AI::WalkabilityDesc bad;
    EXPECT_FALSE(walk.bake(bad));
    EXPECT_FALSE(walk.valid());
    EXPECT_NE(walk.bakeSerial(), serial);
}
