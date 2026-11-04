#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "AI/WaterPeek.h"
#include "Assets/AssetManager.h"
#include "Core/AssetPinTable.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Play/Facility.h"
#include "Scene/Building.h"
#include "Scene/SceneFile.h"
#include "Scene/SceneSpawn.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    // Bank at y = 1 for x < 3, canal bed at y = -2 beyond. Water surface at y = 0.
    float canalBank(void*, float x, float)
    {
        return x < 3.0f ? 1.0f : -2.0f;
    }

    float dryGround(void*, float, float)
    {
        return 1.0f;
    }

    float lakeBed(void*, float, float)
    {
        return -3.0f;
    }

    float flatGround(void*, float, float)
    {
        return 0.0f;
    }

    Entity addSearchlight(World& world, const Vector3f& pos, const Vector3f& aim, float sweep)
    {
        const Entity e = world.createEntity();
        Vector3f     dir = aim;
        dir.Normalize();
        world.emplace<TransformComponent>(e, pos, Quaternion::FromLookRotation(dir, Vector3f::Y_AXIS), Vector3f{ 1.0f, 1.0f, 1.0f });
        auto& light        = world.emplace<LocalLightComponent>(e);
        light.type         = LocalLightType::Spot;
        light.range        = 20.0f;
        light.outerConeDeg = 22.0f;
        light.intensity    = 3000.0f;
        SceneObjectData o{};
        o.type        = SceneObjectType::SpotLight;
        o.searchlight = true;
        o.sweepDeg    = sweep;
        applySceneActor(world, e, o, nullptr, nullptr);
        return e;
    }

    Entity addSwimmer(World& world, const Vector3f& pos)
    {
        const Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(e, xf);
        world.emplace<HealthComponent>(e);
        return e;
    }

    // Bank at y = 0 for x < 20, canal bed at y = -2 beyond. Water surface at y = -0.5.
    struct Canal
    {
        World                world;
        AssetManager         assets;
        AssetPinTable        pins;
        AiSystem             ai;
        Terrain::TerrainGrid terrain;
        Entity               guard{};
        Entity               player{};

        // bridge raises a deck over the canal from x = 18 to 22. The bed starts past it.
        bool setup(bool bridge = false)
        {
            Terrain::HeightMap hm;
            if (!hm.create(65, 65, 1.0f, 1.0f))
                return false;
            hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
            for (int z = 0; z < 65; ++z)
            {
                for (int x = 0; x < 65; ++x)
                {
                    float h = x >= 20 ? -2.0f : 0.0f;
                    if (bridge && x >= 18 && x < 22)
                        h = 1.5f;
                    else if (bridge && x >= 22)
                        h = -2.0f;
                    hm.setHeight(x, z, h);
                }
            }
            if (!terrain.createFromHeightMap(std::move(hm), 64))
                return false;
            AI::WalkabilityDesc walk;
            walk.heightMap   = &terrain.coarse();
            walk.waterLevel  = -0.5f;
            walk.agentRadius = 0.8f;
            if (!ai.bake(walk))
                return false;
            ai.setAwarenessEnabled(true);
            ai.setWaterLevel(-0.5f);
            TransformComponent xf{};
            xf.position = Vector3f{ 13.0f, 1.0f, 16.0f };
            guard       = ai.spawnHunter(world, pins, assets, xf);
            if (!guard.valid())
                return false;
            PatrolComponent patrol;
            patrol.points.push_back(Vector3f{ 17.0f, 1.0f, 16.0f });
            patrol.points.push_back(Vector3f{ 13.0f, 1.0f, 16.0f });
            world.emplace<PatrolComponent>(guard, std::move(patrol));
            player = addSwimmer(world, Vector3f{ 60.0f, -2.0f, 60.0f });
            return true;
        }

        void tick(bool submerged, float seconds)
        {
            for (float t = 0.0f; t < seconds; t += 0.05f)
                ai.tickHunters(world, terrain, submerged, 0.05f, player);
        }

        bool reachPeer()
        {
            for (int i = 0; i < 400; ++i)
            {
                tick(false, 0.05f);
                const PatrolComponent* patrol = world.get<PatrolComponent>(guard);
                if (patrol && patrol->peering)
                    return true;
            }
            return false;
        }
    };

    FacilityHost hostFor(World& world, AiSystem& ai, Entity player)
    {
        FacilityHost host;
        host.world    = &world;
        host.ai       = &ai;
        host.player   = player;
        host.listener = player;
        host.heightAt = flatGround;
        return host;
    }
}

TEST(CanalWatch, GuardFindsTheWaterBesideHisStop)
{
    Vector3f dir{};
    ASSERT_TRUE(AI::findWaterBeside(&canalBank, nullptr, Vector3f{ 1.5f, 1.0f, 0.0f }, 0.0f, dir));
    EXPECT_GT(dir.x, 0.9f);
    EXPECT_NEAR(dir.y, 0.0f, 1.0e-5f);
    EXPECT_FALSE(AI::findWaterBeside(&dryGround, nullptr, Vector3f{}, 0.0f, dir));
    EXPECT_FALSE(AI::findWaterBeside(&lakeBed, nullptr, Vector3f{}, 0.0f, dir));
    EXPECT_FALSE(AI::findWaterBeside(&canalBank, nullptr, Vector3f{ 1.5f, 1.0f, 0.0f }, -1.0e9f, dir));
}

TEST(CanalWatch, PeeringGuardSeesOnlyACloseShallowSwimmerInFront)
{
    const Vector3f guard{ 1.5f, 1.0f, 0.0f };
    const Vector3f east{ 1.0f, 0.0f, 0.0f };
    EXPECT_TRUE(AI::peerSpotsSwimmer(guard, east, Vector3f{ 5.0f, -1.0f, 1.0f }, 1.5f));
    EXPECT_FALSE(AI::peerSpotsSwimmer(guard, east, Vector3f{ 5.0f, -3.0f, 1.0f }, 4.0f));
    EXPECT_FALSE(AI::peerSpotsSwimmer(guard, east, Vector3f{ -4.0f, -1.0f, 0.0f }, 1.5f));
    EXPECT_FALSE(AI::peerSpotsSwimmer(guard, east, Vector3f{ 15.0f, -1.0f, 0.0f }, 1.5f));
}

TEST(CanalWatch, SearchlightTakesItsAimFromTheSpot)
{
    World        world;
    const Entity e = addSearchlight(world, Vector3f{ 0.0f, 6.0f, 0.0f }, Vector3f{ 0.0f, -1.0f, 1.0f }, 90.0f);
    const SecurityCameraComponent* cam = world.get<SecurityCameraComponent>(e);
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(cam->def.searchlight);
    EXPECT_EQ(cam->light, e);
    EXPECT_NEAR(cam->def.yaw, 0.0f, 0.5f);
    EXPECT_NEAR(cam->def.drop, 1.0f, 0.02f);
    EXPECT_FLOAT_EQ(cam->def.cone, 22.0f);
    EXPECT_FLOAT_EQ(cam->def.range, 20.0f);
    EXPECT_FLOAT_EQ(cam->def.sweep, 90.0f);
    EXPECT_FLOAT_EQ(cam->def.intensity, 3000.0f);

    SceneObjectData off{};
    off.type = SceneObjectType::SpotLight;
    applySceneActor(world, e, off, nullptr, nullptr);
    EXPECT_EQ(world.get<SecurityCameraComponent>(e), nullptr);
}

TEST(CanalWatch, SearchlightStaysOnItsMountAndResetsItsAim)
{
    World        world;
    AiSystem     ai;
    const Entity e = addSearchlight(world, Vector3f{ 4.0f, 6.0f, -2.0f }, Vector3f{ 0.0f, -1.0f, 1.0f }, 120.0f);
    const TransformComponent* before = world.get<TransformComponent>(e);
    ASSERT_NE(before, nullptr);
    const Vector3f   mount = before->position;
    const Quaternion aim   = before->rotation;
    const SecurityCameraComponent* cam = world.get<SecurityCameraComponent>(e);
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(cam->hasAuthoredPose);
    EXPECT_NEAR(cam->authoredPos.x, mount.x, 1.0e-4f);
    EXPECT_NEAR(cam->authoredPos.y, mount.y, 1.0e-4f);
    EXPECT_NEAR(cam->authoredPos.z, mount.z, 1.0e-4f);
    const float yaw  = cam->def.yaw;
    const float drop = cam->def.drop;

    FacilityState state;
    FacilityHost  host = hostFor(world, ai, Entity{});
    for (int i = 0; i < 30; ++i)
        tickFacility(state, host, 0.1f);

    const TransformComponent* swung = world.get<TransformComponent>(e);
    ASSERT_NE(swung, nullptr);
    EXPECT_NEAR(swung->position.x, mount.x, 1.0e-4f);
    EXPECT_NEAR(swung->position.y, mount.y, 1.0e-4f);
    EXPECT_NEAR(swung->position.z, mount.z, 1.0e-4f);
    const Vector3f swungAim = swung->rotation.Rotate(Vector3f::Z_AXIS);
    const Vector3f restAim  = aim.Rotate(Vector3f::Z_AXIS);
    EXPECT_GT((swungAim - restAim).Magnitude(), 0.2f);

    clearFacilitySession(state, &world, Entity{});
    const TransformComponent* restored = world.get<TransformComponent>(e);
    ASSERT_NE(restored, nullptr);
    EXPECT_NEAR(restored->position.x, mount.x, 1.0e-4f);
    EXPECT_NEAR(restored->position.y, mount.y, 1.0e-4f);
    EXPECT_NEAR(restored->position.z, mount.z, 1.0e-4f);
    EXPECT_FLOAT_EQ(restored->rotation.w, aim.w);
    EXPECT_FLOAT_EQ(restored->rotation.x, aim.x);
    EXPECT_FLOAT_EQ(restored->rotation.y, aim.y);
    EXPECT_FLOAT_EQ(restored->rotation.z, aim.z);

    SceneObjectData again{};
    again.type        = SceneObjectType::SpotLight;
    again.searchlight = true;
    again.sweepDeg    = 120.0f;
    applySceneActor(world, e, again, nullptr, nullptr);
    const SecurityCameraComponent* next = world.get<SecurityCameraComponent>(e);
    ASSERT_NE(next, nullptr);
    EXPECT_NEAR(next->def.pos.x, mount.x, 1.0e-4f);
    EXPECT_NEAR(next->def.pos.y, mount.y, 1.0e-4f);
    EXPECT_NEAR(next->def.pos.z, mount.z, 1.0e-4f);
    EXPECT_NEAR(next->def.yaw, yaw, 0.05f);
    EXPECT_NEAR(next->def.drop, drop, 0.02f);

    for (int i = 0; i < 30; ++i)
        tickFacility(state, host, 0.1f);
    clearFacilitySession(state, &world, Entity{});
    const TransformComponent* second = world.get<TransformComponent>(e);
    ASSERT_NE(second, nullptr);
    EXPECT_NEAR(second->position.x, mount.x, 1.0e-4f);
    EXPECT_NEAR(second->position.y, mount.y, 1.0e-4f);
    EXPECT_NEAR(second->position.z, mount.z, 1.0e-4f);
    EXPECT_FLOAT_EQ(second->rotation.w, aim.w);
    EXPECT_FLOAT_EQ(second->rotation.x, aim.x);
    EXPECT_FLOAT_EQ(second->rotation.y, aim.y);
    EXPECT_FLOAT_EQ(second->rotation.z, aim.z);
}

TEST(CanalWatch, SearchlightCatchesASurfaceSwimmerInItsPool)
{
    World        world;
    AiSystem     ai;
    const Entity player = addSwimmer(world, Vector3f{ 0.0f, 0.0f, 6.0f });
    addSearchlight(world, Vector3f{ 0.0f, 6.0f, 0.0f }, Vector3f{ 0.0f, -1.0f, 1.0f }, 0.0f);
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, player);
    tickFacility(state, host, 0.6f);
    EXPECT_GT(state.alarmTime, 0.0f);
}

TEST(CanalWatch, DivingUnderOrStayingOutOfThePoolHides)
{
    {
        World        world;
        AiSystem     ai;
        const Entity player = addSwimmer(world, Vector3f{ 0.0f, 0.0f, 6.0f });
        addSearchlight(world, Vector3f{ 0.0f, 6.0f, 0.0f }, Vector3f{ 0.0f, -1.0f, 1.0f }, 0.0f);
        FacilityState state;
        FacilityHost  host = hostFor(world, ai, player);
        host.submerged     = true;
        tickFacility(state, host, 0.6f);
        EXPECT_LE(state.alarmTime, 0.0f);
    }
    {
        World        world;
        AiSystem     ai;
        const Entity player = addSwimmer(world, Vector3f{ 0.0f, 0.0f, 14.0f });
        addSearchlight(world, Vector3f{ 0.0f, 6.0f, 0.0f }, Vector3f{ 0.0f, -1.0f, 1.0f }, 0.0f);
        FacilityState state;
        FacilityHost  host = hostFor(world, ai, player);
        tickFacility(state, host, 0.6f);
        EXPECT_LE(state.alarmTime, 0.0f);
    }
}

TEST(CanalWatch, SearchlightSurvivesTheSceneFile)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_searchlight";
    in.mode    = SceneMode::Scene3D;
    SceneObjectData spot{};
    spot.type        = SceneObjectType::SpotLight;
    spot.hasLight    = true;
    spot.searchlight = true;
    spot.sweepDeg    = 75.0f;
    in.objects.push_back(spot);
    SceneObjectData plain = spot;
    plain.searchlight     = false;
    in.objects.push_back(plain);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "darkengine6_searchlight_ut.json";
    std::string                 err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 2u);
    EXPECT_TRUE(out.objects[0].searchlight);
    EXPECT_FLOAT_EQ(out.objects[0].sweepDeg, 75.0f);
    EXPECT_FALSE(out.objects[1].searchlight);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(CanalWatch, GuardAtACanalStopPeersAndSpotsAShallowSwimmer)
{
    Canal canal;
    ASSERT_TRUE(canal.setup());
    ASSERT_TRUE(canal.reachPeer());
    const PatrolComponent* patrol = canal.world.get<PatrolComponent>(canal.guard);
    EXPECT_GT(patrol->peerDir.x, 0.9f);

    canal.world.get<TransformComponent>(canal.player)->position = Vector3f{ 21.0f, -1.6f, 16.0f };
    canal.ai.setPlayerDepth(1.4f);
    canal.tick(true, 3.0f);
    EXPECT_NE(canal.world.get<AiAgentComponent>(canal.guard)->aware.alertness, AI::Alertness::Calm);
}

void watchCanal(Canal& canal, const Vector3f& swimmer, float depth, const AABox3f* box, int boxCount)
{
    canal.world.get<TransformComponent>(canal.player)->position = swimmer;
    canal.ai.setPlayerDepth(depth);
    for (int i = 0; i < 60; ++i)
        canal.ai.tickHunters(canal.world, canal.terrain, true, 0.05f, canal.player, box, boxCount);
}

TEST(CanalWatch, PeeringGuardDoesNotSeeASwimmerThroughAWallOrDoor)
{
    Canal canal;
    ASSERT_TRUE(canal.setup());
    ASSERT_TRUE(canal.reachPeer());
    const AABox3f door = AABox3f::FromCenterExtents(Vector3f{ 19.0f, 1.0f, 16.0f }, Vector3f{ 0.15f, 1.4f, 2.0f });
    watchCanal(canal, Vector3f{ 21.0f, -1.6f, 16.0f }, 1.4f, &door, 1);
    EXPECT_EQ(canal.world.get<AiAgentComponent>(canal.guard)->aware.alertness, AI::Alertness::Calm);
}

TEST(CanalWatch, PeeringGuardDoesNotSeeASwimmerThroughCover)
{
    Canal canal;
    ASSERT_TRUE(canal.setup());
    ASSERT_TRUE(canal.reachPeer());
    std::vector<AABox3f> cover;
    cover.push_back(AABox3f::FromCenterExtents(Vector3f{ 19.0f, 0.6f, 16.0f }, Vector3f{ 0.4f, 1.2f, 2.0f }));
    canal.ai.setConcealVolumes(std::move(cover));
    watchCanal(canal, Vector3f{ 21.0f, -1.6f, 16.0f }, 1.4f, nullptr, 0);
    EXPECT_EQ(canal.world.get<AiAgentComponent>(canal.guard)->aware.alertness, AI::Alertness::Calm);
}

TEST(CanalWatch, PeeringGuardDoesNotSeeASwimmerUnderABridge)
{
    Canal canal;
    ASSERT_TRUE(canal.setup(true));
    TransformComponent* gxf = canal.world.get<TransformComponent>(canal.guard);
    ASSERT_NE(gxf, nullptr);
    gxf->position = Vector3f{ 14.0f, 1.0f, 16.0f };
    canal.world.get<AiAgentComponent>(canal.guard)->forward = Vector3f{ 1.0f, 0.0f, 0.0f };
    PatrolComponent* patrol = canal.world.get<PatrolComponent>(canal.guard);
    ASSERT_NE(patrol, nullptr);
    patrol->waitLeft = 30.0f;
    patrol->peering  = true;
    patrol->peerDir  = Vector3f{ 1.0f, 0.0f, 0.0f };
    watchCanal(canal, Vector3f{ 22.4f, -1.5f, 16.0f }, 1.2f, nullptr, 0);
    EXPECT_TRUE(patrol->peering);
    EXPECT_EQ(canal.world.get<AiAgentComponent>(canal.guard)->aware.alertness, AI::Alertness::Calm);
}

TEST(CanalWatch, DeepSwimmerStaysHiddenFromAPeeringGuard)
{
    Canal canal;
    ASSERT_TRUE(canal.setup());
    ASSERT_TRUE(canal.reachPeer());
    canal.world.get<TransformComponent>(canal.player)->position = Vector3f{ 21.0f, -2.0f, 16.0f };
    canal.ai.setPlayerDepth(4.0f);
    canal.tick(true, 3.0f);
    EXPECT_EQ(canal.world.get<AiAgentComponent>(canal.guard)->aware.alertness, AI::Alertness::Calm);
}

TEST(CanalWatch, CanalTestSceneAimsIntoThePoolAndEveryStopSeesWater)
{
    const std::filesystem::path path = defaultScenePath("canal_test.json");
    SceneFileData               scene{};
    std::string                 err;
    ASSERT_TRUE(loadSceneFromJson(path, scene, &err)) << err;
    Terrain::HeightMap ground;
    ASSERT_TRUE(ground.loadBinary(path.parent_path() / scene.terrain.heightFile));
    const float waterLevel = -0.5f;

    World world;
    int   searchlights = 0;
    int   stops        = 0;
    for (const SceneObjectData& o : scene.objects)
    {
        if (o.type == SceneObjectType::SpotLight && o.searchlight)
        {
            const Entity e = world.createEntity();
            world.emplace<TransformComponent>(e, o.position, o.rotation, Vector3f{ 1.0f, 1.0f, 1.0f });
            auto& light        = world.emplace<LocalLightComponent>(e);
            light.range        = o.lightRange;
            light.outerConeDeg = o.lightOuterDeg;
            applySceneActor(world, e, o, nullptr, nullptr);
            const SecurityCameraComponent* cam = world.get<SecurityCameraComponent>(e);
            ASSERT_NE(cam, nullptr);
            EXPECT_NEAR(cam->def.drop, 7.5f / 14.5f, 0.02f);
            const float    rad = cam->def.yaw * 0.0174533f;
            const float    t   = (o.position.y - waterLevel) / cam->def.drop;
            const Vector3f pool{ o.position.x + std::sin(rad) * t, 0.0f, o.position.z + std::cos(rad) * t };
            EXPECT_LT(std::fabs(pool.x), 15.0f);
            EXPECT_LT(std::fabs(pool.z), 11.0f);
            ++searchlights;
        }
        if (o.type == SceneObjectType::Hunter)
        {
            for (const Vector2f& stop : o.patrol)
            {
                Vector3f dir{};
                EXPECT_TRUE(AI::findWaterBeside(
                    [](void* user, float x, float z) { return static_cast<Terrain::HeightMap*>(user)->heightAtWorld(x, z); }, &ground,
                    Vector3f{ stop.x, 0.0f, stop.y }, waterLevel, dir))
                    << stop.x << ", " << stop.y;
                ++stops;
            }
        }
    }
    EXPECT_EQ(searchlights, 2);
    EXPECT_EQ(stops, 6);
    EXPECT_FLOAT_EQ(scene.water.level, waterLevel);
}

TEST(CanalWatch, AlertedGuardWaitsOnTheBankInsteadOfWalkingIn)
{
    Canal canal;
    ASSERT_TRUE(canal.setup());
    const Vector3f swimmer{ 26.0f, -0.5f, 16.0f };
    canal.world.get<TransformComponent>(canal.player)->position = swimmer;
    canal.ai.forceAlert(canal.world, canal.guard, swimmer);
    canal.ai.setPlayerSwimming(true);
    float furthest = 0.0f;
    for (int i = 0; i < 200; ++i)
    {
        canal.tick(false, 0.05f);
        furthest = std::max(furthest, canal.world.get<TransformComponent>(canal.guard)->position.x);
    }
    EXPECT_LT(furthest, 20.0f);
    EXPECT_GT(furthest, 15.0f);
}
