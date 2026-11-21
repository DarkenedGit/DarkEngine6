#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Core/TimerQueue.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Input/Input.h"
#include "Play/PlaySession.h"

using namespace Dark;

namespace
{

    struct Counter
    {
        int fires = 0;
        static void onFire(void* user) noexcept { static_cast<Counter*>(user)->fires += 1; }
    };

    struct SimRig
    {
        World       world;
        Input       input;
        Entity      player{};
        PlaySession play;

        SimRig()
        {
            input.actions().bindKeyAsAxis("move_z", Key::W, 1.0f);
            input.actions().bindKeyAsAxis("move_z", Key::S, -1.0f);
            player = world.createEntity();
            TransformComponent xf{};
            xf.position = Math::Vector3f{0.0f, 0.5f, 0.0f};
            world.emplace<TransformComponent>(player, xf);
            world.emplace<HealthComponent>(player);
            world.emplace<PlayerMotorComponent>(player);
            play.bind(&SimRig::fill, this);
        }

        static void fill(void* user, PlayHost& host)
        {
            SimRig* self    = static_cast<SimRig*>(user);
            host.world      = &self->world;
            host.input      = &self->input;
            host.player     = self->player;
            host.heightAt   = [](void*, float, float) { return 0.0f; };
            host.waterLevel = -1000.0f;
        }
    };

    TimerHandle arm(PlaySession& play, Counter& counter, double delay)
    {
        TimerDesc desc{};
        desc.delaySec = delay;
        desc.mode     = TimerMode::Once;
        desc.fn       = &Counter::onFire;
        desc.user     = &counter;
        return play.simTimers().schedule(desc);
    }

} // namespace

TEST(PlaySessionTimers, PausedTickDoesNotFireAndSimStepDoes)
{
    SimRig  rig;
    Counter counter;
    const TimerHandle h = arm(rig.play, counter, 0.0);
    EXPECT_TRUE(h.valid());

    rig.play.setSimPaused(true);
    rig.play.tickSimulation(rig.play.scaleDt(0.1f));
    EXPECT_EQ(counter.fires, 0);
    EXPECT_TRUE(rig.play.simTimers().alive(h));

    rig.play.requestSimStep();
    rig.play.tickSimulation(rig.play.scaleDt(0.1f));
    EXPECT_EQ(counter.fires, 1);
    EXPECT_FALSE(rig.play.simTimers().alive(h));
}

TEST(PlaySessionTimers, TimeScaleDelaysOneShot)
{
    SimRig rig;
    rig.play.setSimTimeScale(0.25f);
    Counter counter;
    arm(rig.play, counter, 0.1);

    int steps = 0;
    while (counter.fires == 0 && steps < 8)
    {
        rig.play.tickSimulation(rig.play.scaleDt(0.1f));
        steps += 1;
    }
    EXPECT_EQ(counter.fires, 1);
    EXPECT_GT(steps, 1);
}

TEST(PlaySessionTimers, HitStopSlowsOneShot)
{
    SimRig rig;
    rig.play.runtime().hitStop = 0.5f;
    Counter counter;
    const TimerHandle h = arm(rig.play, counter, 0.1);
    rig.play.tickSimulation(rig.play.scaleDt(0.1f));
    EXPECT_EQ(counter.fires, 0);
    EXPECT_TRUE(rig.play.simTimers().alive(h));
    EXPECT_LT(rig.play.runtime().hitStop, 0.5f);
}

TEST(PlaySessionTimers, ResetPawnDropsArmedTimer)
{
    SimRig  rig;
    Counter counter;
    const TimerHandle h = arm(rig.play, counter, 0.0);
    EXPECT_TRUE(rig.play.simTimers().alive(h));
    rig.play.resetPawn();
    EXPECT_FALSE(rig.play.simTimers().alive(h));
    EXPECT_EQ(rig.play.simTimers().liveCount(), 0);
    rig.play.tickSimulation(rig.play.scaleDt(0.1f));
    EXPECT_EQ(counter.fires, 0);
}
