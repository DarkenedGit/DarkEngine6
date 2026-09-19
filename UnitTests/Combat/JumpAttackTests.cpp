#include <gtest/gtest.h>

#include "Combat/JumpAttack.h"
#include "Combat/JumpAttackComponent.h"
#include "ECS/Entity.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <vector>

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{
    struct FakeWorld
    {
        struct Target
        {
            Vector3f pos{};
            Entity   entity{};
            bool     alive = true;
        };
        std::vector<Target> targets;

        static int count(void* user)
        {
            auto* w = static_cast<FakeWorld*>(user);
            return w ? static_cast<int>(w->targets.size()) : 0;
        }

        static bool aliveAt(void* user, int i)
        {
            auto* w = static_cast<FakeWorld*>(user);
            if (!w || i < 0 || i >= static_cast<int>(w->targets.size()))
                return false;
            return w->targets[static_cast<size_t>(i)].alive;
        }

        static Vector3f center(void* user, int i)
        {
            auto* w = static_cast<FakeWorld*>(user);
            if (!w || i < 0 || i >= static_cast<int>(w->targets.size()))
                return Vector3f{ 0.0f, 0.0f, 0.0f };
            return w->targets[static_cast<size_t>(i)].pos;
        }

        static Entity entityAt(void* user, int i)
        {
            auto* w = static_cast<FakeWorld*>(user);
            if (!w || i < 0 || i >= static_cast<int>(w->targets.size()))
                return {};
            return w->targets[static_cast<size_t>(i)].entity;
        }

        WeaponWorldQuery query()
        {
            WeaponWorldQuery q{};
            q.targetCount     = count;
            q.targetAlive     = aliveAt;
            q.targetCenter    = center;
            q.targetEntityAt  = entityAt;
            q.targetUser      = this;
            return q;
        }
    };

    Entity ent(uint32_t index)
    {
        return Entity{ makeEntityID(index, 1) };
    }

    JumpAttackBegin groundedReq()
    {
        JumpAttackBegin req{};
        req.attacker           = ent(1);
        req.position           = Vector3f{ 0.0f, 0.5f, 0.0f };
        req.lookFlat           = Vector3f{ 0.0f, 0.0f, 1.0f };
        req.heightAboveGround  = 0.0f;
        req.airTime            = 0.0f;
        return req;
    }

    JumpAttackBegin airReq()
    {
        JumpAttackBegin req{};
        req.attacker           = ent(1);
        req.position           = Vector3f{ 0.0f, 2.0f, 0.0f };
        req.lookFlat           = Vector3f{ 0.0f, 0.0f, 1.0f };
        req.velocity           = Vector3f{ 0.0f, 5.0f, 10.0f };
        req.heightAboveGround  = 2.0f;
        req.airTime            = 0.2f;
        return req;
    }

    struct HeightPlane
    {
        float y = 0.0f;
        static float at(void* user, float, float)
        {
            auto* h = static_cast<HeightPlane*>(user);
            return h ? h->y : 0.0f;
        }
    };

    bool stepAutonomous(JumpAttack& ja, Vector3f& pos, float dt, HeightPlane& ground, float waterY, const Vector3f* target, bool hasTarget, bool& landed, bool& splashed)
    {
        ja.tick(dt);
        ja.tickAutonomous(pos, dt, &HeightPlane::at, &ground, waterY, target, hasTarget, landed, splashed);
        return landed || splashed;
    }
} // namespace

TEST(JumpAttack_BeginGrounded, TelegraphSucceedsZeroFails)
{
    JumpAttackDef def{};
    def.telegraphSeconds = 0.40f;
    JumpAttack ja{ def };
    EXPECT_TRUE(ja.begin(groundedReq()));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Telegraph);
    EXPECT_TRUE(ja.busy());
    EXPECT_FALSE(ja.inAirCommit());

    JumpAttackDef player = {};
    player.telegraphSeconds = 0.0f;
    JumpAttack ja2{ player };
    EXPECT_FALSE(ja2.begin(groundedReq()));
    EXPECT_EQ(ja2.phase(), JumpAttackPhase::Idle);
}

TEST(JumpAttack_BeginAirborne, HeightOrAirTimePasses)
{
    JumpAttack jaHigh{};
    JumpAttackBegin high = airReq();
    high.airTime           = 0.0f;
    high.heightAboveGround = 0.45f;
    EXPECT_TRUE(jaHigh.begin(high));
    EXPECT_EQ(jaHigh.phase(), JumpAttackPhase::Leap);

    JumpAttack jaTimed{};
    JumpAttackBegin timed = groundedReq();
    timed.airTime           = 0.10f;
    timed.heightAboveGround = 0.0f;
    EXPECT_TRUE(jaTimed.begin(timed));
    EXPECT_EQ(jaTimed.phase(), JumpAttackPhase::Leap);
}

TEST(JumpAttack_BeginNotIdle, SecondBeginFails)
{
    JumpAttackDef def{};
    def.telegraphSeconds = 0.40f;
    JumpAttack ja{ def };
    ASSERT_TRUE(ja.begin(groundedReq()));
    EXPECT_FALSE(ja.begin(groundedReq()));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Telegraph);

    ja.tick(0.40f);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);
    EXPECT_TRUE(ja.inAirCommit());
    EXPECT_FALSE(ja.begin(airReq()));
}

TEST(JumpAttack_Cooldown, AssignedAtBeginCancelDoesNotRefund)
{
    JumpAttackDef def{};
    def.cooldown         = 1.25f;
    def.telegraphSeconds = 0.40f;
    JumpAttack ja{ def };
    ASSERT_TRUE(ja.begin(groundedReq()));
    EXPECT_NEAR(ja.cooldownLeft(), 1.25f, 1.0e-4f);
    EXPECT_FALSE(ja.begin(groundedReq()));

    ja.cancel(JumpAttackCancel::NoPound);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Idle);
    EXPECT_NEAR(ja.cooldownLeft(), 1.25f, 1.0e-4f);
    EXPECT_FALSE(ja.begin(groundedReq()));

    ja.tick(1.25f);
    EXPECT_NEAR(ja.cooldownLeft(), 0.0f, 1.0e-4f);
    EXPECT_TRUE(ja.begin(groundedReq()));
}

TEST(JumpAttack_Cooldown, TickAlwaysDecrementsWhileIdle)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    ja.cancel(JumpAttackCancel::NoPound);
    const float before = ja.cooldownLeft();
    ja.tick(0.25f);
    EXPECT_NEAR(ja.cooldownLeft(), before - 0.25f, 1.0e-4f);
}

TEST(JumpAttack_ConnectWindow, InRangeOneEventHitSetSize1)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);
    EXPECT_NEAR(ja.connectWindowLeft(), 0.55f, 1.0e-4f);

    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.5f }, ent(2), true });

    DamageEvent ev{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Connected);
    EXPECT_EQ(ja.connectedTarget().id(), ent(2).id());
    EXPECT_EQ(ev.target.id(), ent(2).id());
    EXPECT_EQ(ev.source.id(), ent(1).id());
    EXPECT_FLOAT_EQ(ev.amount, 32.0f);
    EXPECT_FLOAT_EQ(ev.poiseDamage, 28.0f);
    EXPECT_EQ(ev.type, DamageType::Blunt);
    EXPECT_FLOAT_EQ(ev.statusDuration, 1.4f);
    EXPECT_FLOAT_EQ(ev.statusMagnitude, 2.4f);
    EXPECT_EQ(ev.flags, ja.def().connectFlags);
    EXPECT_NE(ev.flags & (1u << 7), 0u);
    EXPECT_NE(ev.flags & DamageFlags::HardCc, 0u);
    EXPECT_NE(ev.flags & DamageFlags::CanBlock, 0u);
    EXPECT_EQ(ev.flags & DamageFlags::CanParry, 0u);
}

TEST(JumpAttack_ConnectTwice, SecondFalse)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.2f }, ent(3), true });

    DamageEvent a{};
    DamageEvent b{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, a));
    EXPECT_FALSE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, b));
    EXPECT_EQ(ja.connectedTarget().id(), a.target.id());
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Connected);
}

TEST(JumpAttack_ConnectWindow, MissThenPoundNoConnect)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    ja.tick(0.55f);
    EXPECT_NEAR(ja.connectWindowLeft(), 0.0f, 1.0e-4f);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);

    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 0.5f, 1.0f }, ent(2), true });
    DamageEvent miss{};
    EXPECT_FALSE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, miss));

    const Vector3f land{ 0.0f, 0.5f, 0.0f };
    ja.onLanded(land);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Pound);

    DamageEvent events[8]{};
    const int   n = ja.tryPound(world.query(), land, events, 8);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(events[0].target.id(), ent(2).id());
    EXPECT_FLOAT_EQ(events[0].amount, 20.0f);
    EXPECT_FLOAT_EQ(events[0].poiseDamage, 16.0f);
    EXPECT_FLOAT_EQ(events[0].statusDuration, 0.85f);
    EXPECT_EQ(events[0].flags, ja.def().poundFlags);
    EXPECT_NE(events[0].flags & DamageFlags::CanParry, 0u);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Recover);
}

TEST(JumpAttack_ConnectThenPound, PouncedExcludedOthersIncluded)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 2.0f, 0.5f, 0.0f }, ent(3), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 0.5f, 2.0f }, ent(4), true });

    DamageEvent connect{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, connect));
    EXPECT_EQ(connect.target.id(), ent(2).id());

    const Vector3f land{ 0.0f, 0.5f, 0.0f };
    ja.onLanded(land);
    DamageEvent events[8]{};
    const int   n = ja.tryPound(world.query(), land, events, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(ja.connectedTarget().id(), ent(2).id());
    bool saw3 = false;
    bool saw4 = false;
    for (int i = 0; i < n; ++i)
    {
        EXPECT_NE(events[i].target.id(), ent(2).id());
        if (events[i].target.id() == ent(3).id())
            saw3 = true;
        if (events[i].target.id() == ent(4).id())
            saw4 = true;
    }
    EXPECT_TRUE(saw3);
    EXPECT_TRUE(saw4);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Recover);
}

TEST(JumpAttack_TryPound, SecondReturnsZero)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 0.5f, 1.0f }, ent(2), true });
    ja.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    DamageEvent events[4]{};
    EXPECT_GT(ja.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, events, 4), 0);
    EXPECT_EQ(ja.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, events, 4), 0);
}

TEST(JumpAttack_ForgotTryPound, TickEntersRecoverNoEvents)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    ja.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Pound);
    ja.tick(1.0f / 60.0f);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Recover);
    EXPECT_FALSE(ja.connectedTarget().valid());

    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 0.5f, 1.0f }, ent(2), true });
    DamageEvent events[4]{};
    EXPECT_EQ(ja.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, events, 4), 0);
}

TEST(JumpAttack_SplashCancel, ProducesNoPound)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 0.5f, 1.0f }, ent(2), true });
    ja.onSplashed();
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(ja.busy());
    DamageEvent events[4]{};
    EXPECT_EQ(ja.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, events, 4), 0);
    EXPECT_FALSE(ja.connectedTarget().valid());
}

TEST(JumpAttack_Filters, RadiusVerticalSlopLookCone)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    const Vector3f attacker{ 0.0f, 2.0f, 0.0f };
    const Vector3f look{ 0.0f, 0.0f, 1.0f };

    FakeWorld farWorld;
    farWorld.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 4.0f }, ent(2), true });
    DamageEvent ev{};
    EXPECT_FALSE(ja.tryConnect(farWorld.query(), attacker, look, ev));

    FakeWorld vertWorld;
    vertWorld.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 4.0f, 1.0f }, ent(3), true });
    EXPECT_FALSE(ja.tryConnect(vertWorld.query(), attacker, look, ev));

    FakeWorld coneWorld;
    coneWorld.targets.push_back(FakeWorld::Target{ Vector3f{ 2.0f, 2.0f, 0.1f }, ent(4), true });
    EXPECT_FALSE(ja.tryConnect(coneWorld.query(), attacker, look, ev));

    FakeWorld okWorld;
    okWorld.targets.push_back(FakeWorld::Target{ Vector3f{ 0.5f, 2.0f, 2.0f }, ent(5), true });
    EXPECT_TRUE(ja.tryConnect(okWorld.query(), attacker, look, ev));
    EXPECT_EQ(ev.target.id(), ent(5).id());
}

TEST(JumpAttack_Filters, PoundRadiusAndVerticalSlop)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    const Vector3f land{ 0.0f, 0.5f, 0.0f };
    ja.onLanded(land);

    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 4.0f, 0.5f, 0.0f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 1.0f, 3.0f, 0.0f }, ent(3), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 1.0f, 0.5f, 0.0f }, ent(4), true });
    DamageEvent events[8]{};
    const int   n = ja.tryPound(world.query(), land, events, 8);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(events[0].target.id(), ent(4).id());
}

TEST(JumpAttack_Filters, SkipDeadAndSelf)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(1), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.2f }, ent(2), false });
    DamageEvent ev{};
    EXPECT_FALSE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
}

TEST(JumpAttack_IntendedTarget, WinsOverNearest)
{
    JumpAttack ja{};
    JumpAttackBegin req = airReq();
    req.intendedTarget  = ent(9);
    ASSERT_TRUE(ja.begin(req));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 0.8f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 2.0f }, ent(9), true });
    DamageEvent ev{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ev.target.id(), ent(9).id());
    EXPECT_EQ(ja.connectedTarget().id(), ent(9).id());
}

TEST(JumpAttack_IntendedTarget, OutOfRangeDoesNotFallBackToNearest)
{
    JumpAttack ja{};
    JumpAttackBegin req = airReq();
    req.intendedTarget  = ent(9);
    ASSERT_TRUE(ja.begin(req));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 4.0f }, ent(9), true });
    DamageEvent ev{};
    EXPECT_FALSE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);
    EXPECT_FALSE(ja.connectedTarget().valid());
}

TEST(JumpAttack_IntendedTarget, DeadDoesNotFallBackToNearest)
{
    JumpAttack ja{};
    JumpAttackBegin req = airReq();
    req.intendedTarget  = ent(9);
    ASSERT_TRUE(ja.begin(req));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(2), true });
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.2f }, ent(9), false });
    DamageEvent ev{};
    EXPECT_FALSE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);
    EXPECT_FALSE(ja.connectedTarget().valid());
}

TEST(JumpAttack_IntendedTarget, IgnoresLookCone)
{
    JumpAttack ja{};
    JumpAttackBegin req = airReq();
    req.intendedTarget  = ent(9);
    ASSERT_TRUE(ja.begin(req));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 2.0f, 2.0f, 0.1f }, ent(9), true });
    DamageEvent ev{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ev.target.id(), ent(9).id());
}

TEST(JumpAttack_Connect, OverlappingHorizontalConnects)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 0.0f }, ent(2), true });
    DamageEvent ev{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    EXPECT_EQ(ev.target.id(), ent(2).id());
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Connected);
}

TEST(JumpAttack_Query, InvalidCallbacksNoHits)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    WeaponWorldQuery empty{};
    DamageEvent      ev{};
    EXPECT_FALSE(ja.tryConnect(empty, Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    ja.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    EXPECT_EQ(ja.tryPound(empty, Vector3f{ 0.0f, 0.5f, 0.0f }, &ev, 1), 0);
}

TEST(JumpAttack_LandLag, ConnectedVsWhiff)
{
    JumpAttack connected{};
    ASSERT_TRUE(connected.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.0f }, ent(2), true });
    DamageEvent ev{};
    ASSERT_TRUE(connected.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));
    connected.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    DamageEvent buf[4]{};
    connected.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, buf, 4);
    connected.tick(0.22f);
    EXPECT_EQ(connected.phase(), JumpAttackPhase::Idle);

    JumpAttack whiff{};
    ASSERT_TRUE(whiff.begin(airReq()));
    whiff.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    whiff.tryPound(world.query(), Vector3f{ 0.0f, 0.5f, 0.0f }, buf, 4);
    whiff.tick(0.22f);
    EXPECT_EQ(whiff.phase(), JumpAttackPhase::Recover);
    whiff.tick(0.18f);
    EXPECT_EQ(whiff.phase(), JumpAttackPhase::Idle);
}

TEST(JumpAttack_AirSteering, DtStableRemainingError)
{
    JumpAttackDef def{};
    def.homingRate       = 6.0f;
    def.leapForwardSpeed = 10.0f;
    JumpAttack ja{ def };

    const Vector3f pos{ 0.0f, 2.0f, 0.0f };
    const Vector3f target{ 0.0f, 2.0f, 10.0f };
    const Vector3f look{ 0.0f, 0.0f, 1.0f };
    const float    tEnd = 0.2f;
    const float    keep = expf(-6.0f * tEnd);

    auto run = [&](float dt) {
        Vector3f vel{ 10.0f, 3.0f, 0.0f };
        float    t = 0.0f;
        while (t + 1.0e-8f < tEnd)
        {
            const float step = Math::Min(dt, tEnd - t);
            ja.applyAirSteering(vel, pos, look, &target, true, step);
            t += step;
        }
        return vel;
    };

    const Vector3f a60 = run(1.0f / 60.0f);
    const Vector3f a30 = run(1.0f / 30.0f);
    EXPECT_NEAR(a60.x, 10.0f * keep, 1.0e-4f);
    EXPECT_NEAR(a60.z, 10.0f * (1.0f - keep), 1.0e-4f);
    EXPECT_NEAR(a60.y, 3.0f, 1.0e-5f);
    EXPECT_NEAR(a30.x, a60.x, 1.0e-4f);
    EXPECT_NEAR(a30.z, a60.z, 1.0e-4f);
    EXPECT_NEAR(a30.y, 3.0f, 1.0e-5f);
}

TEST(JumpAttack_AirSteering, HomingRateZeroBallistic)
{
    JumpAttackDef def{};
    def.homingRate = 0.0f;
    JumpAttack ja{ def };
    Vector3f   vel{ 4.0f, 2.0f, 1.0f };
    const Vector3f target{ 0.0f, 0.0f, 10.0f };
    ja.applyAirSteering(vel, Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, &target, true, 1.0f / 30.0f);
    EXPECT_FLOAT_EQ(vel.x, 4.0f);
    EXPECT_FLOAT_EQ(vel.y, 2.0f);
    EXPECT_FLOAT_EQ(vel.z, 1.0f);
}

TEST(JumpAttack_ConnectSnap, LerpsXzLeavesY)
{
    JumpAttack ja{};
    ASSERT_TRUE(ja.begin(airReq()));
    FakeWorld world;
    world.targets.push_back(FakeWorld::Target{ Vector3f{ 0.0f, 2.0f, 1.5f }, ent(2), true });
    DamageEvent ev{};
    ASSERT_TRUE(ja.tryConnect(world.query(), Vector3f{ 0.0f, 2.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f }, ev));

    Vector3f pos{ 0.0f, 5.0f, 0.0f };
    ja.applyConnectSnap(pos, Vector3f{ 2.0f, 9.0f, 2.0f }, 0.04f);
    EXPECT_NEAR(pos.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(pos.z, 1.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(pos.y, 5.0f);
}

TEST(JumpAttack_AutonomousSplash, LakeBedReportsSplashedNotLanded)
{
    JumpAttackDef def{};
    def.telegraphSeconds = 0.0f;
    def.groundOffset     = 1.0f;
    JumpAttack ja{ def };
    JumpAttackBegin req = airReq();
    req.velocity        = Vector3f{ 0.0f, -8.0f, 0.0f };
    req.position        = Vector3f{ 0.0f, 1.0f, 0.0f };
    ASSERT_TRUE(ja.begin(req));

    HeightPlane lake;
    lake.y = -1.0f;
    const float waterY = 0.0f;
    Vector3f    pos    = req.position;
    bool        landed = false;
    bool        splashed = false;
    float       t        = 0.0f;
    const float dt       = 1.0f / 60.0f;
    while (t < 2.0f && !landed && !splashed)
    {
        stepAutonomous(ja, pos, dt, lake, waterY, nullptr, false, landed, splashed);
        t += dt;
    }
    EXPECT_TRUE(splashed);
    EXPECT_FALSE(landed);
}

void runTakeoffBand(float dist)
{
    JumpAttackDef def{};
    def.telegraphSeconds    = 0.40f;
    def.groundOffset        = 1.0f;
    def.homingRate          = 0.0f;
    def.leapVerticalSpeed   = 10.0f;
    def.gravity             = 24.0f;
    def.leapForwardSpeedMax = 12.0f;
    JumpAttack ja{ def };

    JumpAttackBegin req = groundedReq();
    req.position        = Vector3f{ 0.0f, 1.0f, 0.0f };
    req.lookFlat        = Vector3f{ 1.0f, 0.0f, 0.0f };
    req.intendedTarget  = ent(2);
    ASSERT_TRUE(ja.begin(req));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Telegraph);

    HeightPlane ground;
    ground.y              = 0.0f;
    Vector3f       pos    = req.position;
    const Vector3f target = Vector3f{ dist, 1.0f, 0.0f };
    bool           landed = false;
    bool           splashed = false;
    const float    dt     = 1.0f / 60.0f;
    ja.tickAutonomous(pos, dt, &HeightPlane::at, &ground, -100.0f, &target, true, landed, splashed);
    EXPECT_FALSE(landed);
    EXPECT_NEAR(pos.y, 1.0f, 1.0e-4f);

    ja.tick(0.40f);
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);

    ja.tickAutonomous(pos, dt, &HeightPlane::at, &ground, -100.0f, &target, true, landed, splashed);
    const float hang    = 2.0f * 10.0f / 24.0f;
    const float wantSpd = Math::Clamp(dist / hang, 0.0f, 12.0f);
    const float horiz   = sqrtf(ja.velocity().x * ja.velocity().x + ja.velocity().z * ja.velocity().z);
    EXPECT_NEAR(horiz, wantSpd, 0.05f);
    EXPECT_GT(ja.velocity().y, 0.0f);

    float       t         = dt;
    float       maxY      = pos.y;
    const float landPlane = 1.0f;
    while (t < 2.0f && !landed && !splashed)
    {
        EXPECT_GT(pos.y, landPlane - 1.0e-3f);
        if (t < 0.40f)
            EXPECT_GT(pos.y, landPlane + 0.10f);
        stepAutonomous(ja, pos, dt, ground, -100.0f, &target, true, landed, splashed);
        maxY = Math::Max(maxY, pos.y);
        t += dt;
    }
    EXPECT_FALSE(splashed);
    EXPECT_TRUE(landed);
    EXPECT_NEAR(t, hang, 0.06f);
    EXPECT_GT(maxY, landPlane + 1.5f);
    EXPECT_NEAR(pos.y, landPlane, 1.0e-3f);
}

TEST(JumpAttack_AutonomousTakeoff, FourAndNineMetersHangNoMidArcSnap)
{
    ASSERT_NO_FATAL_FAILURE(runTakeoffBand(4.0f));
    ASSERT_NO_FATAL_FAILURE(runTakeoffBand(9.0f));
}

TEST(JumpAttack_AutonomousTakeoff, PlayerBeginDoesNotOverwriteVelocity)
{
    JumpAttack ja{};
    JumpAttackBegin req = airReq();
    req.velocity        = Vector3f{ 3.0f, 7.0f, 1.0f };
    ASSERT_TRUE(ja.begin(req));
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Leap);
    EXPECT_FLOAT_EQ(ja.velocity().x, 3.0f);
    EXPECT_FLOAT_EQ(ja.velocity().y, 7.0f);
    EXPECT_FLOAT_EQ(ja.velocity().z, 1.0f);

    HeightPlane ground;
    Vector3f    pos{ 0.0f, 2.0f, 0.0f };
    bool        landed   = false;
    bool        splashed = false;
    ja.tickAutonomous(pos, 1.0f / 60.0f, &HeightPlane::at, &ground, -100.0f, nullptr, false, landed, splashed);
    EXPECT_NEAR(ja.velocity().y, 7.0f - 24.0f / 60.0f, 1.0e-4f);
}

TEST(JumpAttack_OnLanded, IgnoredUnlessLeapOrConnected)
{
    JumpAttackDef def{};
    def.telegraphSeconds = 0.40f;
    JumpAttack ja{ def };
    ASSERT_TRUE(ja.begin(groundedReq()));
    ja.onLanded(Vector3f{ 0.0f, 0.5f, 0.0f });
    EXPECT_EQ(ja.phase(), JumpAttackPhase::Telegraph);
}

TEST(JumpAttack_Component, WrapsMachine)
{
    JumpAttackComponent c;
    EXPECT_STREQ(JumpAttackComponent::kTypeName, "JumpAttack");
    EXPECT_EQ(c.jump.phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(c.jump.busy());
}
