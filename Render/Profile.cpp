#include "Render/Profile.h"

#if !defined(DE_GPU_MARKERS)
#define DE_GPU_MARKERS 1
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
// pix3.h uses Win32 types and does not include windows.h itself.
// d3d12.h must come first so the command-list PIX overloads are declared.
#include <windows.h>
#include <d3d12.h>
#if DE_GPU_MARKERS
// USE_PIX keeps the events in Release. pix3.h otherwise compiles them out
// unless _DEBUG or PROFILE is set, and Release is the build these tools time.
#define USE_PIX
#include <pix3.h>
#include <nvtx3/nvToolsExt.h>
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Core/Log.h"

#include <wrl/client.h>

#include <cstring>

namespace Dark
{

namespace
{

constexpr uint32_t kNoQuery      = 0xFFFFFFFFu;
constexpr uint32_t kNoSlot       = 0xFFFFFFFFu;
constexpr uint32_t kMaxDepth     = 16;
constexpr uint32_t kFrameRing    = 4;
constexpr uint32_t kMaxQueries   = kProfileMaxRanges * 2;

struct StoredRange
{
    ProfileRange view{};
    uint64_t     cpuStart = 0;
    uint32_t     gpuBegin = kNoQuery;
    uint32_t     gpuEnd   = kNoQuery;
};

struct FrameRecord
{
    StoredRange ranges[kProfileMaxRanges]{};
    uint32_t    rangeCount = 0;
    uint32_t    queryCount = 0;
    uint32_t    gpuSlot    = kNoSlot;
    uint64_t    sequence   = 0;
    uint64_t    cpuStart   = 0;
    float       fps        = 0.0f;
    float       frameMs    = 0.0f;
    float       cpuMs      = 0.0f;
    float       gpuMs      = 0.0f;
    bool        gpuPending = false;
    bool        gpuValid   = false;
};

struct GpuSlot
{
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  readback;
    uint32_t                                queryCount = 0;
};

FrameRecord     g_ring[kFrameRing]{};
ProfileSnapshot g_published{};
GpuSlot         g_slots[kProfileGpuSlots]{};
uint32_t        g_open[kMaxDepth]{};
uint32_t        g_ringIndex   = 0;
uint32_t        g_depth       = 0;
uint32_t        g_overflow    = 0;
uint32_t        g_activeSlot  = kNoSlot;
uint64_t        g_sequence    = 0;
uint64_t        g_frequency   = 0;
bool            g_haveRing    = false;
bool            g_collecting  = false;
bool            g_gpuReady    = false;
FrameRecord*    g_frame       = nullptr;
ID3D12GraphicsCommandList* g_cmd = nullptr;

double qpcToMs(uint64_t ticks)
{
    static const double kScale = []() {
        LARGE_INTEGER freq{};
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)
            return 0.0;
        return 1000.0 / static_cast<double>(freq.QuadPart);
    }();
    return static_cast<double>(ticks) * kScale;
}

uint64_t qpcNow()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
}

void copyName(char* dst, const char* src)
{
    if (!dst)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    uint32_t i = 0;
    for (; i + 1 < kProfileNameBytes && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void publish(const FrameRecord& frame)
{
    g_published.fps        = frame.fps;
    g_published.frameMs    = frame.frameMs;
    g_published.cpuMs      = frame.cpuMs;
    g_published.gpuMs      = frame.gpuMs;
    g_published.gpuValid   = frame.gpuValid;
    g_published.rangeCount = frame.rangeCount;
    for (uint32_t i = 0; i < frame.rangeCount; ++i)
        g_published.ranges[i] = frame.ranges[i].view;
}

StoredRange* topRange()
{
    if (!g_collecting || g_frame == nullptr || g_overflow > 0 || g_depth == 0)
        return nullptr;
    const uint32_t index = g_open[g_depth - 1];
    if (index >= g_frame->rangeCount)
        return nullptr;
    return &g_frame->ranges[index];
}

uint32_t stamp(ID3D12GraphicsCommandList* cmd)
{
    if (!g_gpuReady || cmd == nullptr || cmd != g_cmd || g_activeSlot >= kProfileGpuSlots)
        return kNoQuery;
    GpuSlot& slot = g_slots[g_activeSlot];
    if (!slot.heap || slot.queryCount >= kMaxQueries)
        return kNoQuery;
    const uint32_t query = slot.queryCount++;
    cmd->EndQuery(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    return query;
}

void fillGpu(FrameRecord& frame)
{
    frame.gpuPending = false;
    frame.gpuValid   = false;
    frame.gpuMs      = 0.0f;
    if (frame.gpuSlot >= kProfileGpuSlots || frame.queryCount == 0 || g_frequency == 0)
        return;
    GpuSlot& slot = g_slots[frame.gpuSlot];
    if (!slot.readback)
        return;

    const UINT64 bytes = static_cast<UINT64>(frame.queryCount) * sizeof(uint64_t);
    void*        mapped = nullptr;
    const D3D12_RANGE read{ 0, static_cast<SIZE_T>(bytes) };
    if (FAILED(slot.readback->Map(0, &read, &mapped)) || mapped == nullptr)
    {
        DE_LOG_ERROR(LogCategory::Render, "Profile: timestamp readback map failed");
        return;
    }
    const auto* stamps = static_cast<const uint64_t*>(mapped);

    float frameGpu = -1.0f;
    bool  anyGpu   = false;
    for (uint32_t i = 0; i < frame.rangeCount; ++i)
    {
        StoredRange& range = frame.ranges[i];
        range.view.gpuMs   = -1.0f;
        if (range.gpuBegin == kNoQuery || range.gpuEnd == kNoQuery)
            continue;
        if (range.gpuBegin >= frame.queryCount || range.gpuEnd >= frame.queryCount || range.gpuEnd < range.gpuBegin)
            continue;
        const uint64_t endTs   = stamps[range.gpuEnd];
        const uint64_t beginTs = stamps[range.gpuBegin];
        const uint64_t delta   = endTs >= beginTs ? endTs - beginTs : 0;
        range.view.gpuMs     = static_cast<float>(static_cast<double>(delta) * 1000.0 / static_cast<double>(g_frequency));
        anyGpu                = true;
        if (frameGpu < 0.0f && std::strcmp(range.view.name, "Frame") == 0)
            frameGpu = range.view.gpuMs;
    }

    const D3D12_RANGE none{ 0, 0 };
    slot.readback->Unmap(0, &none);

    if (!anyGpu)
        return;
    if (frameGpu < 0.0f)
    {
        for (uint32_t i = 0; i < frame.rangeCount; ++i)
        {
            if (frame.ranges[i].view.depth == 0 && frame.ranges[i].view.gpuMs > frameGpu)
                frameGpu = frame.ranges[i].view.gpuMs;
        }
    }
    frame.gpuMs    = frameGpu > 0.0f ? frameGpu : 0.0f;
    frame.gpuValid = frameGpu >= 0.0f;
}

void recordCpuBegin(const char* name, uint32_t argb)
{
    if (!g_collecting || g_frame == nullptr || name == nullptr || name[0] == '\0')
        return;
    if (g_overflow > 0 || g_depth >= kMaxDepth || g_frame->rangeCount >= kProfileMaxRanges)
    {
        ++g_overflow;
        return;
    }

    const uint32_t index = g_frame->rangeCount++;
    StoredRange&   range = g_frame->ranges[index];
    range            = {};
    range.cpuStart   = qpcNow();
    range.gpuBegin   = kNoQuery;
    range.gpuEnd     = kNoQuery;
    range.view.gpuMs = -1.0f;
    range.view.color = argb;
    range.view.depth = static_cast<uint16_t>(g_depth);
    copyName(range.view.name, name);
    g_open[g_depth++] = index;
}

} // namespace

void profileStartup()
{
#if DE_GPU_MARKERS
    // Header-only NVTX resolves the injected library here. No tool means a no-op.
    nvtxInitialize(nullptr);
    DE_LOG_INFO(LogCategory::Render,
        "GPU markers on (PIX events + NVTX). Launch under PIX, Nsight Graphics, or Nsight Systems. "
        "DE_STABLE_GPU_POWER=1 locks GPU clocks and needs Windows Developer Mode. "
        "DE_D3D12_DEBUG=0 skips the debug layer in Debug builds.");
#endif
}

void profileFrameBegin()
{
    if (!g_haveRing)
    {
        g_ringIndex = 0;
        g_haveRing  = true;
    }
    else
    {
        g_ringIndex = (g_ringIndex + 1) % kFrameRing;
    }

    FrameRecord& frame = g_ring[g_ringIndex];
    if (frame.gpuPending)
        DE_LOG_WARN(LogCategory::Render, "Profile: dropped GPU timings (frame ring wrapped before readback)");
    frame             = {};
    frame.sequence    = ++g_sequence;
    frame.cpuStart    = qpcNow();
    frame.gpuSlot     = kNoSlot;
    g_frame           = &frame;
    g_collecting      = true;
    g_depth           = 0;
    g_overflow        = 0;
    g_activeSlot      = kNoSlot;
    g_cmd             = nullptr;
}

void profileFrameEnd(float dtSeconds)
{
    if (!g_collecting || g_frame == nullptr)
    {
        g_collecting = false;
        g_frame      = nullptr;
        g_depth      = 0;
        g_overflow   = 0;
        return;
    }

    FrameRecord& frame = *g_frame;
    const float  dt    = dtSeconds > 0.0f ? dtSeconds : 0.0f;
    frame.cpuMs        = static_cast<float>(qpcToMs(qpcNow() - frame.cpuStart));
    frame.frameMs      = dt * 1000.0f;
    frame.fps          = dt > 1.0e-6f ? 1.0f / dt : 0.0f;
    g_depth            = 0;
    g_overflow         = 0;
    g_collecting       = false;
    g_frame            = nullptr;
    g_cmd              = nullptr;
    g_activeSlot       = kNoSlot;
    publish(frame);
}

bool profileTimersInit(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    profileTimersShutdown();
    if (device == nullptr || queue == nullptr)
        return false;

    UINT64 frequency = 0;
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0)
    {
        DE_LOG_WARN(LogCategory::Render, "Profile: GetTimestampFrequency failed");
        return false;
    }

    for (uint32_t i = 0; i < kProfileGpuSlots; ++i)
    {
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = kMaxQueries;
        if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&g_slots[i].heap))))
        {
            DE_LOG_WARN(LogCategory::Render, "Profile: timestamp query heap failed");
            profileTimersShutdown();
            return false;
        }
        wchar_t heapName[40];
        swprintf_s(heapName, L"DE.ProfileTimestamps%u", i);
        g_slots[i].heap->SetName(heapName);

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type                 = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width                = static_cast<UINT64>(kMaxQueries) * sizeof(uint64_t);
        desc.Height               = 1;
        desc.DepthOrArraySize     = 1;
        desc.MipLevels            = 1;
        desc.Format               = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count     = 1;
        desc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_slots[i].readback))))
        {
            DE_LOG_WARN(LogCategory::Render, "Profile: timestamp readback failed");
            profileTimersShutdown();
            return false;
        }
        wchar_t readName[40];
        swprintf_s(readName, L"DE.ProfileReadback%u", i);
        g_slots[i].readback->SetName(readName);
        g_slots[i].queryCount = 0;
    }

    g_frequency = frequency;
    g_gpuReady  = true;
    DE_LOG_INFO(LogCategory::Render, "Profile: GPU timestamp queries ready");
    return true;
}

void profileTimersShutdown()
{
    g_gpuReady   = false;
    g_frequency  = 0;
    g_cmd        = nullptr;
    g_activeSlot = kNoSlot;
    g_collecting = false;
    g_frame      = nullptr;
    g_depth      = 0;
    g_overflow   = 0;
    for (uint32_t i = 0; i < kProfileGpuSlots; ++i)
    {
        g_slots[i].heap.Reset();
        g_slots[i].readback.Reset();
        g_slots[i].queryCount = 0;
    }
    for (uint32_t i = 0; i < kFrameRing; ++i)
        g_ring[i].gpuPending = false;
}

void profileTimersBeginFrame(uint32_t frameSlot, ID3D12GraphicsCommandList* cmd)
{
    if (frameSlot >= kProfileGpuSlots)
        return;

    uint64_t      bestSeq = 0;
    FrameRecord*  chosen  = nullptr;
    for (uint32_t i = 0; i < kFrameRing; ++i)
    {
        FrameRecord& frame = g_ring[i];
        if (!frame.gpuPending || frame.gpuSlot != frameSlot)
            continue;
        if (chosen == nullptr || frame.sequence >= bestSeq)
        {
            chosen  = &frame;
            bestSeq = frame.sequence;
        }
    }
    if (chosen != nullptr)
    {
        fillGpu(*chosen);
        publish(*chosen);
    }

    g_slots[frameSlot].queryCount = 0;
    g_activeSlot                  = frameSlot;
    g_cmd                         = cmd;
    if (g_frame != nullptr)
        g_frame->gpuSlot = frameSlot;
}

void profileTimersResolve(ID3D12GraphicsCommandList* cmd)
{
    if (!g_gpuReady || g_frame == nullptr || g_activeSlot >= kProfileGpuSlots || cmd == nullptr || cmd != g_cmd)
        return;
    GpuSlot& slot = g_slots[g_activeSlot];
    g_frame->queryCount = slot.queryCount;
    g_frame->gpuSlot    = g_activeSlot;
    g_frame->gpuPending = false;
    if (slot.queryCount == 0 || !slot.heap || !slot.readback)
        return;
    cmd->ResolveQueryData(slot.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, slot.queryCount, slot.readback.Get(), 0);
    g_frame->gpuPending = true;
}

const ProfileSnapshot& profileSnapshot()
{
    return g_published;
}

void profileCpuBegin(const char* name, uint32_t argb)
{
#if DE_GPU_MARKERS
    if (name != nullptr && name[0] != '\0')
    {
        PIXBeginEvent(static_cast<UINT64>(argb), "%s", name);

        nvtxEventAttributes_t attr{};
        attr.version       = NVTX_VERSION;
        attr.size          = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
        attr.colorType     = NVTX_COLOR_ARGB;
        attr.color         = argb;
        attr.messageType   = NVTX_MESSAGE_TYPE_ASCII;
        attr.message.ascii = name;
        nvtxRangePushEx(&attr);
    }
#else
    (void)argb;
#endif
    // Copy the name here. Shadow cascades format into a stack buffer that dies
    // before the panel (or the GPU readback) reads the range.
    recordCpuBegin(name, argb);
}

void profileCpuEnd()
{
#if DE_GPU_MARKERS
    nvtxRangePop();
    PIXEndEvent();
#endif
    if (!g_collecting || g_frame == nullptr)
        return;
    if (g_overflow > 0)
    {
        --g_overflow;
        return;
    }
    if (g_depth == 0)
        return;
    const uint32_t index = g_open[--g_depth];
    if (index >= g_frame->rangeCount)
        return;
    StoredRange& range = g_frame->ranges[index];
    range.view.cpuMs   = static_cast<float>(qpcToMs(qpcNow() - range.cpuStart));
}

void profileGpuBegin(ID3D12GraphicsCommandList* cmd, const char* name, uint32_t argb)
{
#if DE_GPU_MARKERS
    if (cmd != nullptr && name != nullptr && name[0] != '\0')
        PIXBeginEvent(cmd, static_cast<UINT64>(argb), "%s", name);
#else
    (void)name;
    (void)argb;
#endif
    StoredRange* range = topRange();
    if (range == nullptr || range->gpuBegin != kNoQuery)
        return;
    const uint32_t query = stamp(cmd);
    if (query != kNoQuery)
        range->gpuBegin = query;
}

void profileGpuEnd(ID3D12GraphicsCommandList* cmd)
{
    StoredRange* range = topRange();
    if (range != nullptr && range->gpuBegin != kNoQuery && range->gpuEnd == kNoQuery)
    {
        const uint32_t query = stamp(cmd);
        if (query != kNoQuery)
            range->gpuEnd = query;
    }
#if DE_GPU_MARKERS
    if (cmd != nullptr)
        PIXEndEvent(cmd);
#else
    (void)cmd;
#endif
}

} // namespace Dark
