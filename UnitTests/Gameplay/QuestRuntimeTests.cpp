#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/QuestRuntime.h"
#include "Gameplay/QuestZone.h"
#include "Play/Carry.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Scene/SceneSpawn.h"
#include "Scene/SceneTypes.h"

#include <gtest/gtest.h>

#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    QuestDef reachQuest()
    {
        QuestDef def;
        def.id    = "reach";
        def.title = "Reach";
        ObjectiveDef gate;
        gate.id     = "gate";
        gate.text   = "Gate";
        gate.type   = ObjectiveType::Reach;
        gate.target = "gate";
        gate.count  = 1;
        def.objectives.push_back(gate);
        return def;
    }

    QuestDef collectQuest()
    {
        QuestDef def;
        def.id    = "collect";
        def.title = "Collect";
        ObjectiveDef files;
        files.id     = "files";
        files.text   = "Files";
        files.type   = ObjectiveType::Collect;
        files.target = "research_file";
        files.count  = 3;
        def.objectives.push_back(files);
        return def;
    }
} // namespace

TEST(QuestRuntime, ZoneEnterReachesTheObjective)
{
    World world;
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player, Vector3f{ 10.0f, 1.0f, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    const Entity zone = world.createEntity();
    QuestZoneComponent area{};
    area.zone = "gate";
    area.box  = AABox3f{ Vector3f{ -1.0f, 0.0f, -1.0f }, Vector3f{ 1.0f, 2.0f, 1.0f } };
    world.emplace<QuestZoneComponent>(zone, area);

    QuestRuntime runtime;
    runtime.start(reachQuest());
    runtime.tick(world, player);
    EXPECT_FALSE(runtime.state(0).done);

    world.get<TransformComponent>(player)->position = Vector3f{ 0.0f, 1.0f, 0.0f };
    runtime.tick(world, player);
    EXPECT_TRUE(runtime.state(0).done);
    EXPECT_TRUE(runtime.complete());
}

TEST(QuestRuntime, CollectTracksHeldItems)
{
    ItemDef file;
    file.id = "research_file";
    InventoryComponent inv;
    inv.bag[0] = { &file, 2 };

    World world;
    const Entity player = world.createEntity();
    world.emplace<InventoryComponent>(player, inv);

    QuestRuntime runtime;
    runtime.start(collectQuest());
    runtime.tick(world, player);
    EXPECT_EQ(runtime.state(0).progress, 2);
    EXPECT_FALSE(runtime.state(0).done);

    world.get<InventoryComponent>(player)->bag[0].count = 3;
    runtime.tick(world, player);
    EXPECT_EQ(runtime.state(0).progress, 3);
    EXPECT_TRUE(runtime.state(0).done);
}

TEST(QuestRuntime, StartIdLoadsTheQuestFile)
{
    QuestRuntime runtime;
    runtime.start(reachQuest());
    EXPECT_TRUE(runtime.active());
    EXPECT_FALSE(runtime.start(""));
    EXPECT_EQ(runtime.def().id, "reach");
    EXPECT_FALSE(runtime.start("no_such_quest"));
    EXPECT_EQ(runtime.def().id, "reach");
    ASSERT_TRUE(runtime.start("north_lab"));
    EXPECT_EQ(runtime.def().id, "north_lab");
    EXPECT_EQ(runtime.def().objectives.size(), 4u);
    EXPECT_EQ(runtime.def().rewards.size(), 2u);
}

TEST(QuestRuntime, ObjectiveTargetFindsZonePickupAndBreaker)
{
    World world;
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player, Vector3f{ 0.0f, 0.0f, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    const Entity zone = world.createEntity();
    QuestZoneComponent area{};
    area.zone = "gate";
    area.box  = AABox3f{ Vector3f{ 8.0f, 0.0f, 2.0f }, Vector3f{ 12.0f, 2.0f, 6.0f } };
    world.emplace<QuestZoneComponent>(zone, area);

    QuestRuntime runtime;
    runtime.start(reachQuest());
    Vector3f target{};
    ASSERT_TRUE(runtime.objectiveTarget(world, player, target));
    EXPECT_FLOAT_EQ(target.x, 10.0f);
    EXPECT_FLOAT_EQ(target.z, 4.0f);

    ItemDef file;
    file.id = "research_file";
    const Entity nearPickup = world.createEntity();
    world.emplace<TransformComponent>(nearPickup, Vector3f{ 2.0f, 0.0f, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<ItemPickupComponent>(nearPickup, ItemPickupComponent{ ItemStack{ &file, 1 } });
    const Entity farPickup = world.createEntity();
    world.emplace<TransformComponent>(farPickup, Vector3f{ 30.0f, 0.0f, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<ItemPickupComponent>(farPickup, ItemPickupComponent{ ItemStack{ &file, 1 } });
    runtime.start(collectQuest());
    ASSERT_TRUE(runtime.objectiveTarget(world, player, target));
    EXPECT_FLOAT_EQ(target.x, 2.0f);

    QuestDef power;
    power.id = "power";
    ObjectiveDef off;
    off.type   = ObjectiveType::Event;
    off.target = "power_off";
    power.objectives.push_back(off);
    const Entity live = world.createEntity();
    world.emplace<TransformComponent>(live, Vector3f{ 4.0f, 1.0f, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    PowerBoxComponent on{};
    on.on  = true;
    on.pos = Vector3f{ 4.0f, 1.0f, 0.0f };
    world.emplace<PowerBoxComponent>(live, on);
    const Entity dead = world.createEntity();
    PowerBoxComponent offBox{};
    offBox.on  = false;
    offBox.pos = Vector3f{ 1.0f, 0.0f, 0.0f };
    world.emplace<PowerBoxComponent>(dead, offBox);
    runtime.start(std::move(power));
    ASSERT_TRUE(runtime.objectiveTarget(world, player, target));
    EXPECT_FLOAT_EQ(target.x, 4.0f);
    EXPECT_FLOAT_EQ(target.y, 1.0f);
}

TEST(QuestRuntime, GrantRewardsOnce)
{
    QuestDef def;
    def.id = "pay";
    ObjectiveDef done;
    done.type   = ObjectiveType::Event;
    done.target = "done";
    def.objectives.push_back(done);
    def.rewards.push_back(QuestReward{ "research", 3 });
    def.rewards.push_back(QuestReward{ "missing_item", 1 });
    def.rewards.push_back(QuestReward{ "helmet", 1 });

    QuestRuntime runtime;
    runtime.start(std::move(def));
    World world;
    const Entity player = world.createEntity();
    world.emplace<InventoryComponent>(player);
    EXPECT_EQ(runtime.grantRewards(world, player), 0);

    runtime.event("done");
    EXPECT_TRUE(runtime.complete());
    EXPECT_EQ(runtime.grantRewards(world, player), 2);
    const InventoryComponent* inv = world.get<InventoryComponent>(player);
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(countItem(*inv, "research"), 3);
    EXPECT_TRUE(inv->equipped[static_cast<int>(EquipSlot::Head)].def);
    EXPECT_EQ(runtime.grantRewards(world, player), 0);
    EXPECT_EQ(countItem(*inv, "research"), 3);
}

TEST(SceneSpawn, BoxesBecomeZoneCircuitConcealAndObstacle)
{
    std::vector<SceneObjectData> objects(4);
    objects[0].zone     = "gate";
    objects[0].position = Vector3f{ 0.0f, 1.0f, 0.0f };
    objects[0].scale    = Vector3f{ 2.0f, 2.0f, 2.0f };
    objects[1].circuit  = "lab";
    objects[1].position = Vector3f{ 4.0f, 0.0f, 0.0f };
    objects[1].scale    = Vector3f{ 1.0f, 0.2f, 1.0f };
    objects[2].conceal  = true;
    objects[2].position = Vector3f{ 0.0f, 0.0f, 2.0f };
    objects[2].scale    = Vector3f{ 4.0f, 1.0f, 4.0f };
    objects[3].position = Vector3f{ 8.0f, 0.0f, 0.0f };
    objects[3].scale    = Vector3f{ 2.0f, 1.0f, 2.0f };

    World world;
    SceneSpawnHost host{};
    host.world       = &world;
    host.stampPrefix = "test/level";
    instantiateSceneObjects(objects.data(), static_cast<int>(objects.size()), host);

    int zones = 0;
    int powers = 0;
    int conceals = 0;
    int obstacles = 0;
    world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
        ++zones;
        EXPECT_EQ(zone.zone, "gate");
    });
    world.each<PowerBoxComponent>([&](Entity, PowerBoxComponent& box) {
        ++powers;
        EXPECT_EQ(box.circuit, "lab");
        EXPECT_TRUE(box.on);
    });
    world.each<ConcealVolumeComponent>([&](Entity, ConcealVolumeComponent&) { ++conceals; });
    world.each<ObstacleVolumeComponent>([&](Entity, ObstacleVolumeComponent&) { ++obstacles; });
    EXPECT_EQ(zones, 1);
    EXPECT_EQ(powers, 1);
    EXPECT_EQ(conceals, 1);
    EXPECT_EQ(obstacles, 1);
}
