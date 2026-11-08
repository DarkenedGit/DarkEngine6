#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Core/JobSystem.h"
#include "Scene/SceneFile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace Dark;

namespace
{

struct TempTree
{
    std::filesystem::path root;

    explicit TempTree(const char* name)
    {
        root = std::filesystem::temp_directory_path()
            / (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        std::filesystem::create_directories(root / "content" / "scenes", ec);
    }

    ~TempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

bool writeScene(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << "{\"version\":1,\"name\":\"tiny\",\"objects\":[]}";
    return static_cast<bool>(out);
}

} // namespace

TEST(SceneLoadJob, PollsMountedScene)
{
    TempTree tree("dark-scene-load");
    const std::filesystem::path tiny = tree.root / "content" / "scenes" / "tiny.json";
    const std::filesystem::path outside = tree.root / "outside.json";
    ASSERT_TRUE(writeScene(tiny));
    ASSERT_TRUE(writeScene(outside));

    AssetManager assets;
    ASSERT_TRUE(assets.mountDirectory(tree.root / "content"));

    EXPECT_FALSE(acceptJobPath(assets, outside));
    EXPECT_FALSE(acceptJobPath(assets, "../outside.json"));
    EXPECT_TRUE(acceptJobPath(assets, tiny));
    EXPECT_TRUE(acceptJobPath(assets, "scenes/tiny.json"));

    JobSystem jobs;
    jobs.start();

    SceneLoadRequest rejected;
    rejected.path = outside;
    rejected.generation = 1;
    EXPECT_FALSE(enqueueSceneLoad(jobs, assets, rejected));

    SceneLoadResult missing;
    EXPECT_FALSE(pollSceneLoad(jobs, 1, missing));
    EXPECT_EQ(missing.status, JobStatus::Failed);

    SceneLoadRequest zero;
    zero.path = tiny;
    zero.generation = 0;
    EXPECT_FALSE(enqueueSceneLoad(jobs, assets, zero));

    SceneLoadRequest request;
    request.path = tiny;
    request.generation = 1;
    ASSERT_TRUE(enqueueSceneLoad(jobs, assets, request));

    SceneLoadResult result;
    bool terminal = false;
    for (int i = 0; i < 200 && !terminal; ++i)
    {
        terminal = pollSceneLoad(jobs, 1, result);
        if (!terminal)
        {
            ASSERT_EQ(result.status, JobStatus::NotReady);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(terminal);
    EXPECT_EQ(result.status, JobStatus::Ok);
    EXPECT_EQ(result.scene.name, "tiny");
    EXPECT_TRUE(result.scene.objects.empty());

    SceneFileData caller;
    std::string error;
    EXPECT_TRUE(loadSceneFromJson(outside, caller, &error));
    EXPECT_EQ(caller.name, "tiny");

    jobs.shutdown();
}

TEST(SceneLoadJob, NotStartedEnqueueFails)
{
    TempTree tree("dark-scene-stopped");
    const std::filesystem::path tiny = tree.root / "content" / "scenes" / "tiny.json";
    ASSERT_TRUE(writeScene(tiny));

    AssetManager assets;
    ASSERT_TRUE(assets.mountDirectory(tree.root / "content"));
    ASSERT_TRUE(acceptJobPath(assets, tiny));

    JobSystem jobs;
    SceneLoadRequest request;
    request.path = tiny;
    request.generation = 1;
    EXPECT_FALSE(enqueueSceneLoad(jobs, assets, request));

    SceneLoadResult result;
    EXPECT_FALSE(pollSceneLoad(jobs, 1, result));
    EXPECT_EQ(result.status, JobStatus::Failed);
}
