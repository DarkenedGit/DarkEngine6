#include "Render/Profile.h"

#if !defined(DE_GPU_MARKERS)
#define DE_GPU_MARKERS 1
#endif

#if DE_GPU_MARKERS
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
// USE_PIX keeps the events in Release. pix3.h otherwise compiles them out
// unless _DEBUG or PROFILE is set, and Release is the build these tools time.
#define USE_PIX
#include <pix3.h>
#include <nvtx3/nvToolsExt.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Core/Log.h"
#endif

namespace Dark
{

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

void profileCpuBegin(const char* name, uint32_t argb)
{
#if DE_GPU_MARKERS
    if (!name || !name[0])
        return;
    PIXBeginEvent(static_cast<UINT64>(argb), "%s", name);

    nvtxEventAttributes_t attr{};
    attr.version       = NVTX_VERSION;
    attr.size          = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
    attr.colorType     = NVTX_COLOR_ARGB;
    attr.color         = argb;
    attr.messageType   = NVTX_MESSAGE_TYPE_ASCII;
    attr.message.ascii = name;
    nvtxRangePushEx(&attr);
#else
    (void)name;
    (void)argb;
#endif
}

void profileCpuEnd()
{
#if DE_GPU_MARKERS
    nvtxRangePop();
    PIXEndEvent();
#endif
}

void profileGpuBegin(ID3D12GraphicsCommandList* cmd, const char* name, uint32_t argb)
{
#if DE_GPU_MARKERS
    if (!cmd || !name || !name[0])
        return;
    PIXBeginEvent(cmd, static_cast<UINT64>(argb), "%s", name);
#else
    (void)cmd;
    (void)name;
    (void)argb;
#endif
}

void profileGpuEnd(ID3D12GraphicsCommandList* cmd)
{
#if DE_GPU_MARKERS
    if (!cmd)
        return;
    PIXEndEvent(cmd);
#else
    (void)cmd;
#endif
}

} // namespace Dark
