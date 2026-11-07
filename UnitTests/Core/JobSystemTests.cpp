#include <gtest/gtest.h>

#include "Core/JobSystem.h"
#include "Core/Log.h"
#include "Core/MainThread.h"
#include "ECS/World.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace Dark;

namespace
{

static_assert(!std::is_copy_constructible_v<JobSystem>);
static_assert(!std::is_copy_assignable_v<JobSystem>);
static_assert(!std::is_move_constructible_v<JobSystem>);
static_assert(!std::is_move_assignable_v<JobSystem>);

std::mutex              g_logMutex;
std::vector<std::string> g_logs;

void captureJobLog(LogLevel level, LogCategory category, const char* message)
{
    (void)level;
    (void)category;
    std::lock_guard lock(g_logMutex);
    g_logs.push_back(message ? message : "");
}

struct CaptureGuard
{
    CaptureGuard()
    {
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
        Log::setCapture(&captureJobLog);
    }

    ~CaptureGuard()
    {
        Log::setCapture(nullptr);
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
    }
};

bool logContains(const char* text)
{
    std::lock_guard lock(g_logMutex);
    for (const std::string& line : g_logs)
    {
        if (line.find(text) != std::string::npos)
            return true;
    }
    return false;
}

struct Gate
{
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    release = false;
    std::atomic<int>        popped{0};

    ~Gate()
    {
        open();
    }

    void wait()
    {
        popped.fetch_add(1, std::memory_order_acq_rel);
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return release; });
    }

    void open()
    {
        {
            std::lock_guard lock(mutex);
            release = true;
        }
        cv.notify_all();
    }
};

struct SleepJob
{
    int                ms       = 0;
    std::atomic<bool>* finished = nullptr;
    std::atomic<bool>* sawMain  = nullptr;
};

struct Marker
{
    int value = 0;
};

struct UnbindMainThread
{
    ~UnbindMainThread() { unbindMainThread(); }
};

JobStatus gateJob(void* user)
{
    static_cast<Gate*>(user)->wait();
    return JobStatus::Ok;
}

JobStatus okJob(void* user)
{
    if (user)
        static_cast<std::atomic<int>*>(user)->fetch_add(1, std::memory_order_relaxed);
    return JobStatus::Ok;
}

JobStatus failJob(void* user)
{
    if (user)
        static_cast<std::atomic<int>*>(user)->fetch_add(1, std::memory_order_relaxed);
    return JobStatus::Failed;
}

JobStatus notReadyJob(void* user)
{
    if (user)
        static_cast<std::atomic<int>*>(user)->fetch_add(1, std::memory_order_relaxed);
    return JobStatus::NotReady;
}

JobStatus sleepJob(void* user)
{
    auto* job = static_cast<SleepJob*>(user);
    if (job->sawMain)
        job->sawMain->store(isMainThread());
    std::this_thread::sleep_for(std::chrono::milliseconds(job->ms));
    if (job->finished)
        job->finished->store(true);
    return JobStatus::Ok;
}

JobStatus probeMainJob(void* user)
{
    auto* saw = static_cast<std::atomic<bool>*>(user);
    saw->store(isMainThread());
    return JobStatus::Ok;
}

JobDesc makeDesc(const char* name, JobLane lane, JobStatus (*fn)(void*), void* user)
{
    JobDesc desc{};
    desc.name   = name;
    desc.lane   = lane;
    desc.stream = JobStream::Scene;
    desc.fn     = fn;
    desc.user   = user;
    return desc;
}

template <typename Pred> bool waitUntil(Pred&& pred, int timeoutMs = 2000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

float msSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST(JobSystem, StallWarnDefaultAndNegativeIgnored)
{
    JobSystem jobs;
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 8.f);
    jobs.setJobStallWarnMs(3.5f);
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 3.5f);
    jobs.setJobStallWarnMs(-1.f);
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 3.5f);
    jobs.setJobStallWarnMs(-0.01f);
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 3.5f);
}

TEST(JobSystem, EnqueueRejectsBeforeStartAndMainOnlyWrites)
{
    JobSystem jobs;
    JobDesc   desc = makeDesc("io", JobLane::Io, okJob, nullptr);
    EXPECT_FALSE(jobs.enqueue(desc));
    EXPECT_FALSE(jobs.publishFrame(0));
    EXPECT_TRUE(jobs.joinRender(0));
    EXPECT_FALSE(jobs.joinRender(2));

    jobs.start();
    EXPECT_TRUE(jobs.publishFrame(0));
    EXPECT_FALSE(jobs.publishFrame(2));
    EXPECT_FALSE(jobs.joinRender(2));
    EXPECT_TRUE(jobs.joinRender(1));

    desc.fn = nullptr;
    EXPECT_FALSE(jobs.enqueue(desc));

    desc.fn     = okJob;
    desc.writes = AccessWorldWrite;
    EXPECT_FALSE(jobs.enqueue(desc));
    desc.writes = AccessClipVoices;
    EXPECT_FALSE(jobs.enqueue(desc));
    desc.writes = AccessCopySubmit;
    EXPECT_FALSE(jobs.enqueue(desc));
    desc.writes = AccessListMain;
    EXPECT_FALSE(jobs.enqueue(desc));
    desc.writes = AccessListTexUpdate;
    EXPECT_FALSE(jobs.enqueue(desc));
    desc.writes = AccessFileRead | AccessWorldWrite;
    EXPECT_FALSE(jobs.enqueue(desc));

    std::atomic<int> ran{0};
    desc.user   = &ran;
    desc.writes = AccessFileRead;
    desc.reads  = AccessWorldWrite | AccessClipVoices | AccessCopySubmit | AccessListMain | AccessListTexUpdate;
    desc.generation = 99;
    ASSERT_TRUE(jobs.enqueue(desc));
    desc.generation = 0;
    desc.writes     = AccessStreamVoice;
    ASSERT_TRUE(jobs.enqueue(desc));
    ASSERT_TRUE(waitUntil([&] { return ran.load() >= 2; }));
    EXPECT_EQ(ran.load(), 2);

    jobs.shutdown();
    jobs.shutdown();
    EXPECT_FALSE(jobs.enqueue(makeDesc("after", JobLane::Io, okJob, nullptr)));
    EXPECT_FALSE(jobs.publishFrame(0));
}

TEST(JobSystem, StartIsIdempotentAndShutdownWithoutStart)
{
    JobSystem idle;
    idle.shutdown();
    idle.shutdown();
    EXPECT_FALSE(idle.publishFrame(1));

    JobSystem jobs;
    jobs.start();
    jobs.start();
    std::atomic<int> ran{0};
    ASSERT_TRUE(jobs.enqueue(makeDesc("once", JobLane::Io, okJob, &ran)));
    ASSERT_TRUE(waitUntil([&] { return ran.load() == 1; }));
    jobs.shutdown();
    EXPECT_EQ(ran.load(), 1);
    jobs.shutdown();
}

TEST(JobSystem, IoDoesNotBlockJoinAndRenderWaitsForPublish)
{
    JobSystem jobs;
    Gate      gate;
    jobs.start();

    ASSERT_TRUE(jobs.enqueue(makeDesc("io-block", JobLane::Io, gateJob, &gate)));
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 1; }));

    const auto ioJoinStart = std::chrono::steady_clock::now();
    EXPECT_TRUE(jobs.joinRender(0));
    EXPECT_LT(msSince(ioJoinStart), 30.f);

    ASSERT_TRUE(jobs.enqueue(makeDesc("render-hold", JobLane::Render, gateJob, &gate)));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(gate.popped.load(), 1);

    ASSERT_TRUE(jobs.publishFrame(0));
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 2; }));
    gate.open();
    const auto renderJoinStart = std::chrono::steady_clock::now();
    EXPECT_TRUE(jobs.joinRender(0));
    EXPECT_LT(msSince(renderJoinStart), 500.f);
    jobs.shutdown();
}

TEST(JobSystem, JoinRenderWaitsOutRuntimeStallThreshold)
{
    CaptureGuard logs;
    JobSystem    jobs;
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 8.f);
    jobs.setJobStallWarnMs(12.f);
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 12.f);
    jobs.setJobStallWarnMs(-4.f);
    EXPECT_FLOAT_EQ(jobs.jobStallWarnMs(), 12.f);

    jobs.start();
    ASSERT_TRUE(jobs.publishFrame(0));

    std::atomic<bool> finished{false};
    SleepJob          sleep{50, &finished, nullptr};
    ASSERT_TRUE(jobs.enqueue(makeDesc("stall-job", JobLane::Render, sleepJob, &sleep)));

    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(jobs.joinRender(0));
    const float waited = msSince(t0);
    EXPECT_TRUE(finished.load());
    EXPECT_GE(waited, 40.f);
    EXPECT_TRUE(logContains("JobStall"));
    EXPECT_TRUE(logContains("stall-job"));

    {
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
    }
    jobs.setJobStallWarnMs(500.f);
    finished.store(false);
    sleep.ms = 30;
    ASSERT_TRUE(jobs.publishFrame(1));
    ASSERT_TRUE(jobs.enqueue(makeDesc("quiet-job", JobLane::Render, sleepJob, &sleep)));
    const auto t1 = std::chrono::steady_clock::now();
    EXPECT_TRUE(jobs.joinRender(1));
    const float quietWait = msSince(t1);
    EXPECT_TRUE(finished.load());
    EXPECT_GE(quietWait, 20.f);
    EXPECT_LT(quietWait, 250.f);
    EXPECT_FALSE(logContains("JobStall"));

    jobs.shutdown();
}

TEST(JobSystem, OverlappingRenderWritesRejected)
{
    JobSystem jobs;
    jobs.start();
    ASSERT_TRUE(jobs.publishFrame(0));

    JobDesc shadow = makeDesc("shadow", JobLane::Render, okJob, nullptr);
    shadow.writes  = AccessListShadow;
    ASSERT_TRUE(jobs.enqueue(shadow));
    EXPECT_FALSE(jobs.enqueue(shadow));

    JobDesc file = makeDesc("file", JobLane::Render, okJob, nullptr);
    file.writes   = AccessFileRead;
    ASSERT_TRUE(jobs.enqueue(file));

    JobDesc both = makeDesc("both", JobLane::Render, okJob, nullptr);
    both.writes   = AccessListShadow | AccessFileRead;
    EXPECT_FALSE(jobs.enqueue(both));

    JobDesc empty = makeDesc("empty", JobLane::Render, okJob, nullptr);
    ASSERT_TRUE(jobs.enqueue(empty));

    EXPECT_TRUE(jobs.joinRender(0));
    ASSERT_TRUE(jobs.publishFrame(0));
    ASSERT_TRUE(jobs.enqueue(shadow));
    EXPECT_TRUE(jobs.joinRender(0));
    jobs.shutdown();
}

TEST(JobSystem, RepublishWhileOutstandingRejectsOverlap)
{
    JobSystem jobs;
    Gate      gate;
    jobs.start();
    ASSERT_TRUE(jobs.publishFrame(0));

    JobDesc shadow = makeDesc("shadow-hold", JobLane::Render, gateJob, &gate);
    shadow.writes  = AccessListShadow;
    ASSERT_TRUE(jobs.enqueue(shadow));
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 1; }));

    EXPECT_FALSE(jobs.publishFrame(0));
    EXPECT_FALSE(jobs.enqueue(shadow));

    gate.open();
    EXPECT_TRUE(jobs.joinRender(0));
    jobs.shutdown();
}

TEST(JobSystem, FailedJobIsLoggedAndNotReadyIsNot)
{
    CaptureGuard     logs;
    JobSystem        jobs;
    std::atomic<int> ran{0};
    jobs.start();

    ASSERT_TRUE(jobs.enqueue(makeDesc("io-fail", JobLane::Io, failJob, &ran)));
    ASSERT_TRUE(jobs.enqueue(makeDesc("io-not-ready", JobLane::Io, notReadyJob, &ran)));
    ASSERT_TRUE(jobs.enqueue(makeDesc("io-ok", JobLane::Io, okJob, &ran)));
    ASSERT_TRUE(waitUntil([&] { return ran.load() >= 3; }));
    jobs.shutdown();

    EXPECT_EQ(ran.load(), 3);
    EXPECT_TRUE(logContains("Job failed"));
    EXPECT_TRUE(logContains("io-fail"));
    EXPECT_FALSE(logContains("io-not-ready"));
    EXPECT_FALSE(logContains("io-ok"));
}

TEST(JobSystem, MainThreadToken)
{
    UnbindMainThread unbind;
    assertMainThread();
    EXPECT_FALSE(isMainThread());

    World  world;
    Entity entity = world.createEntity();
    world.emplace<Marker>(entity, Marker{3});
    Marker* got = world.get<Marker>(entity);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->value, 3);
    int seen = 0;
    world.each<Marker>([&](Entity, Marker& marker) { seen += marker.value; });
    EXPECT_EQ(seen, 3);
    world.remove<Marker>(entity);
    EXPECT_EQ(world.get<Marker>(entity), nullptr);

    bindMainThread();
    EXPECT_TRUE(isMainThread());
    assertMainThread();
    world.emplace<Marker>(entity, Marker{1});
    EXPECT_EQ(world.get<Marker>(entity)->value, 1);

    JobSystem         jobs;
    std::atomic<bool> sawMain{true};
    jobs.start();
    ASSERT_TRUE(jobs.enqueue(makeDesc("probe", JobLane::Io, probeMainJob, &sawMain)));
    ASSERT_TRUE(waitUntil([&] { return !sawMain.load(); }));
    EXPECT_FALSE(sawMain.load());
    EXPECT_TRUE(isMainThread());
    jobs.shutdown();
}

TEST(JobSystem, ShutdownJoinsBlockedJob)
{
    JobSystem        jobs;
    Gate             gate;
    std::atomic<bool> done{false};
    jobs.start();
    ASSERT_TRUE(jobs.enqueue(makeDesc("blocked", JobLane::Io, gateJob, &gate)));
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 1; }));

    std::thread stopper([&] {
        jobs.shutdown();
        done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(done.load());
    gate.open();
    stopper.join();
    EXPECT_TRUE(done.load());
}

TEST(JobSystem, IoQueueCapIs64)
{
    JobSystem jobs;
    Gate      gate;
    jobs.start();
    JobDesc desc = makeDesc("io-gate", JobLane::Io, gateJob, &gate);

    int accepted = 0;
    ASSERT_TRUE(jobs.enqueue(desc));
    ++accepted;
    ASSERT_TRUE(jobs.enqueue(desc));
    ++accepted;
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 2; }));

    int guard = 0;
    while (jobs.enqueue(desc))
    {
        ++accepted;
        ++guard;
        if (guard > 10000)
            break;
    }

    const int inFlight = gate.popped.load();
    EXPECT_LT(guard, 10000);
    EXPECT_EQ(inFlight, 2);
    EXPECT_EQ(accepted - inFlight, 64);
    gate.open();
    jobs.shutdown();
}

TEST(JobSystem, RenderQueueCapIs8)
{
    JobSystem jobs;
    Gate      gate;
    jobs.start();
    ASSERT_TRUE(jobs.publishFrame(0));
    JobDesc desc = makeDesc("render-gate", JobLane::Render, gateJob, &gate);

    int accepted = 0;
    ASSERT_TRUE(jobs.enqueue(desc));
    ++accepted;
    ASSERT_TRUE(waitUntil([&] { return gate.popped.load() >= 1; }));

    int guard = 0;
    while (jobs.enqueue(desc))
    {
        ++accepted;
        ++guard;
        if (guard > 10000)
            break;
    }

    const int inFlight = gate.popped.load();
    EXPECT_LT(guard, 10000);
    EXPECT_EQ(inFlight, 1);
    EXPECT_EQ(accepted - inFlight, 8);
    gate.open();
    EXPECT_TRUE(jobs.joinRender(0));
    jobs.shutdown();
}
