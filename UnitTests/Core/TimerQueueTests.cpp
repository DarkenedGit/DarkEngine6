#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Core/MainThread.h"
#include "Core/TimerQueue.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Dark;

namespace
{

    std::string* g_timerLog = nullptr;

    void captureTimerLog(LogLevel, LogCategory, const char* message)
    {
        if (g_timerLog && message)
            *g_timerLog += message;
    }

    struct CaptureLog
    {
        std::string text;
        CaptureLog()
        {
            g_timerLog = &text;
            Log::setCapture(&captureTimerLog);
        }
        ~CaptureLog()
        {
            Log::setCapture(nullptr);
            g_timerLog = nullptr;
        }
    };

    struct UnbindMainThread
    {
        ~UnbindMainThread() { unbindMainThread(); }
    };

    struct Counter
    {
        int fires = 0;
        static void onFire(void* user) noexcept { static_cast<Counter*>(user)->fires += 1; }
    };

    TimerHandle arm(TimerQueue& queue, TimerFn fn, void* user, double delay, TimerMode mode, const char* name = nullptr)
    {
        TimerDesc desc{};
        desc.delaySec = delay;
        desc.mode     = mode;
        desc.fn       = fn;
        desc.user     = user;
        desc.name     = name;
        return queue.schedule(desc);
    }

    struct OrderSlot
    {
        std::vector<int>* out = nullptr;
        int               id  = 0;
        static void onFire(void* user) noexcept
        {
            OrderSlot* self = static_cast<OrderSlot*>(user);
            self->out->push_back(self->id);
        }
    };

    struct Pair
    {
        TimerQueue* queue = nullptr;
        TimerHandle later{};
        int         early = 0;
        int         late  = 0;
        static void cancelLater(void* user) noexcept
        {
            Pair* self = static_cast<Pair*>(user);
            self->early += 1;
            self->queue->cancel(self->later);
        }
        static void mark(void* user) noexcept { static_cast<Pair*>(user)->late += 1; }
    };

    struct DeadInside
    {
        TimerQueue* queue       = nullptr;
        TimerHandle self{};
        bool        aliveInside = true;
        static void onFire(void* user) noexcept
        {
            DeadInside* self = static_cast<DeadInside*>(user);
            self->aliveInside = self->queue->alive(self->self);
        }
    };

    struct SelfGuard
    {
        TimerGuard* guard = nullptr;
        int         fires = 0;
        static void onFire(void* user) noexcept
        {
            SelfGuard* self = static_cast<SelfGuard*>(user);
            self->fires += 1;
            self->guard->reset();
        }
    };

    struct SiblingGuards
    {
        TimerGuard later{};
        int        early = 0;
        int        late  = 0;
        static void earlyFn(void* user) noexcept
        {
            SiblingGuards* self = static_cast<SiblingGuards*>(user);
            self->early += 1;
            self->later.reset();
        }
        static void lateFn(void* user) noexcept { static_cast<SiblingGuards*>(user)->late += 1; }
    };

    struct Deferred
    {
        TimerQueue* queue = nullptr;
        int         fires = 0;
        static void onFire(void* user) noexcept
        {
            Deferred* self = static_cast<Deferred*>(user);
            self->fires += 1;
            if (self->fires != 1)
                return;
            TimerDesc desc{};
            desc.delaySec = 0.0;
            desc.mode     = TimerMode::Once;
            desc.fn       = &Deferred::onFire;
            desc.user     = self;
            self->queue->schedule(desc);
        }
    };

    struct Reuse
    {
        TimerQueue* queue = nullptr;
        int         fires = 0;
        bool        armed = false;
        static void onFire(void* user) noexcept
        {
            Reuse* self = static_cast<Reuse*>(user);
            self->fires += 1;
            if (self->armed)
                return;
            self->armed = true;
            TimerDesc desc{};
            desc.delaySec = 0.0;
            desc.mode     = TimerMode::Once;
            desc.fn       = &Reuse::onFire;
            desc.user     = self;
            self->queue->schedule(desc);
        }
    };

    struct Orphan
    {
        TimerQueue* queue      = nullptr;
        TimerGuard  follow{};
        int         followFires = 0;
        static void followFn(void* user) noexcept { static_cast<Orphan*>(user)->followFires += 1; }
        static void first(void* user) noexcept
        {
            Orphan* self = static_cast<Orphan*>(user);
            TimerDesc desc{};
            desc.delaySec = 0.0;
            desc.mode     = TimerMode::Once;
            desc.fn       = &Orphan::followFn;
            desc.user     = self;
            desc.name     = "follow";
            self->follow  = TimerGuard{*self->queue, self->queue->schedule(desc)};
            self->follow.reset();
        }
    };

    struct FullLoop
    {
        TimerQueue* queue       = nullptr;
        TimerHandle self{};
        TimerHandle replacement{};
        int         fires       = 0;
        static void onFire(void* user) noexcept
        {
            FullLoop* self = static_cast<FullLoop*>(user);
            self->fires += 1;
            self->queue->cancel(self->self);
            TimerDesc desc{};
            desc.delaySec = 0.1;
            desc.mode     = TimerMode::Once;
            desc.fn       = &FullLoop::onFire;
            desc.user     = self;
            desc.name     = "replacement";
            self->replacement = self->queue->schedule(desc);
        }
    };

    struct Nest
    {
        TimerQueue* queue = nullptr;
        int         fires = 0;
        int         during = 0;
        static void onFire(void* user) noexcept
        {
            Nest* self = static_cast<Nest*>(user);
            self->fires += 1;
            const int seen = self->fires;
            self->queue->tick(5.0);
            self->during = self->fires - seen;
        }
    };

    struct StopRest
    {
        TimerQueue* queue = nullptr;
        int         early = 0;
        int         late  = 0;
        static void earlyFn(void* user) noexcept
        {
            StopRest* self = static_cast<StopRest*>(user);
            self->early += 1;
            self->queue->clear();
        }
        static void lateFn(void* user) noexcept { static_cast<StopRest*>(user)->late += 1; }
    };

    struct Member
    {
        int  fires = 0;
        void onFire() noexcept { fires += 1; }
    };

} // namespace

TEST(TimerQueue, OneShotFiresAfterDelay)
{
    TimerQueue q;
    Counter    c;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.25, TimerMode::Once);
    EXPECT_TRUE(h.valid());
    q.tick(0.125);
    EXPECT_EQ(c.fires, 0);
    EXPECT_TRUE(q.alive(h));
    q.tick(0.125);
    EXPECT_EQ(c.fires, 1);
    EXPECT_FALSE(q.alive(h));
    q.tick(1.0);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, OneShotDoesNotFireEarly)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 1.0, TimerMode::Once);
    q.tick(0.5);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.5);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, ZeroDelayOneShotFiresOnNextPositiveTick)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 0.0, TimerMode::Once);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.0);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.016);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, LoopWaitsOneIntervalThenRepeats)
{
    TimerQueue q;
    Counter    c;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.5, TimerMode::Loop);
    q.tick(0.25);
    EXPECT_EQ(c.fires, 0);
    EXPECT_TRUE(q.alive(h));
    q.tick(0.25);
    EXPECT_EQ(c.fires, 1);
    q.tick(0.25);
    EXPECT_EQ(c.fires, 1);
    q.tick(0.25);
    EXPECT_EQ(c.fires, 2);
    EXPECT_TRUE(q.alive(h));
}

TEST(TimerQueue, LoopDoesNotFireAtSchedule)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 0.25, TimerMode::Loop);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.0);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.25);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, OrderIsRegistrationOrder)
{
    TimerQueue q;
    std::vector<int> out;
    OrderSlot a{&out, 1};
    OrderSlot b{&out, 2};
    OrderSlot c{&out, 3};
    arm(q, &OrderSlot::onFire, &a, 0.0, TimerMode::Once);
    arm(q, &OrderSlot::onFire, &b, 0.0, TimerMode::Once);
    arm(q, &OrderSlot::onFire, &c, 0.0, TimerMode::Once);
    q.tick(0.1);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
}

TEST(TimerQueue, CancelBeforeFire)
{
    TimerQueue q;
    Counter    c;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.1, TimerMode::Once);
    EXPECT_TRUE(q.cancel(h));
    EXPECT_FALSE(q.alive(h));
    EXPECT_EQ(q.liveCount(), 0);
    q.tick(1.0);
    EXPECT_EQ(c.fires, 0);
}

TEST(TimerQueue, CancelSiblingFromCallbackSkipsIt)
{
    TimerQueue q;
    Pair       pair;
    pair.queue = &q;
    arm(q, &Pair::cancelLater, &pair, 0.0, TimerMode::Once);
    pair.later = arm(q, &Pair::mark, &pair, 0.0, TimerMode::Once);
    q.tick(0.1);
    EXPECT_EQ(pair.early, 1);
    EXPECT_EQ(pair.late, 0);
    EXPECT_FALSE(q.alive(pair.later));
}

TEST(TimerQueue, OneShotHandleIsDeadInsideCallback)
{
    TimerQueue q;
    DeadInside dead;
    dead.queue = &q;
    dead.self  = arm(q, &DeadInside::onFire, &dead, 0.0, TimerMode::Once);
    q.tick(0.1);
    EXPECT_FALSE(dead.aliveInside);
    EXPECT_FALSE(q.alive(dead.self));
}

TEST(TimerQueue, GuardDestroyDuringOwnCallbackIsSafe)
{
    TimerQueue q;
    SelfGuard  self;
    TimerGuard guard{q, arm(q, &SelfGuard::onFire, &self, 0.0, TimerMode::Once)};
    self.guard = &guard;
    q.tick(0.1);
    EXPECT_EQ(self.fires, 1);
    EXPECT_FALSE(guard.armed());
    EXPECT_EQ(q.liveCount(), 0);
    q.tick(0.1);
    EXPECT_EQ(self.fires, 1);
}

TEST(TimerQueue, GuardDestroyDuringEarlierCallbackCancelsLaterSibling)
{
    TimerQueue    q;
    SiblingGuards sib;
    arm(q, &SiblingGuards::earlyFn, &sib, 0.0, TimerMode::Once);
    sib.later = TimerGuard{q, arm(q, &SiblingGuards::lateFn, &sib, 0.0, TimerMode::Once)};
    q.tick(0.1);
    EXPECT_EQ(sib.early, 1);
    EXPECT_EQ(sib.late, 0);
    EXPECT_FALSE(sib.later.armed());
    EXPECT_EQ(q.liveCount(), 0);
}

TEST(TimerQueue, ScheduleFromCallbackWaitsUntilNextTick)
{
    TimerQueue q;
    Deferred   deferred;
    deferred.queue = &q;
    arm(q, &Deferred::onFire, &deferred, 0.0, TimerMode::Once);
    q.tick(1.0);
    EXPECT_EQ(deferred.fires, 1);
    EXPECT_EQ(q.liveCount(), 1);
    q.tick(1.0);
    EXPECT_EQ(deferred.fires, 2);
    EXPECT_EQ(q.liveCount(), 0);
}

TEST(TimerQueue, RescheduleOneShotFromCallbackCanReuseSlotWhenFull)
{
    TimerQueue q(1);
    Reuse      reuse;
    reuse.queue = &q;
    EXPECT_TRUE(arm(q, &Reuse::onFire, &reuse, 0.0, TimerMode::Once).valid());
    q.tick(0.1);
    EXPECT_EQ(reuse.fires, 1);
    EXPECT_EQ(q.liveCount(), 1);
    q.tick(0.1);
    EXPECT_EQ(reuse.fires, 2);
    EXPECT_EQ(q.liveCount(), 0);
}

TEST(TimerQueue, CancelReusedOneShotSlotDoesNotOrphanIt)
{
    TimerQueue q(1);
    Orphan     orphan;
    orphan.queue = &q;
    EXPECT_TRUE(arm(q, &Orphan::first, &orphan, 0.0, TimerMode::Once, "first").valid());
    q.tick(0.1);
    EXPECT_EQ(q.liveCount(), 0);
    EXPECT_EQ(orphan.followFires, 0);
    Counter           again;
    const TimerHandle h = arm(q, &Counter::onFire, &again, 0.0, TimerMode::Once);
    EXPECT_TRUE(h.valid());
    q.tick(0.1);
    EXPECT_EQ(again.fires, 1);
    EXPECT_EQ(orphan.followFires, 0);
}

TEST(TimerQueue, LoopCancelAndScheduleAtCapacityFailsCleanly)
{
    TimerQueue q(1);
    FullLoop   loop;
    loop.queue = &q;
    loop.self  = arm(q, &FullLoop::onFire, &loop, 0.1, TimerMode::Loop, "loop");
    EXPECT_TRUE(loop.self.valid());
    CaptureLog log;
    q.tick(0.1);
    EXPECT_EQ(loop.fires, 1);
    EXPECT_FALSE(loop.replacement.valid());
    EXPECT_EQ(q.liveCount(), 0);
    EXPECT_NE(log.text.find("full"), std::string::npos);
    Counter           again;
    const TimerHandle h = arm(q, &Counter::onFire, &again, 0.0, TimerMode::Once);
    EXPECT_TRUE(h.valid());
}

TEST(TimerQueue, OversizeDtFiresLoopOnce)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 0.01, TimerMode::Loop);
    q.tick(1.0);
    EXPECT_EQ(c.fires, 1);
    q.tick(0.005);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, TickZeroDoesNotFire)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 0.0, TimerMode::Once);
    q.tick(0.0);
    EXPECT_EQ(c.fires, 0);
    EXPECT_EQ(q.liveCount(), 1);
}

TEST(TimerQueue, NegativeDtDoesNotMutate)
{
#if DE_ENABLE_ASSERTS
    GTEST_SKIP() << "a bad dt hits DE_ASSERT";
#else
    TimerQueue q;
    Counter    c;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 1.0, TimerMode::Once);
    q.tick(-1.0);
    q.tick(std::numeric_limits<double>::quiet_NaN());
    q.tick(std::numeric_limits<double>::infinity());
    EXPECT_TRUE(q.alive(h));
    EXPECT_EQ(c.fires, 0);
    q.tick(0.5);
    EXPECT_EQ(c.fires, 0);
    q.tick(0.5);
    EXPECT_EQ(c.fires, 1);
#endif
}

TEST(TimerQueue, ZeroLoopIntervalRejected)
{
#if DE_ENABLE_ASSERTS
    GTEST_SKIP() << "a zero loop interval hits DE_ASSERT";
#else
    TimerQueue q;
    Counter    c;
    CaptureLog log;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.0, TimerMode::Loop, "loop");
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(q.liveCount(), 0);
    EXPECT_NE(log.text.find("loop interval"), std::string::npos);
    q.tick(1.0);
    EXPECT_EQ(c.fires, 0);
#endif
}

TEST(TimerQueue, NegativeAndNonFiniteDelayRejected)
{
#if DE_ENABLE_ASSERTS
    GTEST_SKIP() << "a bad delay hits DE_ASSERT";
#else
    TimerQueue q;
    Counter    kept;
    const TimerHandle live = arm(q, &Counter::onFire, &kept, 1.0, TimerMode::Once, "kept");
    CaptureLog log;
    EXPECT_FALSE(arm(q, &Counter::onFire, &kept, -0.1, TimerMode::Once, "neg").valid());
    EXPECT_FALSE(arm(q, &Counter::onFire, &kept, std::numeric_limits<double>::quiet_NaN(), TimerMode::Once, "nan").valid());
    EXPECT_FALSE(arm(q, &Counter::onFire, &kept, std::numeric_limits<double>::infinity(), TimerMode::Loop, "inf").valid());
    EXPECT_TRUE(q.alive(live));
    EXPECT_EQ(q.liveCount(), 1);
    EXPECT_NE(log.text.find("bad delay"), std::string::npos);
#endif
}

TEST(TimerQueue, NullFunctionRejected)
{
#if DE_ENABLE_ASSERTS
    GTEST_SKIP() << "a null function hits DE_ASSERT";
#else
    TimerQueue q;
    CaptureLog log;
    const TimerHandle h = arm(q, nullptr, nullptr, 0.1, TimerMode::Once, "none");
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(q.liveCount(), 0);
    EXPECT_NE(log.text.find("null fn"), std::string::npos);
#endif
}

TEST(TimerQueue, StaleCancelReturnsFalse)
{
    TimerQueue q;
    Counter    c;
    EXPECT_FALSE(q.cancel({}));
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.1, TimerMode::Once);
    EXPECT_TRUE(q.cancel(h));
    EXPECT_FALSE(q.cancel(h));
    EXPECT_FALSE(q.alive(h));
    q.tick(1.0);
    EXPECT_EQ(c.fires, 0);
}

TEST(TimerQueue, ClearDoesNotInvoke)
{
    TimerQueue q;
    Counter    c;
    arm(q, &Counter::onFire, &c, 0.0, TimerMode::Once);
    arm(q, &Counter::onFire, &c, 0.5, TimerMode::Loop);
    q.clear();
    EXPECT_EQ(q.liveCount(), 0);
    q.tick(1.0);
    EXPECT_EQ(c.fires, 0);
}

TEST(TimerQueue, ClearDuringCallbackSkipsTheRest)
{
    TimerQueue q;
    StopRest   stop;
    stop.queue = &q;
    arm(q, &StopRest::earlyFn, &stop, 0.0, TimerMode::Once);
    arm(q, &StopRest::lateFn, &stop, 0.0, TimerMode::Once);
    q.tick(0.1);
    EXPECT_EQ(stop.early, 1);
    EXPECT_EQ(stop.late, 0);
    EXPECT_EQ(q.liveCount(), 0);
}

TEST(TimerQueue, NestedTickDoesNotReenter)
{
#if DE_ENABLE_ASSERTS
    GTEST_SKIP() << "a nested tick hits DE_ASSERT";
#else
    TimerQueue q;
    Nest       nest;
    nest.queue = &q;
    Counter other;
    arm(q, &Nest::onFire, &nest, 0.0, TimerMode::Once);
    arm(q, &Counter::onFire, &other, 0.0, TimerMode::Once);
    q.tick(0.1);
    EXPECT_EQ(nest.fires, 1);
    EXPECT_EQ(nest.during, 0);
    EXPECT_EQ(other.fires, 1);
#endif
}

TEST(TimerQueue, GuardMoveConstructStealsWithoutCancel)
{
    TimerQueue q;
    Counter    c;
    TimerGuard source{q, arm(q, &Counter::onFire, &c, 0.1, TimerMode::Once)};
    const TimerHandle h = source.handle();
    TimerGuard        dest{std::move(source)};
    EXPECT_FALSE(source.armed());
    EXPECT_TRUE(dest.armed());
    EXPECT_EQ(dest.handle(), h);
    EXPECT_TRUE(q.alive(h));
    EXPECT_EQ(q.liveCount(), 1);
    q.tick(0.1);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, GuardMoveAssignCancelsOnlyTheOverwrittenHandle)
{
    TimerQueue q;
    Counter    overwritten;
    Counter    survivor;
    TimerGuard a{q, arm(q, &Counter::onFire, &overwritten, 0.0, TimerMode::Once)};
    TimerGuard b{q, arm(q, &Counter::onFire, &survivor, 0.0, TimerMode::Once)};
    const TimerHandle old             = a.handle();
    const TimerHandle survivorHandle  = b.handle();
    a                                 = std::move(b);
    EXPECT_FALSE(b.armed());
    EXPECT_TRUE(a.armed());
    EXPECT_FALSE(q.alive(old));
    EXPECT_TRUE(q.alive(survivorHandle));
    EXPECT_EQ(a.handle(), survivorHandle);
    EXPECT_EQ(q.liveCount(), 1);
    q.tick(0.1);
    EXPECT_EQ(overwritten.fires, 0);
    EXPECT_EQ(survivor.fires, 1);
}

TEST(TimerQueue, BoundMainThreadScheduleThenUnbind)
{
    UnbindMainThread unbind;
    bindMainThread();
    TimerQueue q;
    Counter    c;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 0.0, TimerMode::Once);
    EXPECT_TRUE(h.valid());
    EXPECT_TRUE(q.alive(h));
    q.tick(0.1);
    EXPECT_EQ(c.fires, 1);
}

TEST(TimerQueue, ScheduleMemberCalls)
{
    TimerQueue q;
    Member     member;
    const TimerHandle h = scheduleMember<Member, &Member::onFire>(q, member, 0.0, TimerMode::Once, "member");
    EXPECT_TRUE(h.valid());
    q.tick(0.0);
    EXPECT_EQ(member.fires, 0);
    q.tick(0.01);
    EXPECT_EQ(member.fires, 1);
    EXPECT_FALSE(q.alive(h));
}

TEST(TimerQueue, FullScheduleFailsWithoutAssert)
{
    TimerQueue q(2);
    Counter    c;
    EXPECT_TRUE(arm(q, &Counter::onFire, &c, 1.0, TimerMode::Once).valid());
    EXPECT_TRUE(arm(q, &Counter::onFire, &c, 1.0, TimerMode::Loop).valid());
    CaptureLog log;
    const TimerHandle h = arm(q, &Counter::onFire, &c, 1.0, TimerMode::Once, "overflow");
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(q.liveCount(), 2);
    EXPECT_NE(log.text.find("full"), std::string::npos);
    EXPECT_NE(log.text.find("overflow"), std::string::npos);
}

TEST(TimerQueue, CapacityClampsToRange)
{
    const TimerQueue tiny(0);
    const TimerQueue huge(100000);
    const TimerQueue normal;
    EXPECT_EQ(tiny.capacity(), 1);
    EXPECT_EQ(huge.capacity(), TimerQueue::kHardCap);
    EXPECT_EQ(normal.capacity(), TimerQueue::kDefaultCapacity);
}
