#include <gtest/gtest.h>

#include "Save/SaveTestWorld.h"

#include "AI/AiComponents.h"
#include "AI/Awareness.h"
#include "Character/HealthComponent.h"
#include "Core/ContentRoots.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/PowerSystem.h"
#include "Gameplay/Quest.h"
#include "Gameplay/QuestRuntime.h"
#include "Gameplay/QuestZone.h"
#include "Math/AABox3f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Play/Carry.h"
#include "Save/PersistentId.h"
#include "Scene/SceneFile.h"
#include "Scene/SceneSpawn.h"

#include <filesystem>
#include <string>

using namespace Dark;

namespace
{

    std::filesystem::path sceneFile(const char* name)
    {
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            const std::filesystem::path path = root / "scenes" / name;
            std::error_code             ec;
            if (std::filesystem::exists(path, ec) && !ec)
                return path;
        }
        return {};
    }

    bool loadScene(const char* name, SceneFileData& out)
    {
        const std::filesystem::path path = sceneFile(name);
        if (path.empty())
            return false;
        std::string error;
        return loadSceneFromJson(path, out, &error);
    }

    template <typename T> int countOf(World& world)
    {
        int n = 0;
        world.each<T>([&](Entity, T&) { ++n; });
        return n;
    }

    struct SpawnExpect
    {
        int zones     = 0;
        int powers    = 0;
        int conceals  = 0;
        int obstacles = 0;
        int goals     = 0;
        int pickups   = 0;
    };

    bool localLight(SceneObjectType type)
    {
        return type == SceneObjectType::PointLight || type == SceneObjectType::SpotLight;
    }

    SpawnExpect expectOf(const std::vector<SceneObjectData>& objects)
    {
        SpawnExpect e;
        for (const SceneObjectData& o : objects)
        {
            const bool cube = o.type == SceneObjectType::Cube;
            if (!o.zone.empty())
            {
                ++e.zones;
                continue;
            }
            if (!o.pickup.empty())
            {
                ++e.pickups;
                continue;
            }
            if (o.goal)
                ++e.goals;
            if (!o.circuit.empty() && !localLight(o.type))
                ++e.powers;
            if (o.conceal)
            {
                ++e.conceals;
                continue;
            }
            if (cube && o.scale.y >= 0.3f && !o.goal)
                ++e.obstacles;
        }
        return e;
    }

    void spawn(World& world, const SceneFileData& scene, const char* prefix)
    {
        SceneSpawnHost host{};
        host.world       = &world;
        host.stampPrefix = prefix;
        instantiateSceneObjects(scene.objects.data(), static_cast<int>(scene.objects.size()), host);
    }

    SpawnExpect liveCounts(World& world)
    {
        SpawnExpect e;
        e.zones     = countOf<QuestZoneComponent>(world);
        e.powers    = countOf<PowerBoxComponent>(world);
        e.conceals  = countOf<ConcealVolumeComponent>(world);
        e.obstacles = countOf<ObstacleVolumeComponent>(world);
        e.goals     = countOf<GoalVolumeComponent>(world);
        e.pickups   = countOf<ItemPickupComponent>(world);
        return e;
    }

    void expectMatch(const SpawnExpect& live, const SpawnExpect& file)
    {
        EXPECT_EQ(live.zones, file.zones);
        EXPECT_EQ(live.powers, file.powers);
        EXPECT_EQ(live.conceals, file.conceals);
        EXPECT_EQ(live.obstacles, file.obstacles);
        EXPECT_EQ(live.goals, file.goals);
        EXPECT_EQ(live.pickups, 0);
    }

    bool hasZone(World& world, const char* name)
    {
        bool found = false;
        world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
            if (zone.zone == name)
                found = true;
        });
        return found;
    }

    bool hasCircuit(World& world, const char* name)
    {
        bool found = false;
        world.each<PowerBoxComponent>([&](Entity, PowerBoxComponent& box) {
            if (box.circuit == name && box.on)
                found = true;
        });
        return found;
    }

    bool hasProceduralKey(World& world, const std::string& key)
    {
        const UUID want = makeProceduralId(key);
        bool       found = false;
        world.each<PersistentIdComponent>([&](Entity, PersistentIdComponent& pid) {
            if (pid.id == want)
                found = true;
        });
        return found;
    }

    QuestDef northLab()
    {
        QuestDef def;
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            std::string error;
            if (loadQuestFile(root / questVirtualPath("north_lab"), def, &error))
                return def;
        }
        return {};
    }

    Entity makeGuard(World& world, const Math::Vector3f& pos, const char* tag)
    {
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, pos, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        world.emplace<HealthComponent>(e);
        world.emplace<AiAgentComponent>(e);
        if (tag)
            world.emplace<TagComponent>(e, TagComponent{ tag });
        return e;
    }

    Entity makeLight(World& world, const char* circuit)
    {
        const Entity e = world.createEntity();
        world.emplace<LocalLightComponent>(e);
        CircuitMemberComponent member{};
        member.circuit = circuit;
        world.emplace<CircuitMemberComponent>(e, member);
        return e;
    }

    float flatHeight(void*, float, float) { return 0.0f; }

    struct HideHook
    {
        QuestTracker* quest = nullptr;
        int           hits  = 0;
    };

    void onHidden(void* user)
    {
        auto* hook = static_cast<HideHook*>(user);
        ++hook->hits;
        if (hook->quest)
            hook->quest->event("body_hidden");
    }

} // namespace

TEST(ScenePlay, LevelJsonSpawnsTwoObstaclesAndNoQuestVolumes)
{
    SceneFileData scene;
    ASSERT_TRUE(loadScene("level.json", scene));
    const SpawnExpect file = expectOf(scene.objects);
    EXPECT_EQ(file.obstacles, 2);
    EXPECT_EQ(file.zones, 0);
    EXPECT_EQ(file.powers, 0);
    EXPECT_EQ(file.conceals, 0);
    EXPECT_EQ(file.goals, 0);
    EXPECT_EQ(file.pickups, 0);

    World sandbox;
    World editor;
    spawn(sandbox, scene, "sandbox/level");
    spawn(editor, scene, "editor/scene");
    const SpawnExpect a = liveCounts(sandbox);
    const SpawnExpect b = liveCounts(editor);
    expectMatch(a, file);
    expectMatch(b, file);
    EXPECT_EQ(a.obstacles, b.obstacles);
}

TEST(ScenePlay, StealthVolumesMatchInBothApps)
{
    SceneFileData scene;
    ASSERT_TRUE(loadScene("stealth_test.json", scene));
    EXPECT_EQ(scene.quest, "north_lab");
    const SpawnExpect file = expectOf(scene.objects);
    EXPECT_EQ(file.zones, 2);
    EXPECT_EQ(file.pickups, 4);
    EXPECT_GT(file.powers, 0);
    EXPECT_GT(file.conceals, 0);

    World sandbox;
    World editor;
    spawn(sandbox, scene, "sandbox/level");
    spawn(editor, scene, "editor/scene");
    expectMatch(liveCounts(sandbox), file);
    expectMatch(liveCounts(editor), file);
    EXPECT_TRUE(hasZone(sandbox, "facility_gate"));
    EXPECT_TRUE(hasZone(sandbox, "extraction"));
    EXPECT_TRUE(hasZone(editor, "facility_gate"));
    EXPECT_TRUE(hasZone(editor, "extraction"));
    EXPECT_TRUE(hasCircuit(sandbox, "A"));
    EXPECT_TRUE(hasCircuit(sandbox, "B"));
    EXPECT_TRUE(hasCircuit(sandbox, "C"));
    EXPECT_TRUE(hasCircuit(sandbox, "D"));
    EXPECT_TRUE(hasCircuit(editor, "A"));
    EXPECT_TRUE(hasProceduralKey(sandbox, "sandbox/level/power/0"));
    EXPECT_TRUE(hasProceduralKey(editor, "editor/scene/power/0"));
    EXPECT_NE(makeProceduralId("sandbox/level/power/0"), makeProceduralId("editor/scene/power/0"));
}

TEST(ScenePlay, StealthQuestReachAndCollect)
{
    SceneFileData scene;
    ASSERT_TRUE(loadScene("stealth_test.json", scene));
    World world;
    spawn(world, scene, "sandbox/level");

    QuestDef def = northLab();
    ASSERT_EQ(def.id, "north_lab");
    QuestRuntime runtime;
    runtime.start(std::move(def));

    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    InventoryComponent inv{};
    world.emplace<InventoryComponent>(player, inv);

    runtime.tick(world, player);
    EXPECT_FALSE(runtime.state(0).done);

    Math::Vector3f gate{};
    bool           haveGate = false;
    world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
        if (zone.zone == "facility_gate")
        {
            gate     = zone.box.Center();
            haveGate = true;
        }
    });
    ASSERT_TRUE(haveGate);
    world.get<TransformComponent>(player)->position = gate;
    runtime.tick(world, player);
    EXPECT_TRUE(runtime.state(0).done);
    EXPECT_TRUE(runtime.open(1));

    const ItemDef* file = findItemDef("research_file");
    const ItemDef* sample = findItemDef("bio_sample");
    ASSERT_NE(file, nullptr);
    ASSERT_NE(sample, nullptr);
    world.get<InventoryComponent>(player)->bag[0] = ItemStack{ file, 3 };
    runtime.tick(world, player);
    EXPECT_EQ(runtime.state(1).progress, 3);
    EXPECT_TRUE(runtime.state(1).done);

    world.get<InventoryComponent>(player)->bag[1] = ItemStack{ sample, 1 };
    runtime.tick(world, player);
    EXPECT_TRUE(runtime.state(2).done);

    Math::Vector3f exit{};
    bool           haveExit = false;
    world.each<QuestZoneComponent>([&](Entity, QuestZoneComponent& zone) {
        if (zone.zone == "extraction")
        {
            exit     = zone.box.Center();
            haveExit = true;
        }
    });
    ASSERT_TRUE(haveExit);
    world.get<TransformComponent>(player)->position = exit;
    runtime.tick(world, player);
    EXPECT_TRUE(runtime.complete());
}

TEST(PowerPlay, BreakerOffFixAndCheckerPatrol)
{
    World world;
    const Entity box = world.createEntity();
    world.emplace<TransformComponent>(box, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    PowerBoxComponent power{};
    power.circuit = "A";
    power.on      = true;
    power.pos     = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
    world.emplace<PowerBoxComponent>(box, power);
    const Entity lightA = makeLight(world, "A");
    const Entity lightB = makeLight(world, "B");
    const Entity wolf   = makeGuard(world, Math::Vector3f{ 1.0f, 0.0f, 0.0f }, "Wolf");
    const Entity guard  = makeGuard(world, Math::Vector3f{ 5.0f, 0.0f, 0.0f }, "Hunter");
    const Entity far    = makeGuard(world, Math::Vector3f{ 40.0f, 0.0f, 0.0f }, "Hunter");
    (void)wolf;
    (void)far;

    QuestDef def;
    def.id         = "power";
    def.sequential = false;
    ObjectiveDef objective;
    objective.type   = ObjectiveType::Event;
    objective.target = "power_off";
    objective.count  = 1;
    def.objectives.push_back(objective);
    QuestTracker quest;
    quest.start(std::move(def));

    setPowerCircuit(world, box, false, true, nullptr, 1.0f, &quest, nullptr, nullptr);
    EXPECT_FALSE(world.get<PowerBoxComponent>(box)->on);
    EXPECT_FALSE(world.get<LocalLightComponent>(lightA)->enabled);
    EXPECT_TRUE(world.get<LocalLightComponent>(lightB)->enabled);
    EXPECT_EQ(world.get<PowerBoxComponent>(box)->fixer, guard);
    EXPECT_TRUE(quest.state(0).done);

    world.get<TransformComponent>(guard)->position = world.get<PowerBoxComponent>(box)->service;
    tickPower(world, 1.5f, nullptr, 2.0f, &quest, nullptr, nullptr);
    const PowerBoxComponent* fixed = world.get<PowerBoxComponent>(box);
    EXPECT_TRUE(fixed->on);
    EXPECT_TRUE(world.get<LocalLightComponent>(lightA)->enabled);
    EXPECT_EQ(fixed->checker, guard);
    EXPECT_FALSE(fixed->fixer.valid());

    for (int point = 0; point < PowerBoxComponent::kCheckCount; ++point)
    {
        const PowerBoxComponent* live = world.get<PowerBoxComponent>(box);
        ASSERT_EQ(live->checkIdx, point);
        world.get<TransformComponent>(guard)->position = live->checks[point];
        tickPower(world, 2.5f, nullptr, 2.0f, &quest, nullptr, nullptr);
    }
    EXPECT_FALSE(world.get<PowerBoxComponent>(box)->checker.valid());
    EXPECT_EQ(world.get<AiAgentComponent>(guard)->aware.alertness, AI::Alertness::Calm);
}

TEST(PowerPlay, HeardOnlyKeepsTheChecker)
{
    World world;
    const Entity box = world.createEntity();
    world.emplace<PowerBoxComponent>(box);
    world.get<PowerBoxComponent>(box)->checker = makeGuard(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "Hunter");
    const Entity guard = world.get<PowerBoxComponent>(box)->checker;
    AiAgentComponent* ai = world.get<AiAgentComponent>(guard);
    ai->aware.alertness = AI::Alertness::Suspicious;
    ai->aware.heardOnly = true;

    tickPower(world, 0.01f, nullptr, 0.0f, nullptr, nullptr, nullptr);
    EXPECT_EQ(world.get<PowerBoxComponent>(box)->checker, guard);
    EXPECT_EQ(world.get<AiAgentComponent>(guard)->aware.alertness, AI::Alertness::Investigating);

    world.get<AiAgentComponent>(guard)->aware.alertness = AI::Alertness::Suspicious;
    world.get<AiAgentComponent>(guard)->aware.heardOnly = false;
    tickPower(world, 0.01f, nullptr, 0.0f, nullptr, nullptr, nullptr);
    EXPECT_FALSE(world.get<PowerBoxComponent>(box)->checker.valid());
}

TEST(CarryPlay, DropInsideConcealFiresHidden)
{
    SceneFileData scene;
    ASSERT_TRUE(loadScene("stealth_test.json", scene));
    World world;
    spawn(world, scene, "sandbox/level");

    Math::AABox3f patch;
    bool    have = false;
    world.each<ConcealVolumeComponent>([&](Entity, ConcealVolumeComponent& vol) {
        if (have)
            return;
        const Math::Vector3f size = vol.box.Max - vol.box.Min;
        if (size.x > 2.0f && size.z > 2.0f)
        {
            patch = vol.box;
            have  = true;
        }
    });
    ASSERT_TRUE(have);

    QuestDef def;
    def.sequential = false;
    ObjectiveDef objective;
    objective.type   = ObjectiveType::Event;
    objective.target = "body_hidden";
    objective.count  = 1;
    def.objectives.push_back(objective);
    QuestTracker quest;
    quest.start(std::move(def));
    HideHook hook;
    hook.quest = &quest;

    const Math::Vector3f center = patch.Center();
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player, center, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    const Entity body = world.createEntity();
    world.emplace<TransformComponent>(body, center, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<AiAgentComponent>(body);
    ASSERT_TRUE(takeCarriedBody(world, player, body, flatHeight, nullptr));

    CarryView view{};
    view.world        = &world;
    view.carrier      = player;
    view.look         = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
    view.heightAt     = flatHeight;
    view.conceal      = &patch;
    view.concealCount = 1;
    view.onHidden     = onHidden;
    view.user         = &hook;
    dropCarriedBody(view);

    EXPECT_EQ(hook.hits, 1);
    EXPECT_TRUE(quest.state(0).done);
    EXPECT_FALSE(world.get<AiAgentComponent>(body)->carried);
    EXPECT_FALSE(world.has<CarriedByComponent>(body));
}

TEST(SavePlay, BreakerOffAndCarriedBodyRoundTrip)
{
    World world;
    const Entity box = world.createEntity();
    world.emplace<TransformComponent>(box, Math::Vector3f{ 2.0f, 0.0f, 0.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    PowerBoxComponent power{};
    power.circuit = "lab";
    power.on      = false;
    power.pos     = Math::Vector3f{ 2.0f, 0.0f, 0.0f };
    world.emplace<PowerBoxComponent>(box, power);
    stampProceduralId(world, box, "unit/power", "power");
    const Entity light = makeLight(world, "lab");
    world.get<LocalLightComponent>(light)->enabled = true;

    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player);
    stampProceduralId(world, player, "unit/player", "player");
    const Entity body = world.createEntity();
    world.emplace<TransformComponent>(body, Math::Vector3f{ 0.0f, 1.0f, 0.0f }, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
    world.emplace<AiAgentComponent>(body);
    stampProceduralId(world, body, "unit/body", "hunter");
    ASSERT_TRUE(takeCarriedBody(world, player, body, flatHeight, nullptr));

    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);

    world.get<PowerBoxComponent>(box)->on = true;
    world.remove<CarriedByComponent>(body);
    world.get<AiAgentComponent>(body)->carried = false;
    world.get<LocalLightComponent>(light)->enabled = true;

    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    applyDisabledCircuits(world);
    syncCarriedAgents(world);

    EXPECT_FALSE(world.get<PowerBoxComponent>(box)->on);
    EXPECT_EQ(world.get<PowerBoxComponent>(box)->circuit, "lab");
    EXPECT_FALSE(world.get<LocalLightComponent>(light)->enabled);
    const CarriedByComponent* carried = world.get<CarriedByComponent>(body);
    ASSERT_NE(carried, nullptr);
    EXPECT_EQ(carried->carrier, player);
    EXPECT_TRUE(world.get<AiAgentComponent>(body)->carried);
}
