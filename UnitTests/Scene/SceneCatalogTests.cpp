#include <gtest/gtest.h>

#include "Scene/SceneCatalog.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Dark;

namespace
{

std::filesystem::path makeTempDir(const char* name)
{
    std::error_code ec;
    const auto      dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

TEST(SceneCatalog, PeekReadsNameAndMode)
{
    const auto dir  = makeTempDir("darkengine6_scenecatalog_peek");
    const auto path = dir / "arena.json";
    writeFile(path, R"({
  "version": 2,
  "name": "Arena",
  "mode": "2d",
  "objects": []
})");

    SceneFileInfo info{};
    std::string   err;
    ASSERT_TRUE(peekSceneFile(path, info, &err)) << err;
    EXPECT_EQ(info.fileName, "arena.json");
    EXPECT_EQ(info.displayName, "Arena");
    EXPECT_EQ(info.mode, SceneMode::Scene2D);
    EXPECT_EQ(info.version, 2);
    EXPECT_EQ(info.path.filename(), "arena.json");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(SceneCatalog, PeekDefaultsModeAndName)
{
    const auto dir  = makeTempDir("darkengine6_scenecatalog_defaults");
    const auto path = dir / "level.json";
    writeFile(path, R"({ "objects": [] })");

    SceneFileInfo info{};
    ASSERT_TRUE(peekSceneFile(path, info, nullptr));
    EXPECT_EQ(info.displayName, "level");
    EXPECT_EQ(info.mode, SceneMode::Scene3D);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(SceneCatalog, PeekRejectsMissingAndInvalid)
{
    SceneFileInfo info{};
    std::string   err;
    EXPECT_FALSE(peekSceneFile(std::filesystem::path("no_such_scene.json"), info, &err));
    EXPECT_FALSE(err.empty());

    const auto dir  = makeTempDir("darkengine6_scenecatalog_bad");
    const auto path = dir / "bad.json";
    writeFile(path, "not json");
    err.clear();
    EXPECT_FALSE(peekSceneFile(path, info, &err));
    EXPECT_FALSE(err.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(SceneCatalog, ListFiltersByModeAndSorts)
{
    const auto dir = makeTempDir("darkengine6_scenecatalog_list");
    writeFile(dir / "zeta.json", R"({ "name": "Zeta", "mode": "2d", "objects": [] })");
    writeFile(dir / "alpha.json", R"({ "name": "Alpha", "mode": "2d", "objects": [] })");
    writeFile(dir / "world.json", R"({ "name": "World", "mode": "3d", "objects": [] })");
    writeFile(dir / "notes.txt", "ignore me");
    writeFile(dir / "broken.json", "{");

    const auto twoD = listSceneFiles(SceneMode::Scene2D, dir);
    ASSERT_EQ(twoD.size(), 2u);
    EXPECT_EQ(twoD[0].displayName, "Alpha");
    EXPECT_EQ(twoD[0].fileName, "alpha.json");
    EXPECT_EQ(twoD[1].displayName, "Zeta");

    const auto threeD = listSceneFiles(SceneMode::Scene3D, dir);
    ASSERT_EQ(threeD.size(), 1u);
    EXPECT_EQ(threeD[0].fileName, "world.json");

    EXPECT_TRUE(listSceneFiles(SceneMode::Scene2D, {}).empty());

    const SceneFileInfo* found = findSceneByFileName(twoD, "zeta.json");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->displayName, "Zeta");
    EXPECT_EQ(findSceneByFileName(twoD, "missing.json"), nullptr);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(SceneCatalog, FindScenesDirectoryPointsAtScenes)
{
    const auto dir = findScenesDirectory();
    if (dir.empty())
        GTEST_SKIP() << "no content/scenes on this machine";
    EXPECT_EQ(dir.filename(), "scenes");
}
