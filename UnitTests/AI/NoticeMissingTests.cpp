#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "Assets/AssetManager.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

#include <gtest/gtest.h>

#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    Terrain::HeightMap makeFlat(int samples, float y)
    {
        Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }

    struct Yard
    {
        World              world;
        AssetManager       assets;
        AssetPinTable      pins;
        AiSystem           ai;
        Terrain::TerrainGrid terrain;

        bool setup()
        {
            if (!terrain.createFromHeightMap(makeFlat(65, 0.0f), 64))
                return false;
            AI::WalkabilityDesc walk;
            walk.heightMap   = &terrain.coarse();
            walk.waterLevel  = -100.0f;
            walk.agentRadius = 0.8f;
            if (!ai.bake(walk))
                return false;
            AI::AwarenessSettings settings;
            settings.bodyRange = 1.0f;
            ai.setAwarenessSettings(settings);
            ai.setAwarenessEnabled(true);
            return ai.awarenessSettings().bodyRange == 1.0f;
        }

        Entity spawn(const Vector3f& pos, uint32_t squad)
        {
            TransformComponent xf{};
            xf.position = pos;
            Entity e    = ai.spawnHunter(world, pins, assets, xf);
            if (!e.valid())
                return {};
            if (TransformComponent* live = world.get<TransformComponent>(e))
                live->position = pos;
            AiAgentComponent* agent = world.get<AiAgentComponent>(e);
            if (!agent)
                return {};
            agent->squad            = squad;
            agent->lastAlivePos     = pos;
            agent->missingReported  = false;
            agent->reportMissing    = false;
            agent->bodyFound        = false;
            agent->aware.alertness  = AI::Alertness::Calm;
            return e;
        }

        void kill(Entity e)
        {
            HealthComponent* hp = world.get<HealthComponent>(e);
            if (hp)
                hp->health.applyDamage(1000.0f);
            if (AiAgentComponent* agent = world.get<AiAgentComponent>(e))
                agent->deadFor = 30.0f;
        }

        void tick()
        {
            ai.tickHunters(world, terrain, false, 0.01f, Entity{});
        }
    };

} // namespace

TEST(HideSearch, SpotPast31IsNearestAndBit31StillCounts)
{
    std::vector<Vector3f> spots(41, Vector3f{ 100.0f, 0.0f, 100.0f });
    spots[31] = Vector3f{ 3.0f, 0.0f, 0.0f };
    spots[40] = Vector3f{ 0.0f, 0.0f, 0.0f };
    std::vector<uint64_t> bits;
    EXPECT_EQ(nearestUncheckedHide(spots, bits, Vector3f::ZERO, 12.0f * 12.0f), 40);

    for (size_t i = 0; i < 32; ++i)
        markHideSpotChecked(bits, i);
    EXPECT_TRUE(hideSpotChecked(bits, 31));
    EXPECT_FALSE(hideSpotChecked(bits, 32));
    EXPECT_FALSE(hideSpotChecked(bits, 40));
    EXPECT_EQ(nearestUncheckedHide(spots, bits, Vector3f::ZERO, 12.0f * 12.0f), 40);

    bits.clear();
    markHideSpotChecked(bits, 40);
    EXPECT_EQ(nearestUncheckedHide(spots, bits, Vector3f::ZERO, 12.0f * 12.0f), 31);

    std::vector<Vector3f> edge{ Vector3f{ 12.0f, 0.0f, 0.0f } };
    EXPECT_EQ(nearestUncheckedHide(edge, {}, Vector3f::ZERO, 12.0f * 12.0f), -1);
}

TEST(NoticeMissing, NearerSquadMateBeatsALaterOne)
{
    Yard yard;
    ASSERT_TRUE(yard.setup());
    const Vector3f downPos{ 16.0f, 1.0f, 16.0f };
    const Vector3f nearPos{ 24.0f, 1.0f, 16.0f };
    const Vector3f distantPos{ 16.0f, 1.0f, 40.0f };
    const Entity down    = yard.spawn(downPos, 4);
    const Entity closeMate = yard.spawn(nearPos, 4);
    const Entity distant = yard.spawn(distantPos, 4);
    ASSERT_TRUE(down.valid());
    ASSERT_TRUE(closeMate.valid());
    ASSERT_TRUE(distant.valid());
    yard.kill(down);

    yard.tick();

    const AiAgentComponent* dead = yard.world.get<AiAgentComponent>(down);
    const AiAgentComponent* nai  = yard.world.get<AiAgentComponent>(closeMate);
    const AiAgentComponent* dai  = yard.world.get<AiAgentComponent>(distant);
    ASSERT_NE(dead, nullptr);
    ASSERT_NE(nai, nullptr);
    ASSERT_NE(dai, nullptr);
    EXPECT_FALSE(dead->bodyFound);
    EXPECT_TRUE(dead->missingReported);
    EXPECT_TRUE(nai->reportMissing);
    EXPECT_EQ(nai->aware.alertness, AI::Alertness::Investigating);
    EXPECT_FALSE(dai->reportMissing);
    EXPECT_EQ(dai->aware.alertness, AI::Alertness::Calm);
}

TEST(NoticeMissing, NobodyNearbyCanReportOnALaterTick)
{
    Yard yard;
    ASSERT_TRUE(yard.setup());
    const Entity down = yard.spawn(Vector3f{ 8.0f, 1.0f, 8.0f }, 0);
    // Squad mates are accepted at any distance. This mate is unaffiliated, so the 40 m gate applies.
    const Entity mate = yard.spawn(Vector3f{ 8.0f, 1.0f, 60.0f }, 0);
    ASSERT_TRUE(down.valid());
    ASSERT_TRUE(mate.valid());
    yard.kill(down);

    yard.tick();
    EXPECT_FALSE(yard.world.get<AiAgentComponent>(down)->missingReported);
    EXPECT_FALSE(yard.world.get<AiAgentComponent>(down)->bodyFound);
    EXPECT_FALSE(yard.world.get<AiAgentComponent>(mate)->reportMissing);

    yard.world.get<TransformComponent>(mate)->position = Vector3f{ 8.0f, 1.0f, 30.0f };
    yard.tick();
    EXPECT_TRUE(yard.world.get<AiAgentComponent>(down)->missingReported);
    EXPECT_TRUE(yard.world.get<AiAgentComponent>(mate)->reportMissing);
    EXPECT_EQ(yard.world.get<AiAgentComponent>(mate)->aware.alertness, AI::Alertness::Investigating);
}
