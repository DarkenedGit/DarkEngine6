#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Character/PlayerMotorComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/EnvironmentTick.h"
#include "Render/Camera3D.h"
#include "Terrain/HeightMap.h"
#include "Terrain/LeafRustle.h"
#include "Terrain/TerrainGrid.h"

using namespace Dark;

namespace
{

    bool makeGrid(Terrain::TerrainGrid& grid)
    {
        constexpr int      kTileCells = 8;
        constexpr float    kCell      = 16.0f;
        constexpr uint32_t kSamples   = static_cast<uint32_t>(kTileCells * 2 + 1);
        Terrain::HeightMap working;
        if (!working.create(kSamples, kSamples, kCell, 1.0f))
            return false;
        working.setOrigin(Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        Terrain::HeightMap      coarse = working;
        Terrain::TerrainGridDesc desc{};
        desc.tilesX       = 2;
        desc.tilesZ       = 2;
        desc.tileCells    = static_cast<uint32_t>(kTileCells);
        desc.cellSize     = kCell;
        desc.origin       = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        desc.residentRing = 1;
        if (!grid.createFromCoarse(desc, std::move(coarse)))
            return false;
        return grid.setWorking(std::move(working), Terrain::SplatMap{});
    }

} // namespace

TEST(EnvironmentTick, StoppedLeavesDoNotAdvance)
{
    Terrain::LeafField        field;
    Terrain::LeafRustleVoices rustle;
    Audio::AudioSystem        audio;
    double                    sim = 4.0;
    FallingLeafTick           tick;
    tick.field      = &field;
    tick.rustle     = &rustle;
    tick.audio      = &audio;
    tick.simSeconds = &sim;
    stepFallingLeaves(tick, 0.0f);
    stepFallingLeaves(tick, -1.0f);
    EXPECT_DOUBLE_EQ(sim, 4.0);
}

TEST(EnvironmentTick, OpenGroundStepsTheLeafClock)
{
    Terrain::TerrainGrid grid;
    ASSERT_TRUE(makeGrid(grid));
    Terrain::LeafField        field;
    Terrain::LeafRustleVoices rustle;
    Audio::AudioSystem        audio;
    AssetManager              assets;
    Camera3D                  camera;
    camera.SetPosition(8.0f, 2.0f, 8.0f);
    double          sim = 1.0;
    FallingLeafTick tick;
    tick.field      = &field;
    tick.rustle     = &rustle;
    tick.terrain    = &grid;
    tick.audio      = &audio;
    tick.assets     = &assets;
    tick.camera     = &camera;
    tick.simSeconds = &sim;
    tick.shedScale  = 0.0f;
    const float dt  = 0.05f;
    stepFallingLeaves(tick, dt);
    EXPECT_DOUBLE_EQ(sim, 1.0 + static_cast<double>(dt));
}

TEST(EnvironmentTick, GrassSampleUsesTheBodyAndLeavesAMissAlone)
{
    World world;
    const Entity body = world.createEntity();
    world.emplace<TransformComponent>(body, Math::Vector3f{ 3.0f, 1.0f, -4.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    PlayerMotorComponent& motor = world.emplace<PlayerMotorComponent>(body);
    motor.motor.setHorizontalVelocity(1.5f, -2.0f);

    float                      x = 0.0f;
    float                      z = 0.0f;
    Terrain::GrassPlayerSample sample;
    EXPECT_TRUE(fillGrassPlayerSample(world, body, x, z, sample));
    EXPECT_FLOAT_EQ(x, 3.0f);
    EXPECT_FLOAT_EQ(z, -4.0f);
    EXPECT_FLOAT_EQ(sample.x, 3.0f);
    EXPECT_FLOAT_EQ(sample.z, -4.0f);
    EXPECT_FLOAT_EQ(sample.velX, 1.5f);
    EXPECT_FLOAT_EQ(sample.velZ, -2.0f);
    EXPECT_TRUE(sample.shoving);

    float keepX = 8.0f;
    float keepZ = 9.0f;
    EXPECT_FALSE(fillGrassPlayerSample(world, Entity{}, keepX, keepZ, sample));
    EXPECT_FLOAT_EQ(keepX, 8.0f);
    EXPECT_FLOAT_EQ(keepZ, 9.0f);
}

TEST(EnvironmentTick, ListenerCopiesTheCamera)
{
    Audio::AudioSystem audio;
    Camera3D           camera;
    camera.SetPosition(1.0f, 2.0f, 3.0f);
    setViewAudioListener(audio, camera, nullptr, {}, false);
    EXPECT_FLOAT_EQ(audio.listener().position.x, 1.0f);
    EXPECT_FLOAT_EQ(audio.listener().position.y, 2.0f);
    EXPECT_FLOAT_EQ(audio.listener().position.z, 3.0f);
    EXPECT_FLOAT_EQ(audio.listener().distanceScale, 1.0f);
    EXPECT_FLOAT_EQ(audio.listener().forward.z, camera.GetLook().z);
    EXPECT_FLOAT_EQ(audio.listener().up.y, camera.GetUp().y);
}
