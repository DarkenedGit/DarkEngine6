#include "Content/ArchetypeParams.h"
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
    EXPECT_EQ(spawner->desc.unit, SpawnerUnit::Hunter);
    EXPECT_EQ(spawner->desc.unitArchetype, "hunter");

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(ArchetypeSpawn, SceneLightOverrideCarriesIntensity)
{
    SceneObjectData object;
    object.archetype            = "core:point_light";
    object.lightKind = "point";
    object.hasLight = true;
    object.hasLight        = true;
    object.lightIntensity  = 1885.0f;
    object.lightRange      = 8.0f;
    const nlohmann::json overlay = nlohmann::json::parse(sceneComponentOverrides(object), nullptr, false);
    ASSERT_TRUE(overlay.is_object());
    ASSERT_TRUE(overlay.contains("LocalLight"));
    EXPECT_FLOAT_EQ(overlay["LocalLight"]["intensity"].get<float>(), 1885.0f);
    EXPECT_FLOAT_EQ(overlay["LocalLight"]["range"].get<float>(), 8.0f);
}
