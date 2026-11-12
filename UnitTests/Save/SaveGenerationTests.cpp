#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Save/PersistentId.h"
#include "Save/SavePaths.h"
#include "Save/SaveTypes.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace Dark;

namespace
{

    void pump(Save::SaveSystem& save, World& world)
    {
        for (int i = 0; i < 100; ++i)
        {
            save.service(world);
            if (!save.busy())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

} // namespace

TEST(SaveGeneration, AsyncLoadDropsWhenTheWorldChanges)
{
    ASSERT_FALSE(Save::savesDirectory().empty());
    World world;
    EXPECT_EQ(world.generation(), 1u);
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 2.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/generation", "box");

    Save::SaveSystem save;
    armSave(save, testHost());
    const auto path = Save::savesDirectory() / "quick_2099-11-11_12-00-01_ab12.json";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_EQ(save.saveNow(world, path, Save::SaveKind::Quick, "generation"), Save::SaveResult::Ok);

    world.get<TransformComponent>(e)->position.x = 9.0f;
    ASSERT_EQ(save.requestLoadName("quick_2099-11-11_12-00-01_ab12.json"), Save::SaveResult::Ok);
    save.service(world);
    world.bumpGeneration();
    pump(save, world);

    EXPECT_EQ(save.lastResult(), Save::SaveResult::SceneMismatch);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 9.0f);
    std::filesystem::remove(path, ec);
}

TEST(SaveGeneration, AsyncLoadAppliesWhenTheGenerationMatches)
{
    ASSERT_FALSE(Save::savesDirectory().empty());
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 2.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/generation-match", "box");

    Save::SaveSystem save;
    armSave(save, testHost());
    const auto path = Save::savesDirectory() / "quick_2099-11-11_12-00-02_ab13.json";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_EQ(save.saveNow(world, path, Save::SaveKind::Quick, "generation"), Save::SaveResult::Ok);

    world.get<TransformComponent>(e)->position.x = 9.0f;
    ASSERT_EQ(save.requestLoadName("quick_2099-11-11_12-00-02_ab13.json"), Save::SaveResult::Ok);
    save.service(world);
    pump(save, world);

    EXPECT_EQ(save.lastResult(), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 2.0f);
    std::filesystem::remove(path, ec);
}
