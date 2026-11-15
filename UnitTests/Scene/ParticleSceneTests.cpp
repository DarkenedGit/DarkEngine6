#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Content/ContentDb.h"
#include "Content/SceneObjectSpawn.h"
#include "Particles/ParticleScene.h"
#include "Scene/SceneFile.h"

#include <filesystem>
#include <fstream>

using namespace Dark;
using namespace Dark::Math;

namespace
{

std::filesystem::path tempScenePath(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

bool writeScene(const std::filesystem::path& path, const char* json)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << json;
    return static_cast<bool>(out);
}

} // namespace

TEST(ParticleScene, MissingSimModeStaysCpu)
{
    const auto path = tempScenePath("darkengine6_particle_missing_mode.json");
    ASSERT_TRUE(writeScene(path, R"({
        "version": 2,
        "name": "old",
        "objects": [{
            "archetype": "core:particle_emitter",
            "position": [1, 2, 3],
            "overrides": { "ParticleEmitter": { "name": "old" } }
        }]
    })"));

    SceneFileData data{};
    std::string err;
    if (!contentHasArchetype("core:cube"))
        ASSERT_TRUE(loadContentDatabase(nullptr));
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_TRUE(sceneCarriesComponent(data.objects[0], "ParticleEmitter"));

    ParticleEmitterDesc desc{};
    particleDescFromSceneData(data.objects[0], desc);
    EXPECT_EQ(desc.simMode, ParticleSimMode::Cpu);
    EXPECT_EQ(desc.collision, ParticleCollisionReaction::None);
    EXPECT_FLOAT_EQ(desc.collisionThickness, 0.35f);
    EXPECT_FLOAT_EQ(desc.collisionBias, 0.04f);
    EXPECT_EQ(desc.name, "old");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ParticleScene, StringSimModeDoesNotFailTheLoad)
{
    const auto path = tempScenePath("darkengine6_particle_bad_mode.json");
    ASSERT_TRUE(writeScene(path, R"({
        "version": 2,
        "name": "bad",
        "objects": [
            {
                "archetype": "core:particle_emitter",
                "overrides": { "ParticleEmitter": { "simMode": "gpu", "collision": "bounce", "secondary": "nope" } }
            },
            { "archetype": "core:cube", "position": [4, 5, 6] }
        ]
    })"));

    SceneFileData data{};
    std::string err;
    if (!contentHasArchetype("core:cube"))
        ASSERT_TRUE(loadContentDatabase(nullptr));
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_EQ(data.objects[0].archetype, "core:particle_emitter");
    EXPECT_TRUE(sceneCarriesComponent(data.objects[0], "ParticleEmitter"));
    EXPECT_EQ(data.objects[1].archetype, "core:cube");
    EXPECT_NEAR(data.objects[1].position.x, 4.0f, 1.0e-4f);

    ParticleEmitterDesc desc{};
    particleDescFromSceneData(data.objects[0], desc);
    EXPECT_EQ(desc.simMode, ParticleSimMode::Cpu);
    EXPECT_EQ(desc.collision, ParticleCollisionReaction::None);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ParticleScene, SimModeRoundTripAndDescMatch)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "particles";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData object{};
    object.archetype = "core:particle_emitter";
    object.position  = Vector3f(8.0f, 1.5f, -2.0f);
    ParticleEmitterDesc authored{};
    authored.name                = "rain";
    authored.maxParticles        = 128;
    authored.simMode             = ParticleSimMode::Gpu;
    authored.collision           = ParticleCollisionReaction::Bounce;
    authored.restitution         = 0.45f;
    authored.collisionThickness  = 0.35f;
    authored.collisionBias       = 0.04f;
    authored.hasSecondary        = true;
    authored.secondary.maxParticles = 64;
    authored.secondary.collision = ParticleCollisionReaction::SpawnSecondary;
    authored.secondary.lifetime  = { 0.1f, 0.25f };
    sceneDataFromParticleDesc(authored, object);
    in.objects.push_back(object);

    const auto path = tempScenePath("darkengine6_particle_roundtrip.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData loaded{};
    if (!contentHasArchetype("core:cube"))
        ASSERT_TRUE(loadContentDatabase(nullptr));
    ASSERT_TRUE(loadSceneFromJson(path, loaded, &err)) << err;
    ASSERT_EQ(loaded.objects.size(), 1u);
    ParticleEmitterDesc loadedDesc{};
    particleDescFromSceneData(loaded.objects[0], loadedDesc);
    EXPECT_EQ(loadedDesc.simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(loadedDesc.collision, ParticleCollisionReaction::Bounce);
    EXPECT_TRUE(loadedDesc.hasSecondary);
    EXPECT_EQ(loadedDesc.secondary.collision, ParticleCollisionReaction::Stop);
    EXPECT_EQ(loadedDesc.secondary.maxParticles, 64u);
    EXPECT_NEAR(loadedDesc.restitution, 0.45f, 1.0e-5f);

    ASSERT_TRUE(saveSceneToJson(path, loaded, &err)) << err;
    SceneFileData again{};
    if (!contentHasArchetype("core:cube"))
        ASSERT_TRUE(loadContentDatabase(nullptr));
    ASSERT_TRUE(loadSceneFromJson(path, again, &err)) << err;
    ASSERT_EQ(again.objects.size(), 1u);
    ParticleEmitterDesc againDesc{};
    particleDescFromSceneData(again.objects[0], againDesc);
    EXPECT_EQ(againDesc.simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(againDesc.collision, ParticleCollisionReaction::Bounce);
    EXPECT_EQ(againDesc.secondary.collision, ParticleCollisionReaction::Stop);

    ParticleEmitterDesc desc{};
    particleDescFromSceneData(again.objects[0], desc);
    EXPECT_EQ(desc.simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(desc.collision, ParticleCollisionReaction::Bounce);
    EXPECT_EQ(desc.maxParticles, 128u);
    EXPECT_EQ(desc.name, "rain");
    EXPECT_TRUE(desc.hasSecondary);
    EXPECT_EQ(desc.secondary.collision, ParticleCollisionReaction::Stop);
    EXPECT_EQ(desc.secondary.maxParticles, 64u);
    EXPECT_NEAR(desc.collisionThickness, 0.35f, 1.0e-5f);
    EXPECT_NEAR(desc.collisionBias, 0.04f, 1.0e-5f);

    SceneObjectData stored{};
    sceneDataFromParticleDesc(desc, stored);
    EXPECT_TRUE(sceneCarriesComponent(stored, "ParticleEmitter"));
    ParticleEmitterDesc storedDesc{};
    particleDescFromSceneData(stored, storedDesc);
    EXPECT_EQ(storedDesc.simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(storedDesc.collision, ParticleCollisionReaction::Bounce);
    EXPECT_EQ(storedDesc.secondary.collision, ParticleCollisionReaction::Stop);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(ParticleScene, SpawnCreatesEmitterWithoutLocalLight)
{
    SceneObjectData data{};
    data.archetype = "core:particle_emitter";
    data.position  = Vector3f(1.0f, 2.0f, 3.0f);
    data.scale     = Vector3f(2.0f, 2.0f, 2.0f);
    ParticleEmitterDesc sparks{};
    sparks.name               = "sparks";
    sparks.maxParticles       = 100;
    sparks.simMode            = ParticleSimMode::Gpu;
    sparks.collision          = ParticleCollisionReaction::Bounce;
    sparks.collisionThickness = 0.35f;
    sparks.collisionBias      = 0.04f;
    sceneDataFromParticleDesc(sparks, data);

    World world;
    AssetManager assets;
    AssetPinTable pins;
    const Entity e = spawnSceneParticleEmitter(world, assets, &pins, data);
    ASSERT_TRUE(e.valid());

    const ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(pe, nullptr);
    EXPECT_EQ(pe->desc.simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(pe->desc.collision, ParticleCollisionReaction::Bounce);
    EXPECT_EQ(pe->desc.maxParticles, 100u);
    EXPECT_EQ(pe->desc.name, "sparks");
    EXPECT_FLOAT_EQ(pe->desc.collisionThickness, 0.35f);
    EXPECT_FLOAT_EQ(pe->desc.collisionBias, 0.04f);
    EXPECT_NE(pe->matAssetID, NULL_ASSET);
    EXPECT_TRUE(pins.get(pe->matAssetID));
    ASSERT_NE(pe->runtime, nullptr);
    EXPECT_EQ(pe->runtime->desc().simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(pe->runtime->capacity(), 0u);

    const TransformComponent* xf = world.get<TransformComponent>(e);
    ASSERT_NE(xf, nullptr);
    EXPECT_NEAR(xf->position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(xf->position.y, 2.0f, 1.0e-4f);
    EXPECT_NEAR(xf->position.z, 3.0f, 1.0e-4f);
    EXPECT_NEAR(xf->scale.x, 2.0f, 1.0e-4f);
    EXPECT_FALSE(world.has<LocalLightComponent>(e));

    SceneObjectData plain{};
    plain.archetype = "core:particle_emitter";
    plain.position  = Vector3f(0.0f, 1.0f, 0.0f);
    const Entity bare = spawnSceneParticleEmitter(world, assets, nullptr, plain);
    ASSERT_TRUE(bare.valid());
    const ParticleEmitterComponent* cpu = world.get<ParticleEmitterComponent>(bare);
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->desc.simMode, ParticleSimMode::Cpu);
    EXPECT_EQ(cpu->desc.collision, ParticleCollisionReaction::None);
    EXPECT_FALSE(world.has<LocalLightComponent>(bare));
}
