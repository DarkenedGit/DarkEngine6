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
