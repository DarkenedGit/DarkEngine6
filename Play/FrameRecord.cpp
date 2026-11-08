#include "Play/FrameRecord.h"

#include "Core/Log.h"
#include "Render/Profile.h"

#include <chrono>
#include <cstring>

namespace Dark
{

namespace
{

// PIX was not captured for the overlap, so the worker stays off until a host opts in.
bool g_shadowWorker = false;

// enqueue refuses these. A row that cannot be kicked must not occupy the table.
constexpr uint32_t kMainOnlyWrites =
    AccessWorldWrite | AccessClipVoices | AccessCopySubmit | AccessListMain | AccessListTexUpdate;

constexpr int kMaxFramePasses = 16;

// Main thread only. The render lane runs record(); it does not read this table.
struct FramePassRow
{
    PassDesc desc{};
    bool     armed = false;
};

FramePassRow g_passes[kMaxFramePasses]{};
int          g_passCount = 0;

float elapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

bool samePassName(const char* a, const char* b)
{
    if (!a || !b)
        return false;
    return std::strcmp(a, b) == 0;
}

int findPass(const char* name)
{
    for (int i = 0; i < g_passCount; ++i)
    {
        if (samePassName(g_passes[i].desc.name, name))
            return i;
    }
    return -1;
}

bool offThreadWritesOverlap(const PassDesc& pass, int ignore)
{
    if (!pass.recordOffThread || pass.writes == 0)
        return false;
    for (int i = 0; i < g_passCount; ++i)
    {
        if (i == ignore)
            continue;
        const PassDesc& other = g_passes[i].desc;
        if (!other.recordOffThread || other.writes == 0)
            continue;
        if ((other.writes & pass.writes) != 0)
            return true;
    }
    return false;
}

void enqueueOffThreadPasses(JobSystem& jobs, uint32_t frameIndex)
{
    // The kick site has already published. An unpublished slot must not enqueue
    // onto whichever frame happens to be current.
    if (!jobs.frameIsPublished(frameIndex))
        return;

    int indices[kMaxFramePasses];
    int count = 0;
    for (int i = 0; i < g_passCount; ++i)
    {
        const FramePassRow& row = g_passes[i];
        if (!row.armed || !row.desc.recordOffThread || !row.desc.record)
            continue;
        indices[count++] = i;
    }

    for (int i = 1; i < count; ++i)
    {
        const int key = indices[i];
        int       j   = i;
        while (j > 0 && g_passes[indices[j - 1]].desc.order > g_passes[key].desc.order)
        {
            indices[j] = indices[j - 1];
            --j;
        }
        indices[j] = key;
    }

    for (int n = 0; n < count; ++n)
    {
        FramePassRow& row = g_passes[indices[n]];
        // Disarm before the worker runs. A later join, a worker-off frame, and splash must not Reset again.
        row.armed = false;
        JobDesc job{};
        job.name   = row.desc.name;
        job.lane   = JobLane::Render;
        job.writes = row.desc.writes;
        job.fn     = row.desc.record;
        job.user   = row.desc.user;
        if (jobs.enqueue(job))
            continue;
        if (jobs.renderBusy(frameIndex))
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame pass {} was not kicked; slot {} is busy",
                job.name ? job.name : "(unnamed)", frameIndex);
            continue;
        }
        // Queue rejected the row and nothing else owns the slot. Record on the caller so the list is still closed.
        row.desc.record(row.desc.user);
    }
}

} // namespace

void frameNoteBeginFrame(JobSystem& jobs, uint32_t frameIndex)
{
    // beginFrame already waited this slot. Starting the scheduler here would
    // spawn threads on the splash path, which never kicks a render job.
    jobs.publishFrame(frameIndex);
}

void kickFramePasses(JobSystem& jobs, uint32_t frameIndex)
{
    // The main list is still open. Starting here is what lets the worker overlap it.
    enqueueOffThreadPasses(jobs, frameIndex);
}

bool frameJoinRender(JobSystem& jobs, uint32_t frameIndex)
{
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = jobs.joinRender(frameIndex);
    const float waitedMs = elapsedMs(t0);
    // JobSystem logs JobStall. The profile range stays on the main thread.
    if (ok && waitedMs >= jobs.jobStallWarnMs())
        profileAppendCpuRange("JobStall", waitedMs);
    return ok;
}

void setShadowWorker(bool enabled)
{
    g_shadowWorker = enabled;
}

bool shadowWorker()
{
    return g_shadowWorker;
}

bool registerFramePass(const PassDesc& pass)
{
    if ((pass.writes & kMainOnlyWrites) != 0)
        return false;
    if (!pass.record)
        return false;

    const int existing = findPass(pass.name);
    if (offThreadWritesOverlap(pass, existing))
        return false;
    if (existing < 0 && g_passCount >= kMaxFramePasses)
        return false;

    const int slot = (existing >= 0) ? existing : g_passCount;
    if (existing < 0)
        ++g_passCount;
    g_passes[slot].desc  = pass;
    g_passes[slot].armed = pass.recordOffThread;
    return true;
}

void resetFramePasses()
{
    g_passCount = 0;
    for (int i = 0; i < kMaxFramePasses; ++i)
        g_passes[i] = {};
}

} // namespace Dark
