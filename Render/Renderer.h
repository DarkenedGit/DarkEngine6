#pragma once
#include "Render/DebugRenderState.h"
#include "Render/ScenePath.h"
#include "Core/UiPalette.h"

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <wrl/client.h>

namespace Dark
{

    class Window;
    class SceneBuffers;
    class Texture2D;
    class GpuResourceCache;

    using Microsoft::WRL::ComPtr;

    struct FrameStats
    {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
    };

    // Native Direct3D 12 renderer: device, graphics queue, flip-model swap chain,
    // double-buffered RTVs, depth buffer, and a single recording command list.
    class Renderer
    {
    public:
        static constexpr uint32_t kFrameCount = 2;

        explicit Renderer(Window& window, bool vsync = true);
        ~Renderer();

        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool isValid() const { return m_valid; }

        bool beginFrame();
        bool endFrame();
        // true if SUCCEEDED(hr), including DXGI_STATUS_OCCLUDED.
        bool present();

        void setVsync(bool vsync) { m_vsync = vsync; }

        // Recreate back buffers + depth for a new client size (no-op if unchanged).
        bool resize(uint32_t width, uint32_t height);

        // After waitForGpu on currentIndex, every in-flight slot must share that next
        // fence value. ResizeBuffers may change GetCurrentBackBufferIndex(); signaling
        // a stale smaller value can hang WaitForSingleObject on the fence.
        static void broadcastFenceValueAfterWait(uint64_t* values, uint32_t count, uint32_t currentIndex);

        // Drain the graphics queue (resource uploads, teardown).
        void waitForGpu();

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

        const FrameStats& stats() const
        {
            return m_stats;
        }
        FrameStats& stats()
        {
            return m_stats;
        }

        void bindSceneTargets();
        void bindColorTargetOnly();
        void setClearColor(float r, float g, float b, float a = 1.0f);
        const float* clearColor() const { return m_clearColor; }
        void transitionDepth(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu() const { return m_depthSrvCpu; }
        ID3D12Resource*             depthResource() const { return m_depthStencil.Get(); }
        const D3D12_VIEWPORT& viewport() const { return m_viewport; }
        const D3D12_RECT&     scissor() const { return m_scissor; }

        bool      enableSceneBuffers(ScenePath path);
        ScenePath scenePath() const { return m_scenePath; }
        bool      hasSceneBuffers() const;
        DXGI_FORMAT sceneColorFormat() const;

        void bindGBuffer();
        void bindHdr(bool bindDepth);
        void bindPostHdr();
        void bindHdrColorTarget();
        void bindTaaTarget();
        void copyPostToHistory(ID3D12GraphicsCommandList* cmd);
        void clearGBuffer();
        void clearHdr();
        void setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);
        void setHeightSrv(D3D12_CPU_DESCRIPTOR_HANDLE heightCpu);
        void setIblSrvs(D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu, D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu, D3D12_CPU_DESCRIPTOR_HANDLE brdfLutCpu);
        void setLightingAlbedoRaw(bool raw);

        bool ensureIblBrdfLut();
        D3D12_CPU_DESCRIPTOR_HANDLE iblBrdfLutCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE iblDummyCubeCpu() const { return m_iblDummyCubeCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE iblDummyLutCpu() const;

        GpuResourceCache&       gpuResources();
        const GpuResourceCache& gpuResources() const;

        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE postHdrSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE velocitySrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE historySrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE albedoSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE attribSrvCpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE lightingTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE heightTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE aoTableGpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE iblTableGpu() const;
        ID3D12DescriptorHeap*       lightingHeap() const;
        bool hasGBuffer() const;

        DebugRenderState&       debugState() { return m_debugState; }
        const DebugRenderState& debugState() const { return m_debugState; }

    private:
        bool initD3D12(Window& window);
        bool createRenderTargets();
        bool createDepthResources();
        void updateViewport();
        bool moveToNextFrame();
        void ensureIblDummyResources();
        void applyIblSrvs();

        ComPtr<ID3D12Device>              m_device;
        ComPtr<ID3D12CommandQueue>        m_commandQueue;
        ComPtr<IDXGISwapChain3>           m_swapChain;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap>      m_dsvHeap;
        ComPtr<ID3D12Resource>            m_renderTargets[kFrameCount];
        ComPtr<ID3D12Resource>            m_depthStencil;
        ComPtr<ID3D12DescriptorHeap>      m_depthSrvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_depthSrvCpu{};
        D3D12_RESOURCE_STATES             m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        ComPtr<ID3D12CommandAllocator>    m_commandAllocators[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12Fence>               m_fence;

        UINT64 m_fenceValues[kFrameCount] = {};
        HANDLE m_fenceEvent               = nullptr;

        UINT     m_frameIndex        = 0;
        UINT     m_rtvDescriptorSize = 0;
        UINT     m_swapChainFlags    = 0;
        uint32_t m_width             = 0;
        uint32_t m_height            = 0;

        D3D12_VIEWPORT m_viewport{};
        D3D12_RECT     m_scissor{};

        FrameStats       m_stats{};
        float            m_clearColor[4]{ UiPalette::kVoid.r, UiPalette::kVoid.g, UiPalette::kVoid.b, UiPalette::kVoid.a };
        bool             m_valid           = false;
        bool             m_vsync           = true;
        bool             m_frameSubmitted  = false;
        ScenePath        m_scenePath       = ScenePath::SwapChainForward;
        std::unique_ptr<SceneBuffers>     m_sceneBuffers;
        std::unique_ptr<Texture2D>        m_fogHeightDummy;
        std::unique_ptr<Texture2D>        m_iblBrdfLut;
        std::unique_ptr<Texture2D>        m_iblDummyLut;
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
