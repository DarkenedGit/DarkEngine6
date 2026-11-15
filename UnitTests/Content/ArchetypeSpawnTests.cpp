#include "Content/ArchetypeParams.h"
#include "Content/ArchetypeSpawn.h"
#include "Content/ContentDb.h"
#include "Content/SceneObjectSpawn.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/Spawner.h"

#include "third_party/nlohmann/json.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Dark;

namespace
{

    std::filesystem::path tempRoot(const char* name)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    void writeFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << text;
    }

    ArchetypeSpawnHost worldHost(World& world)
    {
        ArchetypeSpawnHost host{};
        host.world = &world;
        return host;
    }

} // namespace

TEST(ArchetypeSpawn, LightOverrideKeepsRange)
{
    resetContentDatabase();
    const auto root = tempRoot("darkengine6_spawn_light");
    writeFile(root / "package.json", R"({ "id": "lab", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(root / "archetypes" / "lamp.archetype.json", R"({
        "id": "lab:lamp",
        "components": {
            "LocalLight": { "kind": "point", "intensity": 1885.0, "range": 8.0 }
        },
        "behaviors": [ { "name": "light.local" } ]
    })");
    writeFile(root / "archetypes" / "search.archetype.json", R"({
        "id": "lab:search",
        "components": {
            "LocalLight": { "kind": "spot", "intensity": 2513.0, "range": 16.0 }
        },
        "behaviors": [ { "name": "light.searchlight" } ]
    })");

    ContentError err;
    ASSERT_TRUE(loadContentPackagesForTest({ root }, &err)) << err.message;

    World world;
    ArchetypeSpawnPose pose{};
    const Entity e = instantiateArchetype(worldHost(world), "lab:lamp", pose, R"({"LocalLight":{"intensity":12}})", &err);
    ASSERT_TRUE(e.valid()) << err.message;
    const LocalLightComponent* light = world.get<LocalLightComponent>(e);
    ASSERT_NE(light, nullptr);
    EXPECT_FLOAT_EQ(light->intensity, 12.0f);
    EXPECT_FLOAT_EQ(light->range, 8.0f);

    const Entity alias = instantiateArchetype(worldHost(world), "lab:search", pose, "{}", &err);
    ASSERT_TRUE(alias.valid()) << err.message;
    EXPECT_NE(world.get<LocalLightComponent>(alias), nullptr);

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(ArchetypeSpawn, SpawnerCountOverrideKeepsUnit)
{
    resetContentDatabase();
    const auto root = tempRoot("darkengine6_spawn_spawner");
    writeFile(root / "package.json", R"({ "id": "lab", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(root / "archetypes" / "nest.archetype.json", R"({
        "id": "lab:nest",
        "components": {
            "Spawner": { "mode": "maintain", "unit": "hunter", "gate": "cleared", "count": 3, "radius": 6.0 }
        },
        "behaviors": [ { "name": "spawner", "params": { "unit": "hunter" } } ]
    })");

    ContentError err;
    ASSERT_TRUE(loadContentPackagesForTest({ root }, &err)) << err.message;

    World world;
    ArchetypeSpawnPose pose{};
    const Entity e = instantiateArchetype(worldHost(world), "lab:nest", pose, R"({"Spawner":{"count":6}})", &err);
    ASSERT_TRUE(e.valid()) << err.message;
    const SpawnerComponent* spawner = world.get<SpawnerComponent>(e);
    ASSERT_NE(spawner, nullptr);
    EXPECT_EQ(spawner->desc.count, 6);
    EXPECT_EQ(spawner->desc.unitArchetype, "hunter");

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(ArchetypeSpawn, SceneLightOverrideCarriesIntensity)
{
    SceneObjectData object;
    object.archetype = "core:point_light";
    SceneLightFields light;
    light.kind      = "point";
    light.intensity = 1885.0f;
    light.range     = 8.0f;
    sceneWriteLight(object, light);
    const nlohmann::json overlay = nlohmann::json::parse(sceneComponentOverrides(object), nullptr, false);
    ASSERT_TRUE(overlay.is_object());
    ASSERT_TRUE(overlay.contains("LocalLight"));
    EXPECT_FLOAT_EQ(overlay["LocalLight"]["intensity"].get<float>(), 1885.0f);
    EXPECT_FLOAT_EQ(overlay["LocalLight"]["range"].get<float>(), 8.0f);
}

TEST(ArchetypeSpawn, RespawnSavedArchetypeResolvesAliasAndPose)
{
    resetContentDatabase();
    const auto root = tempRoot("darkengine6_spawn_respawn");
    writeFile(root / "package.json", R"({ "id": "lab", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(root / "archetypes" / "cube.archetype.json", R"({
        "id": "core:cube",
        "components": { "Tag": {} }
    })");
    writeFile(root / "archetypes" / "hunter.archetype.json", R"({
        "id": "core:hunter",
        "components": { "Tag": {} }
    })");
    writeFile(root / "archetypes" / "wolf.archetype.json", R"({
        "id": "core:wolf",
        "components": { "Tag": {} }
    })");

    ContentError err;
    ASSERT_TRUE(loadContentPackagesForTest({ root }, &err)) << err.message;

    World world;
    ArchetypeSpawnPose pose{};
    pose.position = { 3.0f, 4.0f, 5.0f };
    pose.scale    = { 2.0f, 2.0f, 2.0f };

    const Entity cube = respawnSavedArchetype(worldHost(world), "cube", pose);
    ASSERT_TRUE(cube.valid());
    const ArchetypeParamsComponent* cubeParams = world.get<ArchetypeParamsComponent>(cube);
    ASSERT_NE(cubeParams, nullptr);
    EXPECT_EQ(cubeParams->archetype, "core:cube");
    const TransformComponent* cubeXf = world.get<TransformComponent>(cube);
    ASSERT_NE(cubeXf, nullptr);
    EXPECT_FLOAT_EQ(cubeXf->position.x, 3.0f);
    EXPECT_FLOAT_EQ(cubeXf->position.y, 4.0f);
    EXPECT_FLOAT_EQ(cubeXf->position.z, 5.0f);
    EXPECT_FLOAT_EQ(cubeXf->scale.x, 2.0f);
    EXPECT_FLOAT_EQ(cubeXf->scale.y, 2.0f);
    EXPECT_FLOAT_EQ(cubeXf->scale.z, 2.0f);

    const Entity named = respawnSavedArchetype(worldHost(world), "core:cube", pose);
    ASSERT_TRUE(named.valid());
    const ArchetypeParamsComponent* namedParams = world.get<ArchetypeParamsComponent>(named);
    ASSERT_NE(namedParams, nullptr);
    EXPECT_EQ(namedParams->archetype, "core:cube");

    ArchetypeSpawnPose zero{};
    zero.position = { 1.0f, 2.0f, 3.0f };
    zero.scale    = { 0.0f, 0.0f, 0.0f };
    const Entity scaled = respawnSavedArchetype(worldHost(world), "core:cube", zero);
    ASSERT_TRUE(scaled.valid());
    const TransformComponent* scaledXf = world.get<TransformComponent>(scaled);
    ASSERT_NE(scaledXf, nullptr);
    EXPECT_FLOAT_EQ(scaledXf->position.x, 1.0f);
    EXPECT_FLOAT_EQ(scaledXf->scale.x, 1.0f);
    EXPECT_FLOAT_EQ(scaledXf->scale.y, 1.0f);
    EXPECT_FLOAT_EQ(scaledXf->scale.z, 1.0f);

    const Entity hunter = respawnSavedArchetype(worldHost(world), "hunter", pose);
    ASSERT_TRUE(hunter.valid());
    const ArchetypeParamsComponent* hunterParams = world.get<ArchetypeParamsComponent>(hunter);
    ASSERT_NE(hunterParams, nullptr);
    EXPECT_EQ(hunterParams->archetype, "core:hunter");

    const Entity wolf = respawnSavedArchetype(worldHost(world), "wolf", pose);
    ASSERT_TRUE(wolf.valid());
    const ArchetypeParamsComponent* wolfParams = world.get<ArchetypeParamsComponent>(wolf);
    ASSERT_NE(wolfParams, nullptr);
    EXPECT_EQ(wolfParams->archetype, "core:wolf");

    int before = 0;
    world.each<ArchetypeParamsComponent>([&](Entity, ArchetypeParamsComponent&) { ++before; });
    const Entity missing = respawnSavedArchetype(worldHost(world), "no-such-archetype", pose);
    EXPECT_FALSE(missing.valid());
    int after = 0;
    world.each<ArchetypeParamsComponent>([&](Entity, ArchetypeParamsComponent&) { ++after; });
    EXPECT_EQ(after, before);

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
