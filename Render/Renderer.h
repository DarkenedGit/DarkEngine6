#pragma once
#include "Render/DebugRenderState.h"
#include "Render/ScenePath.h"

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <wrl/client.h>

namespace Dark
{

    class Window;
    class SceneBuffers;

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
        void clearGBuffer();
        void clearHdr();

        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE lightingTableGpu() const;
        ID3D12DescriptorHeap*       lightingHeap() const;

        DebugRenderState&       debugState() { return m_debugState; }
        const DebugRenderState& debugState() const { return m_debugState; }

    private:
        bool initD3D12(Window& window);
        bool createRenderTargets();
        bool createDepthResources();
        void updateViewport();
        bool moveToNextFrame();

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
        float            m_clearColor[4]{ 0.05f, 0.05f, 0.07f, 1.0f };
        bool             m_valid           = false;
        bool             m_vsync           = true;
        bool             m_frameSubmitted  = false;
        ScenePath        m_scenePath       = ScenePath::SwapChainForward;
        std::unique_ptr<SceneBuffers> m_sceneBuffers;
        DebugRenderState m_debugState{};
    };

} // namespace Dark
