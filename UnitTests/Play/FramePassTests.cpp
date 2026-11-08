#include <gtest/gtest.h>

#include "Core/JobSystem.h"

#if defined(__d3d12_h__)
#error JobSystem.h must not include d3d12.h
#endif

#include "Play/FrameRecord.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Dark;

namespace
{

struct ResetPasses
{
    ResetPasses() { resetFramePasses(); }
    ~ResetPasses() { resetFramePasses(); }
};

struct Probe
{
    std::atomic<int>*   seq = nullptr;
    std::thread::id     caller{};
    std::atomic<int>    seen{-1};
    std::atomic<int>    offCaller{0};
    std::atomic<int>    ran{0};
};

struct CookBlob
{
    int              payload   = 0;
    int              result    = 0;
    int              committed = 0;
    std::atomic<int> ready{0};
};

JobStatus probeRecord(void* user)
{
    auto* job = static_cast<Probe*>(user);
    if (!job)
        return JobStatus::Ok;
    if (std::this_thread::get_id() != job->caller)
        job->offCaller.store(1, std::memory_order_relaxed);
    if (job->seq)
        job->seen.store(job->seq->fetch_add(1, std::memory_order_acq_rel), std::memory_order_relaxed);
    job->ran.store(1, std::memory_order_release);
    return JobStatus::Ok;
}

JobStatus noopRecord(void*)
{
    return JobStatus::Ok;
}

JobStatus cookBlob(void* user)
{
    auto* blob = static_cast<CookBlob*>(user);
    if (!blob)
        return JobStatus::Failed;
    blob->result = blob->payload;
    blob->ready.store(1, std::memory_order_release);
    return JobStatus::Ok;
}

bool waitReady(const CookBlob& blob)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (blob.ready.load(std::memory_order_acquire) == 0)
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

PassDesc makePass(const char* name, int order, uint32_t writes, bool offThread, void* user)
{
    PassDesc pass{};
    pass.name            = name;
    pass.order           = order;
    pass.writes          = writes;
    pass.recordOffThread = offThread;
    pass.record          = (user == nullptr) ? &noopRecord : &probeRecord;
    pass.user            = user;
    return pass;
}

} // namespace

TEST(FramePass, JoinEnqueuesNoopWhenStarted)
{
    ResetPasses      guard;
    std::atomic<int> seq{0};
    Probe            early{};
    Probe            late{};
    Probe            mainRow{};
    early.seq    = &seq;
    late.seq     = &seq;
    early.caller = std::this_thread::get_id();
    late.caller  = early.caller;

    EXPECT_FALSE(shadowWorker());
    JobSystem jobs;
    jobs.start();
    frameNoteBeginFrame(jobs, 0);
    EXPECT_TRUE(jobs.frameIsPublished(0));

    // Registered late-first so the join must sort, not keep insertion order.
    ASSERT_TRUE(registerFramePass(makePass("late", 2, AccessNone, true, &late)));
    ASSERT_TRUE(registerFramePass(makePass("early", 0, AccessNone, true, &early)));
    ASSERT_TRUE(registerFramePass(makePass("main", 100, AccessNone, false, &mainRow)));

    EXPECT_TRUE(frameJoinRender(jobs, 0));
    EXPECT_EQ(early.ran.load(), 1);
    EXPECT_EQ(late.ran.load(), 1);
    EXPECT_EQ(early.offCaller.load(), 1);
    EXPECT_EQ(late.offCaller.load(), 1);
    EXPECT_EQ(early.seen.load(), 0);
    EXPECT_EQ(late.seen.load(), 1);
    EXPECT_EQ(mainRow.ran.load(), 0);

    early.ran.store(0);
    EXPECT_TRUE(frameJoinRender(jobs, 0));
    EXPECT_EQ(early.ran.load(), 0);

    ASSERT_TRUE(registerFramePass(makePass("early", 0, AccessNone, true, &early)));
    EXPECT_TRUE(frameJoinRender(jobs, 0));
    EXPECT_EQ(early.ran.load(), 1);

    jobs.shutdown();
    EXPECT_FALSE(shadowWorker());
}

TEST(FramePass, RejectsMainOnlyAccess)
{
    ResetPasses guard;

    const uint32_t mainOnly[] = {
        AccessWorldWrite,
        AccessClipVoices,
        AccessCopySubmit,
        AccessListMain,
        AccessListTexUpdate,
    };
    for (uint32_t bit : mainOnly)
    {
        PassDesc pass = makePass("main-only", 0, bit, true, nullptr);
        EXPECT_FALSE(registerFramePass(pass));
        pass.recordOffThread = false;
        EXPECT_FALSE(registerFramePass(pass));
    }

    PassDesc mixed = makePass("mixed", 0, AccessListMain | AccessFileRead, true, nullptr);
    EXPECT_FALSE(registerFramePass(mixed));

    PassDesc missing = makePass("missing", 0, AccessNone, true, nullptr);
    missing.record = nullptr;
    EXPECT_FALSE(registerFramePass(missing));

    EXPECT_TRUE(registerFramePass(makePass("ok", 0, AccessNone, true, nullptr)));
}

TEST(FramePass, FullTableReturnsFalse)
{
    ResetPasses guard;

    static char names[16][4]{};
    for (int i = 0; i < 16; ++i)
    {
        names[i][0] = 'p';
        names[i][1] = static_cast<char>('a' + (i / 10));
        names[i][2] = static_cast<char>('0' + (i % 10));
        names[i][3] = '\0';
        ASSERT_TRUE(registerFramePass(makePass(names[i], i, AccessNone, true, nullptr)));
    }

    EXPECT_FALSE(registerFramePass(makePass("overflow", 16, AccessNone, true, nullptr)));
    EXPECT_TRUE(registerFramePass(makePass(names[0], 0, AccessNone, true, nullptr)));
    EXPECT_FALSE(registerFramePass(makePass("overflow", 16, AccessNone, true, nullptr)));
}

TEST(FramePass, RejectsOverlappingOffThreadWrites)
{
    ResetPasses guard;

    ASSERT_TRUE(registerFramePass(makePass("shadow", 0, AccessListShadow, true, nullptr)));
    EXPECT_FALSE(registerFramePass(makePass("shadow-again", 1, AccessListShadow, true, nullptr)));
    EXPECT_TRUE(registerFramePass(makePass("file", 2, AccessFileRead, true, nullptr)));
    EXPECT_FALSE(registerFramePass(makePass("both", 3, AccessFileRead | AccessWorldRead, true, nullptr)));
    EXPECT_TRUE(registerFramePass(makePass("main-shadow", 4, AccessListShadow, false, nullptr)));
    EXPECT_TRUE(registerFramePass(makePass("shadow", 0, AccessListShadow, true, nullptr)));
}

TEST(FramePass, SecondIoJobIsEnqueuePlusMainCommit)
{
    ResetPasses guard;
    EXPECT_FALSE(shadowWorker());

    // Water CPU stays off the pass table: IO enqueue, then the main thread commits. Not its own thread.
    CookBlob first{};
    CookBlob second{};
    first.payload  = 3;
    second.payload = 9;

    JobSystem jobs;
    jobs.start();

    JobDesc io{};
    io.name = "WaterCpu";
    io.lane = JobLane::Io;
    io.fn   = &cookBlob;
    io.user = &first;
    ASSERT_TRUE(jobs.enqueue(io));

    io.name = "WaterCpuNext";
    io.user = &second;
    ASSERT_TRUE(jobs.enqueue(io));

    ASSERT_TRUE(waitReady(first));
    ASSERT_TRUE(waitReady(second));
    first.committed  = first.result;
    second.committed = second.result;
    EXPECT_EQ(first.committed, 3);
    EXPECT_EQ(second.committed, 9);
    EXPECT_EQ(first.ready.load(), 1);
    EXPECT_EQ(second.ready.load(), 1);

    jobs.shutdown();
}
