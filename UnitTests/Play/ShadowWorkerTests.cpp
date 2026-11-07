#include <gtest/gtest.h>

#include "Core/JobSystem.h"
#include "Play/FrameRecord.h"
#include "Render/CullResourceSet.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Dark;

namespace
{

struct FlagOff
{
    ~FlagOff() { setShadowWorker(false); }
};

struct SleepUser
{
    int               ms   = 30;
    std::atomic<bool>* done = nullptr;
};

JobStatus sleepPastStall(void* user)
{
    auto* job = static_cast<SleepUser*>(user);
    if (!job)
        return JobStatus::Failed;
    std::this_thread::sleep_for(std::chrono::milliseconds(job->ms));
    if (job->done)
        job->done->store(true);
    return JobStatus::Ok;
}

} // namespace

TEST(ShadowWorker, DefaultsOffAndIdleJoin)
{
    FlagOff guard;
    EXPECT_FALSE(shadowWorker());
    EXPECT_FALSE(usesCameraArgs(cullResourceSetForDepth(true)));
    EXPECT_TRUE(usesCameraArgs(cullResourceSetForDepth(false)));

    JobSystem jobs;
    frameNoteBeginFrame(jobs, 0);
    EXPECT_TRUE(jobs.joinRender(0));
    EXPECT_TRUE(frameJoinRender(jobs, 0));
    EXPECT_TRUE(jobs.joinRender(1));
    EXPECT_FALSE(jobs.joinRender(2));
    EXPECT_FALSE(jobs.publishFrame(2));
    EXPECT_FALSE(frameJoinRender(jobs, 2));

    JobDesc desc{};
    desc.name = "not-started";
    desc.lane = JobLane::Render;
    desc.fn   = sleepPastStall;
    EXPECT_FALSE(jobs.enqueue(desc));
    EXPECT_FALSE(shadowWorker());
}

TEST(ShadowWorker, JoinWaitsAndBusyPublishFails)
{
    FlagOff guard;
    setShadowWorker(true);
    EXPECT_TRUE(shadowWorker());

    JobSystem jobs;
    jobs.start();
    ASSERT_TRUE(jobs.publishFrame(0));

    std::atomic<bool> done{false};
    SleepUser         sleep;
    sleep.ms   = 30;
    sleep.done = &done;

    JobDesc desc{};
    desc.name   = "shadow-sleep";
    desc.lane   = JobLane::Render;
    desc.writes = AccessListShadow;
    desc.fn     = sleepPastStall;
    desc.user   = &sleep;
    ASSERT_TRUE(jobs.enqueue(desc));
    EXPECT_FALSE(jobs.publishFrame(0));

    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(frameJoinRender(jobs, 0));
    const float waitedMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_TRUE(done.load());
    EXPECT_GE(waitedMs, 8.f);

    EXPECT_TRUE(jobs.publishFrame(1));
    jobs.shutdown();
    EXPECT_FALSE(jobs.renderBusy(0));
    setShadowWorker(false);
    EXPECT_FALSE(shadowWorker());
}
