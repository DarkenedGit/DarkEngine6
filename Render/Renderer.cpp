#include "Render/Renderer.h"
#include "Render/GpuIbl.h"
#include "Render/GpuResourceCache.h"
#include "Render/IblBake.h"
#include "Render/SceneBuffers.h"
#include "Render/Texture2D.h"
#include "Assets/Image.h"
#include "Core/ContentRoots.h"
#include "Core/Window.h"
#include "Core/Log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark
{

    namespace
    {
        bool checkHr(HRESULT hr, const char* what)
        {
            if (FAILED(hr))
            {
                DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        constexpr uint32_t kBrdfLutMagic = 0x554c4544u; // 'DELU'

        std::filesystem::path brdfLutCachePath(uint32_t size, uint32_t samples)
        {
            char name[64];
            std::snprintf(name, sizeof(name), "brdf_lut_%u_%u.bin", size, samples);
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                std::error_code ec;
                const std::filesystem::path dir = root / "env";
                if (std::filesystem::is_directory(dir, ec) && !ec)
                    return dir / name;
            }
            const std::vector<std::filesystem::path> roots = contentRootCandidates();
            if (!roots.empty())
                return roots.front() / "env" / name;
            return {};
        }

        bool loadBrdfLutCache(const std::filesystem::path& path, float* rg, uint32_t size, uint32_t samples)
        {
            if (path.empty() || !rg)
                return false;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                return false;
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            uint32_t magic = 0, fileSize = 0, fileSamples = 0;
            in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            in.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));
            in.read(reinterpret_cast<char*>(&fileSamples), sizeof(fileSamples));
            if (!in || magic != kBrdfLutMagic || fileSize != size || fileSamples != samples)
                return false;
            const std::streamsize bytes = static_cast<std::streamsize>(size) * size * 2 * static_cast<std::streamsize>(sizeof(float));
            in.read(reinterpret_cast<char*>(rg), bytes);
            return static_cast<bool>(in);
        }

        void saveBrdfLutCache(const std::filesystem::path& path, const float* rg, uint32_t size, uint32_t samples)
        {
            if (path.empty() || !rg)
                return;
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
                return;
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            const uint32_t magic = kBrdfLutMagic;
            out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            out.write(reinterpret_cast<const char*>(&size), sizeof(size));
            out.write(reinterpret_cast<const char*>(&samples), sizeof(samples));
            const std::streamsize bytes = static_cast<std::streamsize>(size) * size * 2 * static_cast<std::streamsize>(sizeof(float));
            out.write(reinterpret_cast<const char*>(rg), bytes);
        }
    } // namespace

#if defined(_DEBUG)
    void EnableDebugLayer()
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            DE_LOG_INFO(LogCategory::Render, "D3D12 debug layer enabled");
        }
    }
#endif

    Renderer::Renderer(Window& window, bool vsync)
        : m_vsync(vsync)
    {
        m_valid = initD3D12(window);
    }

    Renderer::~Renderer()
    {
        waitForGpu();
        if (m_copyQueue && m_copyFence)
        {
            const UINT64 v = m_copyFenceValue;
            m_copyQueue->Signal(m_copyFence.Get(), v);
            if (m_copyFence->GetCompletedValue() < v && m_fenceEvent)
            {
                m_copyFence->SetEventOnCompletion(v, m_fenceEvent);
                WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
            }
        }
        m_deferredGpu.clear();
        if (m_gpuResources)
        {
            m_gpuResources->clear();
            m_gpuResources.reset();
        }
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
    }

    bool Renderer::initD3D12(Window& window)
    {
        m_width  = window.width();
        m_height = window.height();

        HWND hwnd = static_cast<HWND>(window.nativeHandle());
        if (!hwnd)
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: window has no HWND");
            return false;
        }

#if defined(_DEBUG)
        EnableDebugLayer();
#endif

        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        ComPtr<IDXGIFactory6> factory;
        if (!checkHr(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2"))
            return false;

        // Prefer a hardware adapter that supports D3D12 feature level 11_0+.
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                break;

            adapter.Reset();
        }

        if (!adapter)
        {
            // Fallback: WARP (software) for machines without a D3D12 GPU.
            ComPtr<IDXGIAdapter> warp;
            if (!checkHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter"))
                return false;
            if (!checkHr(warp.As(&adapter), "WARP As IDXGIAdapter1"))
                return false;
            DE_LOG_WARN(LogCategory::Render, "Renderer: using WARP software adapter");
        }

        if (!checkHr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice"))
            return false;

        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            char name[128]{};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            DE_LOG_INFO(LogCategory::Render, "D3D12 device: {}", name);
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (!checkHr(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue"))
            return false;

        D3D12_COMMAND_QUEUE_DESC copyDesc{};
        copyDesc.Type  = D3D12_COMMAND_LIST_TYPE_COPY;
        copyDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (!checkHr(m_device->CreateCommandQueue(&copyDesc, IID_PPV_ARGS(&m_copyQueue)), "CreateCommandQueue (copy)"))
            return false;

        m_swapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width       = m_width;
        scDesc.Height      = m_height;
        scDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc  = { 1, 0 };
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = kFrameCount;
        scDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.Scaling     = DXGI_SCALING_STRETCH;
        scDesc.Flags       = m_swapChainFlags;

        ComPtr<IDXGISwapChain1> swapChain1;
        if (!checkHr(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain1), "CreateSwapChainForHwnd"))
            return false;

        // Alt-Enter handled by the app if desired; disable DXGI's default fullscreen toggle.
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        if (!checkHr(swapChain1.As(&m_swapChain), "QueryInterface IDXGISwapChain3"))
            return false;
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // RTV heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = kFrameCount;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap RTV"))
            return false;
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!createRenderTargets())
            return false;

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (!checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])), "CreateCommandAllocator"))
                return false;
        }

        // DSV heap + depth buffer (slot 0 write, slot 1 READ_ONLY_DEPTH for depth-as-SRV)
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = 2;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap DSV"))
            return false;
        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        if (!createDepthResources())
            return false;

        if (!checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList"))
            return false;
        // Start closed; beginFrame resets and opens it each frame.
        if (!checkHr(m_commandList->Close(), "CommandList Close (init)"))
            return false;

        if (!checkHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence"))
            return false;
        if (!checkHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)), "CreateFence (copy)"))
            return false;
        // Match D3D12HelloFrameBuffering: fence starts at 0; the value we will
        // Signal after the first use of this back-buffer slot is 1.
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            m_fenceValues[i] = 0;
        }
        m_fenceValues[m_frameIndex] = 1;

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
        {
            DE_LOG_ERROR(LogCategory::Render, "CreateEvent for fence failed");
            return false;
        }

        updateViewport();

        m_gpuResources = std::make_unique<GpuResourceCache>(*this);
        DE_LOG_INFO(LogCategory::Render, "Renderer: D3D12 ready ({}x{}, {} buffers)", m_width, m_height, kFrameCount);
        return true;
    }

    GpuResourceCache& Renderer::gpuResources()
    {
        DE_ASSERT(m_gpuResources);
        return *m_gpuResources;
    }

    const GpuResourceCache& Renderer::gpuResources() const
    {
        DE_ASSERT(m_gpuResources);
        return *m_gpuResources;
    }

    bool Renderer::createRenderTargets()
    {
        if (!m_device || !m_swapChain || !m_rtvHeap)
            return false;

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))) || !m_renderTargets[i])
            {
                DE_LOG_ERROR(LogCategory::Render, "Renderer: SwapChain GetBuffer failed");
                return false;
            }
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;
        }
        return true;
    }

    void Renderer::updateViewport()
    {
        m_viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
        m_scissor  = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    }

    bool Renderer::createDepthResources()
    {
        if (!m_device || !m_dsvHeap || m_width == 0 || m_height == 0)
            return false;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width            = m_width;
        depthDesc.Height           = m_height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels        = 1;
        depthDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
        depthDesc.SampleDesc       = { 1, 0 };
        depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear{};
        clear.Format               = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth   = depthClearValue();
        clear.DepthStencil.Stencil = 0;

        if (FAILED(m_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                IID_PPV_ARGS(&m_depthStencil))))
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: CreateCommittedResource depth failed");
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsvWrite = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvRead  = dsvWrite;
        dsvRead.ptr += m_dsvDescriptorSize;

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
        m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, dsvWrite);
        dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, dsvRead);

        m_depthSrvHeap.Reset();
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_depthSrvHeap))))
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: CreateDescriptorHeap depth SRV failed");
            return false;
        }
        m_depthSrvCpu = m_depthSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;
        m_device->CreateShaderResourceView(m_depthStencil.Get(), &srvDesc, m_depthSrvCpu);
        m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        DE_LOG_INFO(LogCategory::Render, "Renderer: depth D32_FLOAT clear=0 reverse-Z");
        return true;
    }

    bool Renderer::resize(uint32_t width, uint32_t height)
    {
        if (!m_swapChain || !m_device)
            return false;
        if (width == 0 || height == 0)
            return true;
        if (width == m_width && height == m_height)
            return true;
        if (FAILED(m_device->GetDeviceRemovedReason()))
            return false;

        waitForGpu();
        broadcastFenceValueAfterWait(m_fenceValues, kFrameCount, m_frameIndex);

        for (uint32_t i = 0; i < kFrameCount; ++i)
            m_renderTargets[i].Reset();
        m_depthStencil.Reset();
        m_depthSrvHeap.Reset();
        m_depthSrvCpu = {};

        const HRESULT hr = m_swapChain->ResizeBuffers(
            kFrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, m_swapChainFlags);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: ResizeBuffers failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        m_width  = width;
        m_height = height;
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        if (!createRenderTargets() || !createDepthResources())
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: resize recreate targets failed");
            return false;
        }

        if (m_sceneBuffers)
        {
            const bool gbuffer = m_scenePath == ScenePath::HybridDeferred;
            if (!m_sceneBuffers->create(m_device.Get(), m_width, m_height, gbuffer, m_depthSrvCpu, m_clearColor))
            {
                DE_LOG_ERROR(LogCategory::Render, "Renderer: SceneBuffers resize failed");
                m_sceneBuffers.reset();
                return false;
            }
            m_sceneBuffers->setLightingAlbedoRaw(m_device.Get(), m_debugState.showAlbedoRaw);
            setHeightSrv(m_heightCpu.ptr != 0 ? m_heightCpu : (m_fogHeightDummy && m_fogHeightDummy->valid() ? m_fogHeightDummy->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{}));
            applyIblSrvs();
            ensureSsrDummyResources();
            setLightingSsrSrv(ssrDummyCpu());
        }

        updateViewport();
        DE_LOG_INFO(LogCategory::Render, "Renderer: resized to {}x{}", m_width, m_height);
        return true;
    }

    void Renderer::broadcastFenceValueAfterWait(uint64_t* values, uint32_t count, uint32_t currentIndex)
    {
        if (!values || count == 0 || currentIndex >= count)
            return;
        const uint64_t next = values[currentIndex];
        for (uint32_t i = 0; i < count; ++i)
            values[i] = next;
    }

    bool Renderer::beginFrame()
    {
        m_stats          = {};
        m_frameSubmitted = false;

        if (!m_valid || !m_device || !m_commandList || !m_fence)
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer::beginFrame: device not initialized");
            return false;
        }
        if (!m_renderTargets[m_frameIndex])
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer::beginFrame: missing back buffer");
            return false;
        }

        pumpDeferredReleases();

        // m_frameIndex was advanced in moveToNextFrame(), which already waited until
        // this slot's prior GPU work finished. Safe to reset its allocator now.
        if (!checkHr(m_commandAllocators[m_frameIndex]->Reset(), "CommandAllocator Reset"))
            return false;
        if (!checkHr(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "CommandList Reset"))
            return false;

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        D3D12_RESOURCE_BARRIER barriers[2]{};
        UINT barrierCount = 0;
        barriers[barrierCount].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource   = m_renderTargets[m_frameIndex].Get();
        barriers[barrierCount].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barriers[barrierCount].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        if (m_depthStencil && m_depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
        {
            barriers[barrierCount].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource   = m_depthStencil.Get();
            barriers[barrierCount].Transition.StateBefore = m_depthState;
            barriers[barrierCount].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        m_commandList->ResourceBarrier(barrierCount, barriers);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        m_commandList->ClearRenderTargetView(rtv, m_clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depthClearValue(), 0, 0, nullptr);
        return true;
    }

    void Renderer::setClearColor(float r, float g, float b, float a)
    {
        m_clearColor[0] = r;
        m_clearColor[1] = g;
        m_clearColor[2] = b;
        m_clearColor[3] = a;
    }

    void Renderer::bindSceneTargets()
    {
        if (!m_commandList)
            return;
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    }

    void Renderer::bindColorTargetOnly()
    {
        if (!m_commandList)
            return;
        if (m_sceneBuffers)
        {
            m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_sceneBuffers->transitionPost(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    void Renderer::transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!m_sceneBuffers)
            return;
        m_sceneBuffers->transitionVelocity(cmd, after);
    }

    void Renderer::transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!m_sceneBuffers)
            return;
        m_sceneBuffers->transitionAlbedo(cmd, after);
    }

    void Renderer::transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!m_sceneBuffers)
            return;
        m_sceneBuffers->transitionAttrib(cmd, after);
    }

    void Renderer::transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!m_sceneBuffers)
            return;
        m_sceneBuffers->transitionHdr(cmd, after);
    }

    void Renderer::transitionDepth(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!cmd || !m_depthStencil || m_depthState == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_depthStencil.Get();
        b.Transition.StateBefore = m_depthState;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        m_depthState = after;
    }

    bool Renderer::endFrame()
    {
        if (!m_valid || !m_commandList || !m_commandQueue)
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer::endFrame: device not initialized");
            return false;
        }

        // RENDER_TARGET -> PRESENT
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        if (!checkHr(m_commandList->Close(), "CommandList Close"))
            return false;

        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
        m_frameSubmitted = true;
        return true;
    }

    bool Renderer::present()
    {
        if (!m_valid || !m_swapChain)
        {
            DE_LOG_ERROR(LogCategory::Render, "Present: renderer not initialized");
            return false;
        }
        if (!m_frameSubmitted)
        {
            DE_LOG_ERROR(LogCategory::Render, "Present: no frame submitted");
            return false;
        }

        const HRESULT hr = m_swapChain->Present(m_vsync ? 1u : 0u, 0);
        if (FAILED(hr))
        {
            const HRESULT removed = m_device ? m_device->GetDeviceRemovedReason() : S_OK;
            if (FAILED(removed))
                DE_LOG_ERROR(LogCategory::Render, "Present: device removed ({})", static_cast<unsigned>(removed));
            else
                DE_LOG_ERROR(LogCategory::Render, "Present failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        return moveToNextFrame();
    }

    bool Renderer::moveToNextFrame()
    {
        if (!m_commandQueue || !m_fence || !m_swapChain)
            return false;

        // Signal a unique, monotonically increasing fence value for the work just
        // submitted with this back-buffer / allocator slot.
        const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
        if (!checkHr(m_commandQueue->Signal(m_fence.Get(), currentFenceValue), "Queue Signal"))
            return false;

        // Advance to the swap-chain's next buffer.
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Block only if the GPU has not finished the last submission that used
        // this slot's command allocator (cannot Reset it until then).
        if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
        {
            if (!checkHr(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "SetEventOnCompletion (next frame)"))
                return false;
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        // Next signal for this slot must be strictly greater than any prior signal.
        m_fenceValues[m_frameIndex] = currentFenceValue + 1;
        return true;
    }

    void Renderer::waitForGpu()
    {
        if (!m_commandQueue || !m_fence || !m_fenceEvent)
            return;

        // Drain the queue so teardown can destroy resources safely.
        const UINT64 fenceValue = m_fenceValues[m_frameIndex];
        if (FAILED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
            return;

        if (m_fence->GetCompletedValue() < fenceValue)
        {
            m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        m_fenceValues[m_frameIndex] = fenceValue + 1;
    }

    void Renderer::deferRelease(IUnknown* obj)
    {
        if (!obj)
            return;
        DeferredGpu d;
        d.obj        = obj;
        d.framesLeft = static_cast<int>(kFrameCount) + 1;
        m_deferredGpu.push_back(std::move(d));
    }

    void Renderer::pumpDeferredReleases()
    {
        for (size_t i = 0; i < m_deferredGpu.size();)
        {
            if (--m_deferredGpu[i].framesLeft <= 0)
            {
                m_deferredGpu[i] = std::move(m_deferredGpu.back());
                m_deferredGpu.pop_back();
            }
            else
                ++i;
        }
    }

    bool Renderer::submitBufferCopies(ID3D12Resource* dstA, ID3D12Resource* srcA, uint64_t bytesA, ID3D12Resource* dstB, ID3D12Resource* srcB, uint64_t bytesB)
    {
        if (!m_device || !dstA || !srcA || bytesA == 0)
            return false;
        ID3D12CommandQueue* q      = m_copyQueue.Get() ? m_copyQueue.Get() : m_commandQueue.Get();
        const D3D12_COMMAND_LIST_TYPE listType = m_copyQueue ? D3D12_COMMAND_LIST_TYPE_COPY : D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!q)
            return false;

        ComPtr<ID3D12CommandAllocator>    alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (!checkHr(m_device->CreateCommandAllocator(listType, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (buffer copy)")
            || !checkHr(m_device->CreateCommandList(0, listType, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (buffer copy)"))
            return false;

        list->CopyBufferRegion(dstA, 0, srcA, 0, bytesA);
        if (dstB && srcB && bytesB > 0)
            list->CopyBufferRegion(dstB, 0, srcB, 0, bytesB);

        if (listType == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            D3D12_RESOURCE_BARRIER barriers[2]{};
            UINT                   n = 0;
            barriers[n].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[n].Transition.pResource   = dstA;
            barriers[n].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[n].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barriers[n].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++n;
            if (dstB)
            {
                barriers[n].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[n].Transition.pResource   = dstB;
                barriers[n].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barriers[n].Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
                barriers[n].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++n;
            }
            list->ResourceBarrier(n, barriers);
        }

        if (!checkHr(list->Close(), "Close buffer copy list"))
            return false;
        ID3D12CommandList* lists[] = { list.Get() };
        q->ExecuteCommandLists(1, lists);

        if (m_copyQueue && m_copyFence && m_commandQueue)
        {
            const UINT64 v = m_copyFenceValue++;
            if (FAILED(m_copyQueue->Signal(m_copyFence.Get(), v)))
                return false;
            if (FAILED(m_commandQueue->Wait(m_copyFence.Get(), v)))
                return false;
        }

        deferRelease(alloc.Get());
        deferRelease(list.Get());
        return true;
    }

    bool Renderer::hasSceneBuffers() const
    {
        return m_sceneBuffers && m_sceneBuffers->valid();
    }

    DXGI_FORMAT Renderer::sceneColorFormat() const
    {
        return hasSceneBuffers() ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    bool Renderer::enableSceneBuffers(ScenePath path)
    {
        if (!m_valid || !m_device)
        {
            DE_LOG_ERROR(LogCategory::Render, "enableSceneBuffers: device not initialized");
            return false;
        }

        if (path == ScenePath::SwapChainForward)
        {
            if (m_sceneBuffers)
            {
                waitForGpu();
                m_sceneBuffers.reset();
            }
            m_scenePath = ScenePath::SwapChainForward;
            return true;
        }

        const bool gbuffer = path == ScenePath::HybridDeferred;
        if (m_sceneBuffers && m_scenePath == path && m_sceneBuffers->matches(m_width, m_height, gbuffer))
            return true;

        waitForGpu();
        auto buffers = std::make_unique<SceneBuffers>();
        if (!buffers->create(m_device.Get(), m_width, m_height, gbuffer, m_depthSrvCpu, m_clearColor))
        {
            DE_LOG_ERROR(LogCategory::Render, "enableSceneBuffers: SceneBuffers create failed");
            m_sceneBuffers.reset();
            m_scenePath = ScenePath::SwapChainForward;
            return false;
        }
        m_sceneBuffers = std::move(buffers);
        m_scenePath    = path;
        m_sceneBuffers->setLightingAlbedoRaw(m_device.Get(), m_debugState.showAlbedoRaw);
        if (!m_fogHeightDummy)
            m_fogHeightDummy = std::make_unique<Texture2D>();
        if (!m_fogHeightDummy->valid())
        {
            const float dry = 1.0e6f;
            m_fogHeightDummy->createFromR32Float(*this, &dry, 1, 1, static_cast<uint32_t>(sizeof(float)));
        }
        setHeightSrv(m_heightCpu.ptr != 0 ? m_heightCpu : (m_fogHeightDummy->valid() ? m_fogHeightDummy->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{}));
        applyIblSrvs();
        ensureSsrDummyResources();
        setLightingSsrSrv(ssrDummyCpu());
        if (path == ScenePath::HybridDeferred)
            ensureIblBrdfLut();
        DE_LOG_INFO(LogCategory::Render, "enableSceneBuffers: path={} {}x{}", static_cast<unsigned>(path), m_width, m_height);
        return true;
    }

    bool Renderer::ensureIblBrdfLut()
    {
        if (m_iblBrdfLut && m_iblBrdfLut->valid())
            return true;
        if (!m_device)
        {
            DE_LOG_ERROR(LogCategory::Render, "ensureIblBrdfLut: null device");
            return false;
        }
        const IblBakeSettings settings{};
        std::vector<float>    rg(static_cast<size_t>(settings.brdfLutSize) * settings.brdfLutSize * 2u);
        const std::filesystem::path cache = brdfLutCachePath(settings.brdfLutSize, settings.sampleCountLut);
        if (!loadBrdfLutCache(cache, rg.data(), settings.brdfLutSize, settings.sampleCountLut))
        {
            if (!IblBake::generateBrdfLut(rg.data(), settings.brdfLutSize, settings.sampleCountLut))
                return false;
            saveBrdfLutCache(cache, rg.data(), settings.brdfLutSize, settings.sampleCountLut);
            DE_LOG_INFO(LogCategory::Render, "Ibl: generated BRDF LUT {}x{} ({} samples)", settings.brdfLutSize, settings.brdfLutSize,
                        settings.sampleCountLut);
        }
        else
            DE_LOG_INFO(LogCategory::Render, "Ibl: loaded BRDF LUT cache '{}'", cache.string());
        if (!m_iblBrdfLut)
            m_iblBrdfLut = std::make_unique<Texture2D>();
        const uint32_t pitch = settings.brdfLutSize * 8u;
        if (!m_iblBrdfLut->createFromRgFloat(*this, rg.data(), settings.brdfLutSize, settings.brdfLutSize, pitch))
        {
            DE_LOG_ERROR(LogCategory::Render, "ensureIblBrdfLut: RG16F upload failed");
            return false;
        }
        if (m_iblBrdfLut->resource())
            m_iblBrdfLut->resource()->SetName(L"DE.Ibl.BrdfLut");
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::iblBrdfLutCpu() const
    {
        return (m_iblBrdfLut && m_iblBrdfLut->valid()) ? m_iblBrdfLut->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::iblDummyLutCpu() const
    {
        return (m_iblDummyLut && m_iblDummyLut->valid()) ? m_iblDummyLut->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::ssrDummyCpu() const
    {
        return (m_ssrDummy && m_ssrDummy->valid()) ? m_ssrDummy->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    void Renderer::ensureIblDummyResources()
    {
        if (!m_iblDummyCube || m_iblDummyCubeCpu.ptr == 0)
            createBlackIblCube(*this, 2, m_iblDummyCube, m_iblDummyCubeCpuHeap, m_iblDummyCubeCpu);
        if (!m_iblDummyLut)
            m_iblDummyLut = std::make_unique<Texture2D>();
        if (!m_iblDummyLut->valid())
        {
            const float blackRg[2] = { 0.0f, 0.0f };
            m_iblDummyLut->createFromRgFloat(*this, blackRg, 1, 1, 8u);
            if (m_iblDummyLut->valid() && m_iblDummyLut->resource())
                m_iblDummyLut->resource()->SetName(L"DE.Ibl.DummyLut");
        }
    }

    void Renderer::applyIblSrvs()
    {
        ensureIblDummyResources();
        if (!m_sceneBuffers)
            return;
        const bool haveLast = m_iblIrrCpu.ptr != 0 && m_iblPrefCpu.ptr != 0 && m_iblLutCpu.ptr != 0;
        if (haveLast)
            m_sceneBuffers->setIblSrvs(m_device.Get(), m_iblIrrCpu, m_iblPrefCpu, m_iblLutCpu);
        else
            m_sceneBuffers->setIblSrvs(m_device.Get(), iblDummyCubeCpu(), iblDummyCubeCpu(), iblDummyLutCpu());
    }

    void Renderer::ensureSsrDummyResources()
    {
        if (!m_ssrDummy)
            m_ssrDummy = std::make_unique<Texture2D>();
        if (!m_ssrDummy->valid())
        {
            Image img;
            const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            if (!img.createFromRgba32f(black, 1, 1, 16u))
                return;
            m_ssrDummy->createFromImage(*this, img, Color::TextureUsage::Ibl);
            if (m_ssrDummy->valid() && m_ssrDummy->resource())
                m_ssrDummy->resource()->SetName(L"DE.Ssr.Dummy");
        }
    }

    void Renderer::setIblSrvs(D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu, D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu, D3D12_CPU_DESCRIPTOR_HANDLE brdfLutCpu)
    {
        m_iblIrrCpu  = irradianceCpu;
        m_iblPrefCpu = prefilterCpu;
        m_iblLutCpu  = brdfLutCpu;
        applyIblSrvs();
    }

    bool Renderer::hasGBuffer() const
    {
        return m_sceneBuffers && m_sceneBuffers->hasGBuffer();
    }

    void Renderer::setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setShadowSrv(m_device.Get(), shadowCpu);
    }

    void Renderer::setLightingAoSrv(D3D12_CPU_DESCRIPTOR_HANDLE aoCpu)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setLightingAoSrv(m_device.Get(), aoCpu);
    }

    void Renderer::setLightingSsrSrv(D3D12_CPU_DESCRIPTOR_HANDLE ssrCpu)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setLightingSsrSrv(m_device.Get(), ssrCpu);
    }

    void Renderer::setLightingAlbedoRaw(bool raw)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setLightingAlbedoRaw(m_device.Get(), raw);
    }

    void Renderer::setHeightSrv(D3D12_CPU_DESCRIPTOR_HANDLE heightCpu)
    {
        m_heightCpu = heightCpu;
        if (m_sceneBuffers)
            m_sceneBuffers->setHeightSrv(m_device.Get(), heightCpu);
    }

    void Renderer::bindGBuffer()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->hasGBuffer())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindGBuffer: G-buffer not allocated");
            return;
        }

        m_sceneBuffers->transitionAlbedo(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_sceneBuffers->transitionAttrib(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_sceneBuffers->transitionVelocity(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_sceneBuffers->transitionAo(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[4] = {
            m_sceneBuffers->albedoRtv(), m_sceneBuffers->attribRtv(), m_sceneBuffers->velocityRtv(), m_sceneBuffers->aoRtv()
        };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_commandList->OMSetRenderTargets(4, rtvs, FALSE, &dsv);
    }

    void Renderer::bindHdr(bool bindDepth)
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindHdr: no SceneBuffers");
            return;
        }

        if (!bindDepth && m_sceneBuffers->hasGBuffer())
        {
            m_sceneBuffers->transitionAlbedo(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_sceneBuffers->transitionAttrib(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_sceneBuffers->transitionVelocity(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_sceneBuffers->transitionAo(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneBuffers->hdrRtv();
        if (bindDepth)
        {
            transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        }
        else
        {
            transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
#if defined(_DEBUG)
            DE_ASSERT(m_depthState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            if (m_sceneBuffers->hasGBuffer())
            {
                DE_ASSERT(m_sceneBuffers->albedoState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                DE_ASSERT(m_sceneBuffers->attribState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                DE_ASSERT(m_sceneBuffers->velocityState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                DE_ASSERT(m_sceneBuffers->aoState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
#endif
        }
    }

    void Renderer::bindHdrDepthRead()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindHdrDepthRead: no SceneBuffers");
            return;
        }

        m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneBuffers->hdrRtv();
        D3D12_CPU_DESCRIPTOR_HANDLE       dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += m_dsvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    }

    void Renderer::bindPostHdr()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->post())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindPostHdr: no post target");
            return;
        }
        m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (m_sceneBuffers->hasGBuffer())
            m_sceneBuffers->transitionVelocity(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_sceneBuffers->transitionPost(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneBuffers->postRtv();
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    void Renderer::bindHdrColorTarget()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindHdrColorTarget: no HDR target");
            return;
        }
        m_sceneBuffers->transitionPost(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (m_sceneBuffers->hasGBuffer())
            m_sceneBuffers->transitionVelocity(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneBuffers->hdrRtv();
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    void Renderer::bindTaaTarget()
    {
        bindPostHdr();
        if (m_sceneBuffers)
            m_sceneBuffers->transitionHistory(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void Renderer::copyPostToHistory(ID3D12GraphicsCommandList* cmd)
    {
        if (!cmd || !m_sceneBuffers || !m_sceneBuffers->post() || !m_sceneBuffers->history())
            return;
        m_sceneBuffers->transitionPost(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_sceneBuffers->transitionHistory(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(m_sceneBuffers->history(), m_sceneBuffers->post());
        m_sceneBuffers->transitionPost(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_sceneBuffers->transitionHistory(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void Renderer::clearGBuffer()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->hasGBuffer())
            return;
        m_commandList->ClearRenderTargetView(m_sceneBuffers->albedoRtv(), SceneBuffers::kAlbedoClear, 0, nullptr);
        m_commandList->ClearRenderTargetView(m_sceneBuffers->attribRtv(), SceneBuffers::kAttribClear, 0, nullptr);
        m_commandList->ClearRenderTargetView(m_sceneBuffers->velocityRtv(), SceneBuffers::kVelocityClear, 0, nullptr);
        m_commandList->ClearRenderTargetView(m_sceneBuffers->aoRtv(), SceneBuffers::kAoClear, 0, nullptr);
    }

    void Renderer::clearHdr()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
            return;
        // Must match the D3D12_CLEAR_VALUE passed at HDR resource creation (warning #820).
        m_commandList->ClearRenderTargetView(m_sceneBuffers->hdrRtv(), m_sceneBuffers->hdrClear(), 0, nullptr);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::hdrRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->hdrRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::hdrSrvCpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->hdrSrvCpu();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::postHdrSrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->postSrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::velocitySrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->velocitySrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::historySrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->historySrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::albedoSrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->albedoSrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::attribSrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->attribSrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::aoSrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->aoSrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::lightingAoCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->lightingAoCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Renderer::lightingTableGpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->lightingTableGpu();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Renderer::heightTableGpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->heightTableGpu();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Renderer::aoTableGpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->aoTableGpu();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Renderer::iblTableGpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->iblTableGpu();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Renderer::ssrTableGpu() const
    {
        if (!m_sceneBuffers)
            return {};
        return m_sceneBuffers->ssrTableGpu();
    }

    ID3D12DescriptorHeap* Renderer::lightingHeap() const
    {
        return m_sceneBuffers ? m_sceneBuffers->lightingHeap() : nullptr;
    }

} // namespace Dark
