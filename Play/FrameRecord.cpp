#include "Play/FrameRecord.h"

#include "Render/Profile.h"

#include <chrono>

namespace Dark
{

namespace
{

// PIX was not captured for the overlap, so the worker stays off until a host opts in.
bool g_shadowWorker = false;

float elapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

void frameNoteBeginFrame(JobSystem& jobs, uint32_t frameIndex)
{
    // beginFrame already waited this slot. Starting the scheduler here would
    // spawn threads on the splash path, which never kicks a render job.
    jobs.publishFrame(frameIndex);
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

} // namespace Dark
