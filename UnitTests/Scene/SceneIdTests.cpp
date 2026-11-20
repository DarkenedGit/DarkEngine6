#include "Scene/SceneId.h"
#include <gtest/gtest.h>

using namespace Dark;

TEST(SceneId, NormalizesCaseAndBothSeparators)
{
    const std::vector<SceneIdRoot> roots;
    EXPECT_EQ(sceneContentId(R"(C:\Games\Dark\Content\Scenes\Level.JSON)", roots), "scenes/level.json");
    EXPECT_EQ(sceneContentId("/other/install/content/scenes/level.json", roots), "scenes/level.json");
    EXPECT_EQ(sceneContentId("./scenes/LEVEL.json", roots), "scenes/level.json");
    EXPECT_EQ(sceneContentId("level.json", roots), "scenes/level.json");
    EXPECT_TRUE(sceneContentId("", roots).empty());
}

TEST(SceneId, PackageRootsDistinguishSameFileName)
{
    const std::vector<SceneIdRoot> roots = { { "/game/content", "core" }, { "/game/content/mods/frost", "frost" } };
    EXPECT_EQ(sceneContentId("/game/content/scenes/level.json", roots), "scenes/level.json");
    EXPECT_EQ(sceneContentId("/game/content/mods/frost/scenes/level.json", roots), "frost:scenes/level.json");
    EXPECT_EQ(sceneContentId("FROST:scenes/Level.JSON", roots), "frost:scenes/level.json");
    EXPECT_NE(sceneContentId("/game/content/mods/frost/scenes/level.json", roots), sceneContentId("/game/content/scenes/level.json", roots));
}

TEST(SceneId, FolderBoundaryAndNestedPaths)
{
    const std::vector<SceneIdRoot> roots = { { "/game/content/mods/frost", "frost" } };
    EXPECT_EQ(sceneContentId("/game/content/mods/frostbite/scenes/level.json", roots), "scenes/level.json");
    EXPECT_EQ(sceneContentId("/game/content/mods/frost/scenes/sub/level.json", roots), "frost:scenes/sub/level.json");
    EXPECT_NE(sceneContentId("scenes/sub/level.json", roots), sceneContentId("scenes/level.json", roots));
}

TEST(SceneId, SaveGateAcceptsLegacyIdentities)
{
    const std::vector<SceneIdRoot> roots = { { "/game/content/mods/frost", "frost" } };
    // Absolute path written by 7daebbb on another machine.
    EXPECT_TRUE(sceneIdAccepts(R"(D:\Old\Install\content\scenes\level.json)", "scenes/level.json", roots));
    EXPECT_TRUE(sceneIdAccepts("scenes/level.json", "scenes/level.json", roots));
    EXPECT_FALSE(sceneIdAccepts("scenes/other.json", "scenes/level.json", roots));
    EXPECT_FALSE(sceneIdAccepts("frost:scenes/level.json", "scenes/level.json", roots));
    // No identity at all: only the default level (or an unnamed scene) takes it.
    EXPECT_TRUE(sceneIdAccepts("", "scenes/level.json", roots));
    EXPECT_TRUE(sceneIdAccepts("", "", roots));
    EXPECT_FALSE(sceneIdAccepts("", "scenes/other.json", roots));
}
