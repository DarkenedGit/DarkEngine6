#include "Core/JobSystem.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include <chrono>
#include <cstdint>

namespace Dark
{

    namespace
    {

        // Queue depth only. A job a worker has already popped does not consume a slot.
        constexpr size_t kIoQueueCap     = 64;
        constexpr size_t kRenderQueueCap = 8;

        constexpr uint32_t kMainOnlyWrites =
            AccessWorldWrite | AccessClipVoices | AccessCopySubmit | AccessListMain | AccessListTexUpdate;

        void copyJobName(char* dst, size_t cap, const char* name)
        {
            if (cap == 0)
                return;
            if (!name)
            {
                dst[0] = '\0';
                return;
            }
            size_t i = 0;
            while (name[i] != '\0' && (i + 1) < cap)
            {
                dst[i] = name[i];
                ++i;
            }
            dst[i] = '\0';
        }

        int64_t steadyMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        float elapsedMs(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        void logFailedStatus(JobStatus status, const char* name)
        {
            if (status != JobStatus::Failed)
                return;
            DE_LOG_ERROR(LogCategory::Core, "Job failed: {}", (name && name[0]) ? name : "(unnamed)");
        }

    } // namespace

    JobSystem::~JobSystem()
    {
        shutdown();
    }

    void JobSystem::start()
    {
        std::lock_guard lock(m_mutex);
        if (m_started)
            return;

        m_stopping = false;
        // Set before the constructors. A later constructor failure must still be joinable from shutdown.
        m_started = true;
        m_renderThread = std::thread([this] { renderMain(); });
        for (std::thread& thread : m_ioThreads)
            thread = std::thread([this] { ioMain(); });
    }

    void JobSystem::shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            bool live = m_started || m_renderThread.joinable();
            for (const std::thread& thread : m_ioThreads)
                live = live || thread.joinable();
            if (!live)
                return;
            m_stopping = true;
            flushPendingLocked();
        }
        m_cv.notify_all();

        // Join waits out a job blocked inside fn.
        if (m_renderThread.joinable())
            m_renderThread.join();
        for (std::thread& thread : m_ioThreads)
        {
            if (thread.joinable())
                thread.join();
        }

        std::lock_guard lock(m_mutex);
        m_started          = false;
        m_stopping         = false;
        m_hasPublished     = false;
        m_publishedFrame   = 0;
        m_pendingWriteMask = 0;
        m_renderRunning    = false;
        m_runningRenderName  = nullptr;
        m_runningRenderFrame = 0;
        m_pendingRender.clear();
        m_ioQueue.clear();
        m_renderQueue.clear();
        for (FrameSlot& slot : m_slots)
            slot = {};
    }

    bool JobSystem::enqueue(const JobDesc& desc)
    {
        if (!desc.fn)
            return false;
        if ((desc.writes & kMainOnlyWrites) != 0)
            return false;

        std::lock_guard lock(m_mutex);
        if (!m_started || m_stopping)
            return false;

        if (desc.lane == JobLane::Io)
        {
            if (m_ioQueue.size() >= kIoQueueCap)
                return false;
            m_ioQueue.push_back(desc);
            m_cv.notify_all();
            return true;
        }

        if (desc.lane != JobLane::Render)
            return false;

        if ((m_renderQueue.size() + m_pendingRender.size()) >= kRenderQueueCap)
            return false;

        if (!m_hasPublished)
        {
            if (desc.writes != 0 && (m_pendingWriteMask & desc.writes) != 0)
                return false;
            m_pendingWriteMask |= desc.writes;
            m_pendingRender.push_back(desc);
            return true;
        }

        const uint32_t frame = m_publishedFrame;
        if (frame >= kFrameSlots)
            return false;
        if (desc.writes != 0 && (m_slots[frame].writeMask & desc.writes) != 0)
            return false;

        m_slots[frame].writeMask |= desc.writes;
        m_renderQueue.push_back(RenderItem{desc, frame});
        m_slots[frame].queued += 1;
        m_cv.notify_all();
        return true;
    }

    bool JobSystem::publishFrame(uint32_t frameIndex)
    {
        std::lock_guard lock(m_mutex);
        if (!m_started || m_stopping)
            return false;
        if (frameIndex >= kFrameSlots)
            return false;
        // In-flight jobs still own their write bits. A new publish of this slot waits until joinRender.
        if (outstandingLocked(frameIndex) > 0)
            return false;

        m_hasPublished                = true;
        m_publishedFrame              = frameIndex;
        m_slots[frameIndex].writeMask = 0;

        for (const JobDesc& desc : m_pendingRender)
        {
            m_slots[frameIndex].writeMask |= desc.writes;
            m_renderQueue.push_back(RenderItem{desc, frameIndex});
            m_slots[frameIndex].queued += 1;
        }
        m_pendingRender.clear();
        m_pendingWriteMask = 0;
        m_cv.notify_all();
        return true;
    }

    bool JobSystem::joinRender(uint32_t frameIndex)
    {
        if (frameIndex >= kFrameSlots)
            return false;

        std::unique_lock lock(m_mutex);
        const auto       t0 = std::chrono::steady_clock::now();
        while (outstandingLocked(frameIndex) > 0)
        {
            // Read each pass. setJobStallWarnMs must change the log line without a rebuilt wait.
            const float threshold = m_stallWarnMs.load(std::memory_order_relaxed);
            const float waitedMs  = elapsedMs(t0);
            std::chrono::milliseconds slice{1};
            if (waitedMs >= threshold)
            {
                logStall(lock, frameIndex, waitedMs);
                if (outstandingLocked(frameIndex) <= 0)
                    break;
                slice = std::chrono::milliseconds(50);
            }
            else
            {
                float remain = threshold - waitedMs;
                if (remain < 1.f)
                    remain = 1.f;
                if (remain > 60000.f)
                    remain = 60000.f;
                slice = std::chrono::milliseconds(static_cast<int>(remain));
            }
            m_cv.wait_for(lock, slice, [&] { return outstandingLocked(frameIndex) <= 0; });
        }
        return true;
    }

    bool JobSystem::frameIsPublished(uint32_t frameIndex)
    {
        std::lock_guard lock(m_mutex);
        return m_hasPublished && frameIndex < kFrameSlots && m_publishedFrame == frameIndex;
    }

    bool JobSystem::renderBusy(uint32_t frameIndex)
    {
        std::lock_guard lock(m_mutex);
        return outstandingLocked(frameIndex) > 0;
    }

    void JobSystem::setJobStallWarnMs(float ms)
    {
        if (ms < 0.f)
            return;
        m_stallWarnMs.store(ms, std::memory_order_relaxed);
    }

    float JobSystem::jobStallWarnMs() const
    {
        return m_stallWarnMs.load(std::memory_order_relaxed);
    }

    void JobSystem::ioMain()
    {
        const HRESULT com      = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool    uninit   = SUCCEEDED(com);
        const bool    comReady = uninit || com == RPC_E_CHANGED_MODE;
        // RPC_E_CHANGED_MODE did not take a ref. CoUninitialize would unbalance the caller.
        if (com == RPC_E_CHANGED_MODE)
            DE_LOG_WARN(LogCategory::Core, "JobSystem: COM apartment already initialized");
        else if (!comReady)
            DE_LOG_ERROR(LogCategory::Core, "JobSystem: CoInitializeEx failed (HRESULT 0x{:08X})", static_cast<unsigned>(com));

        for (;;)
        {
            JobDesc desc{};
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [&] { return m_stopping || !m_ioQueue.empty(); });
                if (m_ioQueue.empty())
                    break;
                desc = m_ioQueue.front();
                m_ioQueue.pop_front();
            }

            char name[96];
            copyJobName(name, sizeof(name), desc.name);
            const JobStatus status = desc.fn(desc.user);
            logFailedStatus(status, name);
        }

        if (uninit)
            CoUninitialize();
    }

    void JobSystem::renderMain()
    {
        for (;;)
        {
            RenderItem item{};
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [&] { return m_stopping || !m_renderQueue.empty(); });
                if (m_renderQueue.empty())
                    break;
                item = m_renderQueue.front();
                m_renderQueue.pop_front();
                DE_ASSERT(item.frame < kFrameSlots);
                DE_ASSERT(m_slots[item.frame].queued > 0);
                m_slots[item.frame].queued -= 1;
                m_slots[item.frame].running += 1;
                m_runningRenderName  = item.desc.name;
                m_runningRenderFrame = item.frame;
                m_renderRunning      = true;
            }

            char name[96];
            copyJobName(name, sizeof(name), item.desc.name);
            const JobStatus status = item.desc.fn(item.desc.user);
            logFailedStatus(status, name);

            {
                std::lock_guard lock(m_mutex);
                DE_ASSERT(item.frame < kFrameSlots);
                DE_ASSERT(m_slots[item.frame].running > 0);
                m_slots[item.frame].running -= 1;
                m_renderRunning     = false;
                m_runningRenderName = nullptr;
                m_cv.notify_all();
            }
        }
    }

    void JobSystem::flushPendingLocked()
    {
        const uint32_t frame = (m_hasPublished && m_publishedFrame < kFrameSlots) ? m_publishedFrame : 0;
        for (const JobDesc& desc : m_pendingRender)
        {
            m_slots[frame].writeMask |= desc.writes;
            m_renderQueue.push_back(RenderItem{desc, frame});
            m_slots[frame].queued += 1;
        }
        m_pendingRender.clear();
        m_pendingWriteMask = 0;
    }

    void JobSystem::logStall(std::unique_lock<std::mutex>& lock, uint32_t frame, float waitedMs)
    {
        const int64_t nowMs = steadyMs();
        const int64_t last  = m_lastStallLogMs.load(std::memory_order_relaxed);
        if (last >= 0 && (nowMs - last) < 1000)
            return;
        m_lastStallLogMs.store(nowMs, std::memory_order_relaxed);

        char name[96];
        copyJobName(name, sizeof(name), stallNameLocked(frame));
        lock.unlock();
        DE_LOG_WARN(LogCategory::Core, "JobStall name={} waitMs={:.3f}", name[0] ? name : "(unnamed)", static_cast<double>(waitedMs));
        lock.lock();
    }

    const char* JobSystem::stallNameLocked(uint32_t frame) const
    {
        if (m_renderRunning && m_runningRenderFrame == frame)
            return m_runningRenderName;
        for (const RenderItem& item : m_renderQueue)
        {
            if (item.frame == frame)
                return item.desc.name;
        }
        return nullptr;
    }

    int JobSystem::outstandingLocked(uint32_t frame) const
    {
        if (frame >= kFrameSlots)
            return 0;
        return m_slots[frame].queued + m_slots[frame].running;
    }

} // namespace Dark
