#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace Dark
{

    enum class JobStatus : uint8_t
    {
        Ok = 0,
        Failed,
        Cancelled,
        Budget,
        NotReady,
    };

    enum class JobLane : uint8_t
    {
        Io = 0,
        Render,
    };

    enum class JobStream : uint8_t
    {
        Scene = 0,
        TerrainTile,
    };

    enum JobAccess : uint32_t
    {
        AccessNone          = 0,
        AccessWorldRead     = 1u << 0, // snapshot bytes only, never World methods
        AccessWorldWrite    = 1u << 1, // main only
        AccessStreamVoice   = 1u << 2, // not a JobSystem lane
        AccessClipVoices    = 1u << 3, // main only
        AccessFileRead      = 1u << 4,
        AccessCopySubmit    = 1u << 5, // main only
        AccessListMain      = 1u << 6, // main only
        AccessListShadow    = 1u << 7,
        AccessListTexUpdate = 1u << 8, // main only
    };

    struct JobDesc
    {
        const char* name       = nullptr;
        JobLane     lane       = JobLane::Io;
        JobStream   stream     = JobStream::Scene;
        uint32_t    reads      = AccessNone;
        uint32_t    writes     = AccessNone;
        uint64_t    generation = 0;
        JobStatus (*fn)(void* user) = nullptr;
        void*       user       = nullptr;
    };

    class JobSystem
    {
    public:
        JobSystem() = default;
        ~JobSystem();

        JobSystem(const JobSystem&)            = delete;
        JobSystem& operator=(const JobSystem&) = delete;
        JobSystem(JobSystem&&)                 = delete;
        JobSystem& operator=(JobSystem&&)      = delete;

        void start();
        void shutdown();

        bool enqueue(const JobDesc& desc);
        bool publishFrame(uint32_t frameIndex);
        bool joinRender(uint32_t frameIndex);
        // True only for the frame publishFrame last accepted. A second publish clears writeMask.
        bool frameIsPublished(uint32_t frameIndex);
        // Outstanding queued or running render work for this slot.
        bool renderBusy(uint32_t frameIndex);
        void setJobStallWarnMs(float ms);
        float jobStallWarnMs() const;

    private:
        static constexpr uint32_t kFrameSlots    = 2;
        static constexpr int      kIoThreadCount = 2;

        struct FrameSlot
        {
            int      queued    = 0;
            int      running   = 0;
            uint32_t writeMask = 0;
        };

        struct RenderItem
        {
            JobDesc  desc{};
            uint32_t frame = 0;
        };

        void ioMain();
        void renderMain();
        void flushPendingLocked();
        void logStall(std::unique_lock<std::mutex>& lock, uint32_t frame, float waitedMs);
        const char* stallNameLocked(uint32_t frame) const;
        int outstandingLocked(uint32_t frame) const;

        std::mutex              m_mutex;
        std::condition_variable m_cv;
        std::deque<JobDesc>     m_ioQueue;
        std::deque<JobDesc>     m_pendingRender;
        std::deque<RenderItem>  m_renderQueue;
        std::array<FrameSlot, kFrameSlots>       m_slots{};
        std::array<std::thread, kIoThreadCount>  m_ioThreads{};
        std::thread             m_renderThread;
        std::atomic<float>      m_stallWarnMs{8.f};
        std::atomic<int64_t>    m_lastStallLogMs{-1};
        uint32_t                m_pendingWriteMask   = 0;
        uint32_t                m_publishedFrame     = 0;
        const char*             m_runningRenderName  = nullptr;
        uint32_t                m_runningRenderFrame = 0;
        bool                    m_started            = false;
        bool                    m_stopping           = false;
        bool                    m_hasPublished       = false;
        bool                    m_renderRunning      = false;
    };

} // namespace Dark
