#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/Components.h"
#include "ECS/World.h"

using namespace Dark;

namespace
{
    std::string capturedWithIdentity(World& world, const char* identity)
    {
        Save::SaveSystem save;
        save.setHost(testHost());
        save.setWorldIdentity(identity, "", "unit");
        std::string json;
        EXPECT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
        return json;
    }
} // namespace

TEST(SaveSceneId, IdentityIsWrittenContentRelative)
{
    World                  world;
    const std::string      json = capturedWithIdentity(world, "C:/Games/Dark/content/scenes/Level.json");
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    EXPECT_EQ(root["scene"]["path"].get<std::string>(), "scenes/level.json");
}

TEST(SaveSceneId, MovedInstallStillLoads)
{
    World             world;
    const std::string json = capturedWithIdentity(world, "C:/Games/Dark/content/scenes/level.json");
    Save::SaveSystem  load;
    load.setHost(testHost());
    load.setWorldIdentity("E:/Elsewhere/content/scenes/level.json", "", "unit");
    EXPECT_EQ(load.applyPayloadText(world, json), Save::SaveResult::Ok);
}

TEST(SaveSceneId, OtherSceneIsRejected)
{
    World             world;
    const std::string json = capturedWithIdentity(world, "scenes/level.json");
    Save::SaveSystem  load;
    load.setHost(testHost());
    load.setWorldIdentity("scenes/arena.json", "", "unit");
    EXPECT_EQ(load.applyPayloadText(world, json), Save::SaveResult::SceneMismatch);
}

TEST(SaveSceneId, LegacyModPathNeverLoadsAsDefaultLevel)
{
    World                  world;
    const std::string      modJson = capturedWithIdentity(world, "D:/Old/Install/mods/frost/scenes/level.json");
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(modJson, root));
    EXPECT_EQ(root["scene"]["path"].get<std::string>(), "frost:scenes/level.json");

    Save::SaveSystem core;
    core.setHost(testHost());
    core.setWorldIdentity("scenes/level.json", "", "unit");
    EXPECT_EQ(core.applyPayloadText(world, modJson), Save::SaveResult::SceneMismatch);

    // A custom path from another install has no portable identity; it is rejected, not defaulted.
    const std::string customJson = capturedWithIdentity(world, "D:/Old/Custom/frost/scenes/level.json");
    EXPECT_EQ(core.applyPayloadText(world, customJson), Save::SaveResult::SceneMismatch);
}
