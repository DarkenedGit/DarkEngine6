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
    EXPECT_EQ(sceneContentId("/game/content/mods/frostbite/scenes/level.json", roots), "frostbite:scenes/level.json");
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

TEST(SceneId, LegacyModPathKeepsPackageAfterInstallMove)
{
    const std::vector<SceneIdRoot> roots = { { "/new/content", "core" }, { "/new/mods/frost", "frost" } };
    const std::string              old   = R"(D:\Old\mods\Frost\scenes\Level.JSON)";
    EXPECT_EQ(sceneContentId(old, roots), "frost:scenes/level.json");
    EXPECT_TRUE(sceneIdAccepts(old, "/new/mods/frost/scenes/level.json", roots));
    EXPECT_FALSE(sceneIdAccepts(old, "scenes/level.json", roots));
    EXPECT_FALSE(sceneIdAccepts(old, "", roots));
    EXPECT_EQ(sceneContentId("/new/content/mods/extra/scenes/level.json", roots), "extra:scenes/level.json");
}

TEST(SceneId, UnmatchedPathNeverFallsBackToCoreOrDefault)
{
    const std::vector<SceneIdRoot> roots = { { "/new/content", "core" } };
    for (const std::string old : { "/old/custom/frost/scenes/level.json", "/old/mods/core/scenes/level.json", "/old/level.json" })
    {
        const std::string id = sceneContentId(old, roots);
        EXPECT_TRUE(id.starts_with(kUnresolvedScenePrefix)) << old << " -> " << id;
        EXPECT_FALSE(sceneIdAccepts(old, "scenes/level.json", roots)) << old;
        EXPECT_FALSE(sceneIdAccepts(old, "frost:scenes/level.json", roots)) << old;
        EXPECT_FALSE(sceneIdAccepts(old, "", roots)) << old;
        // Idempotent, and the same unresolved file still matches itself (same install, custom path).
        EXPECT_EQ(sceneContentId(id, roots), id);
        EXPECT_TRUE(sceneIdAccepts(old, old, roots));
    }
}

TEST(SceneId, ModFolderMapsToCurrentPackageId)
{
    // package.json can give a package an id that is not its folder name.
    const std::vector<SceneIdRoot> roots = { { "/new/content", "core" }, { "/new/mods/FrostPack", "frost" } };
    EXPECT_EQ(sceneContentId(R"(D:\Old\mods\FrostPack\scenes\Level.json)", roots), "frost:scenes/level.json");
    EXPECT_EQ(sceneContentId("/old/mods/unknown/scenes/level.json", roots), "unknown:scenes/level.json");
    // A "mods" folder in the install prefix of core content is not a mod.
    EXPECT_EQ(sceneContentId("C:/mods/Game/content/scenes/level.json", roots), "scenes/level.json");
}
