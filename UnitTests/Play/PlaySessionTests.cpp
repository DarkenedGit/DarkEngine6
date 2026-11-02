#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Input/Input.h"
#include "Play/PlaySession.h"
#include "Scene/Building.h"

using namespace Dark;
using namespace Dark::Math;

TEST(PlaySession, ScaleDtSlowsWhileHitStop)
{
    PlaySession play;
    play.runtime().hitStop = 0.2f;
    const float scaled     = play.scaleDt(0.05f);
    EXPECT_FLOAT_EQ(scaled, 0.0025f);
    EXPECT_FLOAT_EQ(play.runtime().hitStop, 0.15f);
}

TEST(PlaySession, ResetPawnKeepsLookAndClearsHitStop)
{
    PlaySession play;
    play.runtime().lookYaw = 1.25f;
    play.runtime().hitStop = 0.4f;
    play.resetPawn();
    EXPECT_FLOAT_EQ(play.runtime().lookYaw, 1.25f);
    EXPECT_FLOAT_EQ(play.runtime().hitStop, 0.0f);
}

TEST(PlaySession, TickPawnWithoutHostIsSafe)
{
    PlaySession play;
    play.tickPawn(0.016f);
}

namespace
{

    struct DoorWalk
    {
        World       world;
        Input       input;
        Entity      player{};
        PlaySession play;

        DoorWalk()
        {
            input.actions().bindKeyAsAxis("move_z", Key::W, 1.0f);
            input.actions().bindKeyAsAxis("move_z", Key::S, -1.0f);
            player = world.createEntity();
            TransformComponent xf{};
            xf.position = Vector3f{ 0.0f, 0.5f, 0.0f };
            world.emplace<TransformComponent>(player, xf);
            world.emplace<HealthComponent>(player);
            world.emplace<PlayerMotorComponent>(player);
            play.bind(&DoorWalk::fill, this);
        }

        static void fill(void* user, PlayHost& host)
        {
            DoorWalk* self   = static_cast<DoorWalk*>(user);
            host.world       = &self->world;
            host.input       = &self->input;
            host.player      = self->player;
            host.heightAt    = [](void*, float, float) { return 0.0f; };
            host.waterLevel  = -1000.0f;
        }

        void hold(Key key)
        {
            input.onKeyUp(static_cast<uint16_t>(Key::W));
            input.onKeyUp(static_cast<uint16_t>(Key::S));
            input.onKeyDown(static_cast<uint16_t>(key), false);
        }

        void place(float z)
        {
            world.get<TransformComponent>(player)->position = Vector3f{ 0.0f, 0.5f, z };
        }

        float z()
        {
            return world.get<TransformComponent>(player)->position.z;
        }
    };

    Entity addShutDoor(DoorWalk& rig, float open)
    {
        const Entity  e = rig.world.createEntity();
        DoorComponent door;
        door.closedPos = Vector3f{ 0.0f, 1.3f, 2.0f };
        door.size      = Vector3f{ 4.0f, 2.6f, 0.18f };
        door.open      = open;
        rig.world.emplace<DoorComponent>(e, door);
        TransformComponent xf{};
        xf.position = door.closedPos;
        xf.scale    = door.size;
        rig.world.emplace<TransformComponent>(e, xf);
        return e;
    }

} // namespace

// Leaf half-thickness 0.09 m. The near face is z = 1.91. A 0.45 m body stops short of that face.
TEST(PlaySession, ShutDoorStopsTheWalk)
{
    DoorWalk rig;
    addShutDoor(rig, 0.0f);
    rig.hold(Key::W);
    rig.play.tickPawn(1.0f);
    EXPECT_GT(rig.z(), 1.2f);
    EXPECT_LT(rig.z(), 1.5f);

    // Already overlapping the leaf, center still outside the solid. Pushing in does not sink.
    rig.place(1.66f);
    rig.play.tickPawn(1.0f);
    EXPECT_NEAR(rig.z(), 1.66f, 0.05f);
}

TEST(PlaySession, OpenDoorLetsTheWalkThroughAndAShutDoorLetsYouBackUp)
{
    DoorWalk rig;
    const Entity door = addShutDoor(rig, 1.0f);
    rig.hold(Key::W);
    rig.play.tickPawn(1.0f);
    EXPECT_GT(rig.z(), 3.4f);

    rig.world.get<DoorComponent>(door)->open = 0.0f;
    rig.place(1.66f);
    rig.hold(Key::S);
    rig.play.tickPawn(1.0f);
    EXPECT_LT(rig.z(), 0.0f);
}
