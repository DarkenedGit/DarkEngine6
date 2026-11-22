#pragma once
#include "Render/DebugRenderState.h"
#include "Render/DepthState.h"
#include "Render/RenderTargetPool.h"
#include "Render/ResourceStateTracker.h"
#include "Render/RhiDevice.h"
#include "Core/UiPalette.h"

#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <dxgi1_6.h>
#include <memory>
#include <unknwn.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    class Window;
    class DescriptorRingHeap;
    class GraphTargets;
    class SceneBuffers;
    class Texture2D;
    class GpuResourceCache;

    // No device and no window. debugState() and frameIndex() stay usable.
    struct RendererTestDevice
    {
    };

    using Microsoft::WRL::ComPtr;

    struct FrameStats
    {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
    };

    // completed: fence value that satisfied the wait.
    // waitedValue: m_fenceValues[slot] BEFORE the increment in moveToNextFrame.
    // storedValue: m_fenceValues[slot] AFTER that increment (signaled by this frame's Present).
    // Reset is legal when the wait has happened. completed >= storedValue is the wrong test.
    inline bool allocatorResetAllowed(uint64_t completed, uint64_t waitedValue, uint64_t storedValue)
    {
        return completed >= waitedValue && storedValue == waitedValue + 1;
    }

    // Native Direct3D 12 renderer: device, graphics queue, flip-model swap chain,
    // double-buffered RTVs, depth buffer, the main command list, and one shadow list per slot.
    class Renderer
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        explicit Renderer(Window& window, bool vsync = true);
        explicit Renderer(RendererTestDevice);
        ~Renderer();

        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool isValid() const { return m_valid; }
        // Device caps filled at startup (plan §3.15). Zeroed for the RendererTestDevice shell.
        const RhiCaps& caps() const { return m_caps; }

        bool beginFrame();
        bool endFrame();

        // Off: one ExecuteCommandLists of m_commandList and the unfiltered cull.
        void setShadowSeparateList(bool enabled) { m_shadowSeparateList = enabled; }
        bool shadowSeparateList() const { return m_shadowSeparateList; }

        // Only Reset of this slot's shadow allocator. moveToNextFrame already waited it.
        // Does not wait again and does not compare the post-increment fence value.
        ID3D12GraphicsCommandList* beginShadowList();
        // Close only. endFrame is what submits [shadow, main].
        bool closeShadowList();
        // No Reset. The shadow worker records with these after beginFrame waited the slot.
        ID3D12GraphicsCommandList* shadowCommandList() const;
        ID3D12CommandAllocator*    shadowCommandAllocator() const;
        void noteShadowListReset();
        void adoptClosedShadowList();
        // true if SUCCEEDED(hr), including DXGI_STATUS_OCCLUDED.
        bool present();

        void setVsync(bool vsync) { m_vsync = vsync; }

        // Recreate back buffers + depth for a new client size (no-op if unchanged).
        bool resize(uint32_t width, uint32_t height);

        // After waitForGpu on currentIndex, every in-flight slot must share that next
        // fence value. ResizeBuffers may change GetCurrentBackBufferIndex(); signaling
        // a stale smaller value can hang WaitForSingleObject on the fence.
        static void broadcastFenceValueAfterWait(uint64_t* values, uint32_t count, uint32_t currentIndex);

        // Drain the graphics queue (teardown / resize). Uploads should not call this.
        void waitForGpu();

        // GPU copy of upload→default buffers. Prefers the COPY queue, then
        // graphics-queue Wait — CPU does not stall. srcA/srcB must stay alive
        // until pumpDeferredReleases (caller should deferRelease them).
        bool submitBufferCopies(ID3D12Resource* dstA, ID3D12Resource* srcA, uint64_t bytesA, ID3D12Resource* dstB, ID3D12Resource* srcB, uint64_t bytesB);

        // Copy upload footprints into a texture created in COMMON. Records on the
        // graphics queue: COMMON → COPY_DEST, copy, then PIXEL_SHADER_RESOURCE.
        // A COPY list rejects COPY_DEST (#1334) and did not leave sampled texels.
        // CPU does not stall. upload must stay alive until pumpDeferredReleases
        // (caller should deferRelease it). *queued is set once the GPU may reference
        // dst or upload; on false with queued, keep both alive via deferRelease.
        bool submitTextureCopies(ID3D12Resource* dst, ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* footprints, UINT count, bool* queued = nullptr);

        // Per-frame upload into a texture left in COMMON (first use of the slot) or
        // PIXEL_SHADER_RESOURCE. One graphics list: that state → COPY_DEST, copy,
        // then PIXEL_SHADER_RESOURCE, then signal m_texUpdateFence. Does not signal
        // m_fenceValues, does not CreateCommandAllocator, and does not wait on the CPU.
        bool submitTextureUpdate(ID3D12Resource* dst, D3D12_RESOURCE_STATES before, ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

        // Frame capture: the next endFrame copies the back buffer (after every pass, ImGui
        // included) to a readback buffer, drains the queue once, and writes an opaque PNG.
        // One shot; a second request before endFrame replaces the path.
        void requestBackBufferCapture(const std::filesystem::path& png);
        // True when the last requested capture was written. Clears the result.
        bool takeBackBufferCaptureResult();

        // Logs the removed reason and, when DRED was on at device creation, the auto-breadcrumb
        // trail and the page-fault allocations. Once per device. Safe with no device.
        void logDeviceRemoved(const char* where);

        // Adapter the device was created on. Zero if the device is not up.
        LUID adapterLuid() const;

        // Reset the current slot's closed upload DIRECT allocator and list. The COPY
        // list and m_commandList are not touched. One record after this; do not Reset again.
        ID3D12GraphicsCommandList* beginTexUpdateDirect();

        // Close the list beginTexUpdateDirect opened, execute it on the graphics queue,
        // and signal m_texUpdateFence. Does not Reset the allocator.
        bool executeTexUpdateDirect();

        ID3D12Fence* texUpdateFence() const
        {
            return m_texUpdateFence.Get();
        }
        // Last value signaled on texUpdateFence. 0 if nothing has been signaled.
        UINT64 texUpdateFenceValue() const
        {
            return m_texUpdateFenceValue;
        }

        // Keep a resource/allocator/list until in-flight GPU work that used it is done.
        void deferRelease(IUnknown* obj);
        void pumpDeferredReleases();

        ID3D12Device* device()
        {
            return m_device.Get();
        }
        ID3D12GraphicsCommandList* commandList()
        {
            return m_commandList.Get();
        }
        ID3D12CommandQueue* queue()
        {
            return m_commandQueue.Get();
        }
        IDXGISwapChain3* swapChain()
        {
            return m_swapChain.Get();
        }

        uint32_t width() const
        {
            return m_width;
        }
        uint32_t height() const
        {
            return m_height;
        }
        uint32_t frameIndex() const
        {
            return m_frameIndex;
        }
        void setFrameIndexForTest(uint32_t frame);

        const FrameStats& stats() const
        {
            return m_stats;
        }
        FrameStats& stats()
        {
            return m_stats;
        }

        void bindColorTargetOnly();
        void setClearColor(float r, float g, float b, float a = 1.0f);
        const float* clearColor() const { return m_clearColor; }
        void transitionDepth(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu() const { return m_depthSrvCpu; }
        ID3D12Resource*             depthResource() const;
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE depthReadOnlyDsv() const;
        // Graph-owned persistent targets (render-graph plan Phase 5). Null on a test shell.
        GraphTargets*               targets() const { return m_targets.get(); }
        // The shader-visible descriptor ring (render-graph plan §3.9); null when it failed to create.
        DescriptorRingHeap*         descriptorRing() const { return m_descriptorRing.get(); }
        // GPU drains taken by resize(); the Phase 5 gate expects exactly one per resize.
        uint32_t                    resizeDrainCount() const { return m_resizeDrains; }
        ID3D12Resource*             albedoResource() const;
        ID3D12Resource*             attribResource() const;
        // Frame graph imports (render-graph plan Phase 4). Null when the target does not exist.
        ID3D12Resource*             backBufferResource() const;
        ID3D12Resource*             velocityResource() const;
        ID3D12Resource*             aoResource() const;
        ID3D12Resource*             postResource() const;
        float                       depthClearValue() const { return kDepthClear; }
        const D3D12_VIEWPORT& viewport() const { return m_viewport; }
        const D3D12_RECT&     scissor() const { return m_scissor; }

        // HDR + G-buffer targets for the 3D hosts (Sandbox, Editor). A 3D host treats failure as a fatal init error.
        // 2D hosts never call it and draw straight to the swap chain.
        bool        createSceneTargets();
        bool        hasSceneBuffers() const;
        DXGI_FORMAT sceneColorFormat() const;

        void bindGBuffer();
        void bindHdr(bool bindDepth);
        void bindHdrDepthRead();
        // OM only. HDR target, read-only scene depth. No particle arguments.
        void bindHdrDepthCompute();
        void bindDecalTargets();
        void bindPostHdr();
        void bindHdrColorTarget();
        // Post HDR + the TAA history RTV (graph history, current slot) as MRT.
        void bindTaaTargets(D3D12_CPU_DESCRIPTOR_HANDLE historyRtv);
        void clearGBuffer();
        void clearHdr();
        void setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);
        void setLocalShadowSrvs(D3D12_CPU_DESCRIPTOR_HANDLE arrayCpu, D3D12_CPU_DESCRIPTOR_HANDLE recordsCpu);
        void setLightingAoSrv(D3D12_CPU_DESCRIPTOR_HANDLE aoCpu);
        void setLightingSsrSrv(D3D12_CPU_DESCRIPTOR_HANDLE ssrCpu);
        void setHeightSrv(D3D12_CPU_DESCRIPTOR_HANDLE heightCpu);
        void setIblSrvs(D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu, D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu, D3D12_CPU_DESCRIPTOR_HANDLE brdfLutCpu);
        void setLightingAlbedoRaw(bool raw);

        bool ensureIblBrdfLut();
        D3D12_CPU_DESCRIPTOR_HANDLE iblBrdfLutCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE iblDummyCubeCpu() const { return m_iblDummyCubeCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE iblDummyLutCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE ssrDummyCpu() const;

        GpuResourceCache&       gpuResources();
        const GpuResourceCache& gpuResources() const;

        ID3D12Resource*             hdr() const;
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE postHdrSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE velocitySrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE albedoSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE attribSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE aoSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE lightingAoCpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE lightingTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE heightTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE aoTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE iblTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE ssrTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE localShadowTableGpu() const;
        ID3D12DescriptorHeap*       lightingHeap() const;

        DebugRenderState&       debugState() { return m_debugState; }
        const DebugRenderState& debugState() const { return m_debugState; }

    private:
        bool initD3D12(Window& window);
        bool createRenderTargets();
        bool createDepthResources();
        void refreshDepthViews();
        void updateViewport();
        bool moveToNextFrame();
        bool signalTexUpdate(ID3D12CommandQueue* queue, const char* what);
        void closeProfileFrame();
        void ensureIblDummyResources();
        void ensureSsrDummyResources();
        void applyIblSrvs();

        void recordBackBufferCapture();
        void writeBackBufferCapture();
        void configureDiagnostics(); // before CreateDXGIFactory / D3D12CreateDevice
        void attachDebugMessageLog(); // after D3D12CreateDevice: debug-layer and GBV messages -> DarkEngine6.log
        void detachDebugMessageLog(); // logs the message totals

        ComPtr<ID3D12Device>              m_device;
        RhiCaps                           m_caps;
        std::unique_ptr<GraphTargets>     m_targets; // after m_device; shut down explicitly in ~Renderer
        std::unique_ptr<DescriptorRingHeap> m_descriptorRing; // after m_device
        ComPtr<ID3D12CommandQueue>        m_commandQueue;
        ComPtr<ID3D12CommandQueue>        m_copyQueue;
        ComPtr<ID3D12Fence>               m_copyFence;
        UINT64                            m_copyFenceValue = 1;
        struct DeferredGpu
        {
            Microsoft::WRL::ComPtr<IUnknown> obj;
            int                              framesLeft = 0;
        };
        std::vector<DeferredGpu>          m_deferredGpu;
        ComPtr<IDXGISwapChain3>           m_swapChain;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
        // Enhanced-barrier state of the back buffers and every graph target (Phase 6); the main thread's
        // frame tracker from initD3D12 until the destructor.
        ResourceStateTracker              m_stateTracker;
        ComPtr<ID3D12Resource>            m_renderTargets[kFrameCount];
        Rg::TargetId                      m_depthId;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_depthSrvCpu{};
        ComPtr<ID3D12CommandAllocator>    m_commandAllocators[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12CommandAllocator>    m_texUpdateCopyAlloc[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> m_texUpdateCopyList[kFrameCount];
        ComPtr<ID3D12CommandAllocator>    m_texUpdateDirectAlloc[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> m_texUpdateDirectList[kFrameCount];
        ComPtr<ID3D12CommandAllocator>    m_shadowAlloc[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> m_shadowList[kFrameCount];
        ComPtr<ID3D12Fence>               m_texUpdateFence;
        UINT64                            m_texUpdateFenceValue = 0;
        ComPtr<ID3D12Fence>               m_fence;
        ComPtr<ID3D12Resource>            m_captureReadback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_captureFootprint{};
        UINT64                             m_captureBytes = 0; // GetCopyableFootprints total; the Map range
        std::filesystem::path             m_capturePath;
        bool                              m_capturePending = false;
        bool                              m_captureRecorded = false;
        bool                              m_captureWritten = false;
        ComPtr<IUnknown>                  m_infoQueue; // ID3D12InfoQueue1 when the runtime has it
        DWORD                             m_infoQueueCookie = 0;
        bool                              m_debugLayerEnabled = false;
        bool                              m_dredEnabled = false;
        bool                              m_removedLogged = false;

        UINT64 m_fenceValues[kFrameCount] = {};
        HANDLE m_fenceEvent               = nullptr;

        UINT     m_frameIndex        = 0;
        UINT     m_rtvDescriptorSize = 0;
        uint32_t m_resizeDrains      = 0;
        UINT     m_swapChainFlags    = 0;
        uint32_t m_width             = 0;
        uint32_t m_height            = 0;

        D3D12_VIEWPORT m_viewport{};
        D3D12_RECT     m_scissor{};

        FrameStats       m_stats{};
        float            m_clearColor[4]{ UiPalette::kVoid.r, UiPalette::kVoid.g, UiPalette::kVoid.b, UiPalette::kVoid.a };
        bool             m_valid               = false;
        bool             m_testShell       = false;
        bool             m_vsync               = true;
        bool             m_shadowSeparateList  = true;
        bool             m_shadowListOpen      = false;
        bool             m_shadowListRecorded  = false;
        bool             m_frameSubmitted      = false;
        bool             m_profileFrame    = false;
        std::unique_ptr<SceneBuffers>     m_sceneBuffers;
        std::unique_ptr<Texture2D>        m_fogHeightDummy;
        std::unique_ptr<Texture2D>        m_iblBrdfLut;
        std::unique_ptr<Texture2D>        m_iblDummyLut;
        std::unique_ptr<Texture2D>        m_ssrDummy;
        ComPtr<ID3D12Resource>            m_iblDummyCube;
        ComPtr<ID3D12DescriptorHeap>      m_iblDummyCubeCpuHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_iblDummyCubeCpu{};
        std::unique_ptr<GpuResourceCache> m_gpuResources;
        D3D12_CPU_DESCRIPTOR_HANDLE   m_heightCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE   m_iblIrrCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE   m_iblPrefCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE   m_iblLutCpu{};
        DebugRenderState m_debugState{};
    };

} // namespace Dark
