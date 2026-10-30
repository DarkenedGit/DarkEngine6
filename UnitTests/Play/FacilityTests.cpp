#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/PlayerStealth.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Upgrades.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"
#include "Play/Facility.h"
#include "Play/Interaction.h"
#include "Scene/Building.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    float flatGround(void*, float, float)
    {
        return 0.0f;
    }

    Entity addBody(World& world, const Vector3f& pos, bool alive)
    {
        Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(e, xf);
        HealthComponent hp{};
        if (!alive)
            hp.health.applyDamage(1000.0f);
        world.emplace<HealthComponent>(e, std::move(hp));
        return e;
    }

    Entity addAgent(World& world, const Vector3f& pos, const char* tag, bool alive = true)
    {
        Entity e = addBody(world, pos, alive);
        if (tag && tag[0])
        {
            TagComponent name;
            name.name = tag;
            world.emplace<TagComponent>(e, std::move(name));
        }
        world.emplace<AiAgentComponent>(e);
        return e;
    }

    Entity addDoor(World& world, const Vector3f& pos, const char* lock)
    {
        Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(e, xf);
        DoorComponent door;
        door.closedPos = pos;
        door.size      = Vector3f{ 1.2f, 2.4f, 0.2f };
        if (lock)
            door.lock = lock;
        world.emplace<DoorComponent>(e, std::move(door));
        return e;
    }

    FacilityHost hostFor(World& world, AiSystem& ai, Entity player)
    {
        FacilityHost host;
        host.world     = &world;
        host.ai        = &ai;
        host.player    = player;
        host.listener  = player;
        host.heightAt  = flatGround;
        return host;
    }

    DoorComponent* doorOf(World& world, Entity e)
    {
        return world.get<DoorComponent>(e);
    }

} // namespace

TEST(Facility, LockedDoorStaysShutForAGuard)
{
    World    world;
    AiSystem ai;
    const Entity guard = addAgent(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "Hunter");
    const Entity door  = addDoor(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "keycard_red");
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, {});
    tickFacility(state, host, 0.2f);
    EXPECT_FALSE(doorOf(world, door)->wantOpen);
    EXPECT_FLOAT_EQ(doorOf(world, door)->open, 0.0f);
    (void)guard;

    doorOf(world, door)->lock.clear();
    tickFacility(state, host, 0.1f);
    EXPECT_TRUE(doorOf(world, door)->wantOpen);
    EXPECT_NEAR(doorOf(world, door)->open, 0.2f, 1.0e-4f);
}

TEST(Facility, DoorClosesWhenNobodyLivingIsNear)
{
    World    world;
    AiSystem ai;
    const Entity door  = addDoor(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "");
    const Entity guard = addAgent(world, Vector3f{ 6.0f, 0.0f, 0.0f }, "Hunter");
    const Entity player = addBody(world, Vector3f{ 0.2f, 0.0f, 0.0f }, false);
    doorOf(world, door)->wantOpen = true;
    doorOf(world, door)->open     = 1.0f;
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, player);
    tickFacility(state, host, 0.2f);
    EXPECT_FALSE(doorOf(world, door)->wantOpen);
    EXPECT_LT(doorOf(world, door)->open, 1.0f);
    (void)guard;
}

TEST(Facility, PlayerKeepsAnOpenDoorOpenAndDoesNotForceOne)
{
    World    world;
    AiSystem ai;
    const Entity door   = addDoor(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "");
    const Entity player = addBody(world, Vector3f{ 0.4f, 0.0f, 0.0f }, true);
    doorOf(world, door)->wantOpen = true;
    doorOf(world, door)->open     = 1.0f;
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, player);
    tickFacility(state, host, 0.2f);
    EXPECT_TRUE(doorOf(world, door)->wantOpen);
    EXPECT_FLOAT_EQ(doorOf(world, door)->open, 1.0f);

    doorOf(world, door)->wantOpen = false;
    doorOf(world, door)->open     = 0.0f;
    tickFacility(state, host, 0.2f);
    EXPECT_FALSE(doorOf(world, door)->wantOpen);
    EXPECT_FLOAT_EQ(doorOf(world, door)->open, 0.0f);
}

TEST(Facility, WolfDoesNotOpenAnUnlockedDoor)
{
    World    world;
    AiSystem ai;
    const Entity wolf = addAgent(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "Wolf");
    const Entity door = addDoor(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "");
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, {});
    tickFacility(state, host, 0.2f);
    EXPECT_FALSE(doorOf(world, door)->wantOpen);
    EXPECT_FLOAT_EQ(doorOf(world, door)->open, 0.0f);
    (void)wolf;
}

TEST(Facility, ShutDoorIsABlockerUntilItIsMostlyOpen)
{
    World    world;
    AiSystem ai;
    const Entity guard = addAgent(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "Hunter");
    const Entity door  = addDoor(world, Vector3f{ 0.0f, 0.0f, 0.0f }, "keycard_red");
    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 20.0f, 1.0f, 20.0f }, Vector3f{ 1.0f, 1.0f, 1.0f });
    FacilityState state;
    FacilityHost  host = hostFor(world, ai, {});
    host.cubes         = &cube;
    host.cubeCount     = 1;

    tickFacility(state, host, 0.05f);
    EXPECT_EQ(state.blockCount, 2);
    const AABox3f* copied = state.blocks;
    EXPECT_NE(copied, &cube);
    tickFacility(state, host, 0.05f);
    EXPECT_EQ(state.blocks, copied);
    EXPECT_EQ(state.blockCount, 2);

    doorOf(world, door)->lock.clear();
    tickFacility(state, host, 0.5f);
    EXPECT_GE(doorOf(world, door)->open, 0.8f);
    EXPECT_EQ(state.blockCount, 1);
    EXPECT_EQ(state.blocks, &cube);
    (void)guard;
}

TEST(Facility, WolfIsNotAHideWitnessAndACloseGuardPullsThePlayerOut)
{
    World    world;
    AiSystem ai;
    const Entity player = addBody(world, Vector3f{ 0.0f, 1.0f, 0.0f }, true);
    world.emplace<ModelComponent>(player);
    world.emplace<PlayerMotorComponent>(player);
    world.get<PlayerMotorComponent>(player)->motor.setHorizontalVelocity(3.0f, 4.0f);

    const Entity spot = world.createEntity();
    TransformComponent sxf{};
    sxf.position = Vector3f{ 0.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(spot, sxf);
    HideSpotComponent hide;
    hide.front = Vector3f{ 1.0f, 0.0f, 0.0f };
    hide.label = "Locker";
    world.emplace<HideSpotComponent>(spot, std::move(hide));

    const Entity wolf  = addAgent(world, Vector3f{ 0.5f, 1.0f, 0.0f }, "Wolf");
    const Entity guard = addAgent(world, Vector3f{ 2.0f, 1.0f, 0.0f }, "Hunter");
    world.get<AiAgentComponent>(guard)->aware.alertness = AI::Alertness::Searching;

    FacilityState state;
    FacilityHost  host = hostFor(world, ai, player);
    PlayInteractPick pick;
    pick.kind = PlayInteract::Hide;
    pick.e    = spot;
    EXPECT_TRUE(handleFacilityUse(state, host, pick));
    EXPECT_EQ(state.hideWitness, guard);
    EXPECT_FALSE(world.get<ModelComponent>(player)->visible);
    EXPECT_FLOAT_EQ(world.get<TransformComponent>(player)->position.y, 0.5f);

    tickFacility(state, host, 0.05f);
    EXPECT_FLOAT_EQ(world.get<PlayerMotorComponent>(player)->motor.velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(world.get<PlayerMotorComponent>(player)->motor.velocity().z, 0.0f);
    EXPECT_TRUE(state.hiddenIn.valid());

    tickFacility(state, host, 0.44f);
    EXPECT_TRUE(state.hiddenIn.valid());
    tickFacility(state, host, 0.02f);
    EXPECT_FALSE(state.hiddenIn.valid());
    EXPECT_TRUE(world.get<ModelComponent>(player)->visible);
    EXPECT_EQ(world.get<AiAgentComponent>(guard)->aware.alertness, AI::Alertness::Alert);
    EXPECT_EQ(world.get<AiAgentComponent>(wolf)->aware.alertness, AI::Alertness::Calm);
    EXPECT_NEAR(world.get<TransformComponent>(player)->position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(world.get<TransformComponent>(player)->position.y, 0.5f, 1.0e-4f);
}

TEST(Facility, CameraAlarmAlertsHuntersAndSkipsWolves)
{
    World    world;
    AiSystem ai;
    const Entity player = addBody(world, Vector3f{ 0.0f, 0.0f, 5.0f }, true);
    const Entity hunter = addAgent(world, Vector3f{ 10.0f, 0.0f, 0.0f }, "Hunter");
    const Entity wolf   = addAgent(world, Vector3f{ 12.0f, 0.0f, 0.0f }, "Wolf");

    const Entity cam = world.createEntity();
    TransformComponent cxf{};
    world.emplace<TransformComponent>(cam, cxf);
    SecurityCameraComponent camera;
    camera.def.pos   = Vector3f{ 0.0f, 0.0f, 0.0f };
    camera.def.yaw   = 0.0f;
    camera.def.range = 14.0f;
    camera.def.sweep = 0.0f;
    camera.aimYaw    = 0.0f;
    world.emplace<SecurityCameraComponent>(cam, std::move(camera));

    FacilityState state;
    FacilityHost  host = hostFor(world, ai, player);
    tickFacility(state, host, 0.6f);
    EXPECT_GT(state.alarmTime, 0.0f);
    EXPECT_EQ(world.get<AiAgentComponent>(hunter)->aware.alertness, AI::Alertness::Alert);
    EXPECT_EQ(world.get<AiAgentComponent>(wolf)->aware.alertness, AI::Alertness::Calm);
    EXPECT_FALSE(state.alarmTone);
}

TEST(Facility, UpgradesApplyOnPurchaseAndDoNotRewriteTheMotorEveryFrame)
{
    World    world;
    AiSystem ai;
    const Entity player = addBody(world, Vector3f{ 0.0f, 0.0f, 0.0f }, true);
    world.emplace<PlayerMotorComponent>(player);
    world.emplace<PlayerStealthComponent>(player);
    PlayerMotor& motor = world.get<PlayerMotorComponent>(player)->motor;
    EXPECT_FLOAT_EQ(motor.settings().walkSpeed, 3.6f);

    FacilityState state;
    UpgradeDef    legs;
    legs.id     = "fast_legs";
    legs.effect = "speed";
    legs.value  = 1.1f;
    state.defs.push_back(legs);
    state.owned.push_back("fast_legs");

    FacilityHost host = hostFor(world, ai, player);
    refreshFacilityUpgrades(state, host);
    EXPECT_NEAR(motor.settings().walkSpeed, 3.96f, 1.0e-4f);
    EXPECT_NEAR(motor.settings().sprintSpeed, 7.7f, 1.0e-4f);

    motor.settings().walkSpeed = 9.0f;
    world.get<PlayerStealthComponent>(player)->exposureScale = 0.2f;
    tickFacility(state, host, 0.016f);
    EXPECT_FLOAT_EQ(motor.settings().walkSpeed, 9.0f);
    EXPECT_FLOAT_EQ(world.get<PlayerStealthComponent>(player)->exposureScale, 1.0f);

    const Entity marker = world.createEntity();
    world.emplace<TransformComponent>(marker, TransformComponent{});
    state.hiddenIn = marker;
    tickFacility(state, host, 0.016f);
    EXPECT_TRUE(world.get<PlayerStealthComponent>(player)->hidden);
    EXPECT_FLOAT_EQ(motor.settings().walkSpeed, 9.0f);
    state.hiddenIn = {};
    tickFacility(state, host, 0.016f);
    EXPECT_FALSE(world.get<PlayerStealthComponent>(player)->hidden);
    EXPECT_FLOAT_EQ(motor.settings().walkSpeed, 9.0f);

    clearFacilitySession(state, &world, player);
    EXPECT_NEAR(motor.settings().walkSpeed, 3.6f, 1.0e-4f);
    EXPECT_NEAR(motor.settings().sprintSpeed, 7.0f, 1.0e-4f);
    EXPECT_TRUE(state.owned.empty());

    state.owned.push_back("fast_legs");
    refreshFacilityUpgrades(state, host);
    EXPECT_NEAR(motor.settings().walkSpeed, 3.96f, 1.0e-4f);
    EXPECT_NEAR(motor.settings().sprintSpeed, 7.7f, 1.0e-4f);
}
