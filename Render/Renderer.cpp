#include "Render/Renderer.h"
#include "Render/DescriptorRingHeap.h"
#include "Render/EnhancedBarrierEmitter.h"
#include "Render/GpuIbl.h"
#include "Render/GraphTargets.h"
#include "Render/GpuResourceCache.h"
#include "Render/IblBake.h"
#include "Render/Profile.h"
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
#include <wchar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark
{

    static_assert(kProfileGpuSlots == Renderer::kFrameCount, "timestamp slots match the swap-chain frame count");

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

        void nameObject(ID3D12Object* obj, const wchar_t* name)
        {
            if (obj && name)
                obj->SetName(name);
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

    Renderer::Renderer(Window& window, bool vsync)
        : m_vsync(vsync)
    {
        m_valid = initD3D12(window);
    }

    Renderer::Renderer(RendererTestDevice)
        : m_testShell(true)
    {
    }

    void Renderer::setFrameIndexForTest(uint32_t frame)
    {
        m_frameIndex = frame;
    }

    Renderer::~Renderer()
    {
        if (m_testShell)
            return;
        waitForGpu();
        detachDebugMessageLog();
        profileTimersShutdown();
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
        if (m_texUpdateFence && m_texUpdateFenceValue > 0 && m_fenceEvent && m_texUpdateFence->GetCompletedValue() < m_texUpdateFenceValue)
        {
            m_texUpdateFence->SetEventOnCompletion(m_texUpdateFenceValue, m_fenceEvent);
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }
        m_deferredGpu.clear();
        if (m_gpuResources)
        {
            m_gpuResources->clear();
            m_gpuResources.reset();
        }
        // GPU is idle: release the scene targets, then every graph target and the D3D12MA allocator.
        m_sceneBuffers.reset();
        if (m_targets)
            m_targets->shutdown();
        if (frameStateTracker() == &m_stateTracker)
            setFrameStateTracker(nullptr);
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

        // Every barrier is an enhanced barrier (D13); the tracker holds the state of the back buffers
        // and graph targets from creation on (this thread only: the shadow worker sees none).
        m_stateTracker.clear();
        setFrameStateTracker(&m_stateTracker);

        configureDiagnostics();

        UINT dxgiFactoryFlags = 0;
        if (m_debugLayerEnabled)
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

        ComPtr<IDXGIFactory6> factory;
        if (!checkHr(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2"))
            return false;

        // Platform minimum (plan D5/D9/D13): hardware adapter at FL 12_1 with the startup gate. No WARP or
        // lower-tier fallback; a failed check is a message box, a log line and a non-zero exit.
        RhiDeviceResult rhi;
        if (!createRhiDevice(factory.Get(), rhi))
        {
            reportStartupGateFailure(rhi.gate);
            return false;
        }
        m_device = rhi.device;
        m_caps   = rhi.caps;
        nameObject(m_device.Get(), L"DE.Device");
        attachDebugMessageLog();

        {
            wchar_t power[8]{};
            if (GetEnvironmentVariableW(L"DE_STABLE_GPU_POWER", power, 8) > 0 && power[0] == L'1')
            {
                const HRESULT stable = m_device->SetStablePowerState(TRUE);
                if (FAILED(stable))
                    DE_LOG_WARN(LogCategory::Render, "SetStablePowerState failed (HRESULT 0x{:08X}). Windows Developer Mode is required.", static_cast<unsigned>(stable));
                else
                    DE_LOG_INFO(LogCategory::Render, "GPU clocks locked (DE_STABLE_GPU_POWER)");
            }
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (!checkHr(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue"))
            return false;
        nameObject(m_commandQueue.Get(), L"DE.GraphicsQueue");

        D3D12_COMMAND_QUEUE_DESC copyDesc{};
        copyDesc.Type  = D3D12_COMMAND_LIST_TYPE_COPY;
        copyDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (!checkHr(m_device->CreateCommandQueue(&copyDesc, IID_PPV_ARGS(&m_copyQueue)), "CreateCommandQueue (copy)"))
            return false;
        nameObject(m_copyQueue.Get(), L"DE.CopyQueue");

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
        nameObject(m_rtvHeap.Get(), L"DE.RtvHeap");
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!createRenderTargets())
            return false;

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (!checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])), "CreateCommandAllocator"))
                return false;
            wchar_t allocName[40];
            swprintf_s(allocName, L"DE.GraphicsAllocator%u", i);
            nameObject(m_commandAllocators[i].Get(), allocName);
        }

        // Graph-owned persistent targets (D3D12MA) and the depth buffer (write DSV, READ_ONLY_DEPTH DSV, SRV).
        m_targets = std::make_unique<GraphTargets>();
        if (!m_targets->init(m_device.Get(), m_width, m_height))
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: graph targets (D3D12MA) init failed");
            return false;
        }
        if (!createDepthResources())
            return false;

        // One shader-visible CBV/SRV/UAV heap for converted pipelines (Phase 6): the post passes bind
        // only it, so the renderer cannot run without it (262144 descriptors; D5's Tier 3 allows 1M).
        m_descriptorRing = std::make_unique<DescriptorRingHeap>();
        if (!m_descriptorRing->init(m_device.Get()))
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: descriptor ring init failed");
            m_descriptorRing.reset();
            return false;
        }

        if (!checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList"))
            return false;
        nameObject(m_commandList.Get(), L"DE.GraphicsCmd");
        // Start closed; beginFrame resets and opens it each frame.
        if (!checkHr(m_commandList->Close(), "CommandList Close (init)"))
            return false;

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (!checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_texUpdateCopyAlloc[i])), "CreateCommandAllocator (tex update copy)")
                || !checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_texUpdateDirectAlloc[i])), "CreateCommandAllocator (tex update direct)"))
                return false;
            if (!checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_texUpdateCopyAlloc[i].Get(), nullptr, IID_PPV_ARGS(&m_texUpdateCopyList[i])),
                         "CreateCommandList (tex update copy)"))
                return false;
            if (!checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_texUpdateDirectAlloc[i].Get(), nullptr, IID_PPV_ARGS(&m_texUpdateDirectList[i])),
                         "CreateCommandList (tex update direct)"))
            {
                m_texUpdateCopyList[i]->Close();
                return false;
            }
            // CreateCommandList returns a recording list. Close before any Reset.
            if (!checkHr(m_texUpdateCopyList[i]->Close(), "TexUpdate copy list Close (init)"))
            {
                m_texUpdateDirectList[i]->Close();
                return false;
            }
            if (!checkHr(m_texUpdateDirectList[i]->Close(), "TexUpdate direct list Close (init)"))
                return false;

            wchar_t listName[48];
            swprintf_s(listName, L"DE.TexUpdateCopyAlloc%u", i);
            nameObject(m_texUpdateCopyAlloc[i].Get(), listName);
            swprintf_s(listName, L"DE.TexUpdateCopyList%u", i);
            nameObject(m_texUpdateCopyList[i].Get(), listName);
            swprintf_s(listName, L"DE.TexUpdateDirectAlloc%u", i);
            nameObject(m_texUpdateDirectAlloc[i].Get(), listName);
            swprintf_s(listName, L"DE.TexUpdateDirectList%u", i);
            nameObject(m_texUpdateDirectList[i].Get(), listName);
        }

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (!checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_shadowAlloc[i])), "CreateCommandAllocator (shadow)"))
                return false;
            if (!checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_shadowAlloc[i].Get(), nullptr, IID_PPV_ARGS(&m_shadowList[i])),
                    "CreateCommandList (shadow)"))
                return false;
            if (!checkHr(m_shadowList[i]->Close(), "Shadow list Close (init)"))
                return false;

            wchar_t shadowName[48];
            swprintf_s(shadowName, L"DE.ShadowAllocator%u", i);
            nameObject(m_shadowAlloc[i].Get(), shadowName);
            swprintf_s(shadowName, L"DE.ShadowList%u", i);
            nameObject(m_shadowList[i].Get(), shadowName);
        }

        if (!checkHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence"))
            return false;
        nameObject(m_fence.Get(), L"DE.FrameFence");
        if (!checkHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)), "CreateFence (copy)"))
            return false;
        nameObject(m_copyFence.Get(), L"DE.CopyFence");
        if (!checkHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_texUpdateFence)), "CreateFence (tex update)"))
            return false;
        nameObject(m_texUpdateFence.Get(), L"DE.TexUpdateFence");
        m_texUpdateFenceValue = 0;
        // Match D3D12HelloFrameBuffering: fence starts at 0; the value we will
        // Signal after the first use of this back-buffer slot is 1.
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            m_fenceValues[i] = 0;
        }
        m_fenceValues[m_frameIndex] = 1;
        m_targets->retireAfter(m_fenceValues[m_frameIndex]);

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
        {
            DE_LOG_ERROR(LogCategory::Render, "CreateEvent for fence failed");
            return false;
        }

        updateViewport();

        m_gpuResources = std::make_unique<GpuResourceCache>(*this);
        if (!profileTimersInit(m_device.Get(), m_commandQueue.Get()))
            DE_LOG_WARN(LogCategory::Render, "GPU timestamp queries unavailable; profiler GPU times stay empty");
        profileStartup();
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
            wchar_t bbName[32];
            swprintf_s(bbName, L"DE.BackBuffer%u", i);
            nameObject(m_renderTargets[i].Get(), bbName);
            trackResource(m_renderTargets[i].Get(), D3D12_RESOURCE_STATE_PRESENT); // swap chain buffers start in COMMON
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
        if (!m_targets || !m_targets->valid() || m_width == 0 || m_height == 0)
            return false;

        Rg::TargetDesc desc;
        desc.size         = Rg::TargetSize::OutputFloor;
        desc.format       = static_cast<uint32_t>(DXGI_FORMAT_R32_TYPELESS);
        desc.srvFormat    = static_cast<uint32_t>(DXGI_FORMAT_R32_FLOAT);
        desc.targetFormat = static_cast<uint32_t>(DXGI_FORMAT_D32_FLOAT);
        desc.flags        = static_cast<uint32_t>(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        desc.initialState = static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        desc.srv          = true;
        desc.dsv          = true;
        desc.clear        = true;
        desc.depth        = depthClearValue();
        m_depthId         = m_targets->registerTexture("DE.Depth", desc);
        if (!m_depthId.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: depth target create failed");
            return false;
        }
        refreshDepthViews();
        DE_LOG_INFO(LogCategory::Render, "Renderer: depth D32_FLOAT clear=0 reverse-Z");
        return true;
    }

    void Renderer::refreshDepthViews()
    {
        // The depth target's state is in the frame tracker (from its creation in DEPTH_WRITE).
        m_depthSrvCpu = m_targets ? m_targets->srv(m_depthId) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    ID3D12Resource* Renderer::depthResource() const
    {
        return m_targets ? m_targets->resource(m_depthId) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::depthDsv() const
    {
        return m_targets ? m_targets->dsv(m_depthId) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::depthReadOnlyDsv() const
    {
        return m_targets ? m_targets->readOnlyDsv(m_depthId) : D3D12_CPU_DESCRIPTOR_HANDLE{};
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
        {
            logDeviceRemoved("resize");
            return false;
        }

        // The one GPU drain per resize (render-graph plan Phase 5): swapchain buffers and every
        // graph-registered target are recreated inside this window; owners rebuild views on use.
        waitForGpu();
        ++m_resizeDrains;
        broadcastFenceValueAfterWait(m_fenceValues, kFrameCount, m_frameIndex);

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            forgetResource(m_renderTargets[i].Get());
            m_renderTargets[i].Reset();
        }

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

        if (!createRenderTargets())
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: resize recreate back buffers failed");
            return false;
        }
        // Everything before the drain has completed: retired targets can go as soon as they are retired.
        m_targets->retireAfter(m_fenceValues[m_frameIndex] - 1);
        const bool targetsOk = m_targets->resizeAfterDrain(m_width, m_height);
        m_targets->collect(m_fence->GetCompletedValue());
        m_targets->retireAfter(m_fenceValues[m_frameIndex]);
        if (!targetsOk)
        {
            DE_LOG_ERROR(LogCategory::Render, "Renderer: resize recreate graph targets failed");
            return false;
        }
        refreshDepthViews();

        if (m_sceneBuffers)
        {
            if (!m_sceneBuffers->rebuildViews())
            {
                DE_LOG_ERROR(LogCategory::Render, "Renderer: SceneBuffers resize failed");
                m_sceneBuffers.reset();
                return false;
            }
            m_sceneBuffers->setLightingAlbedoRaw(m_debugState.showAlbedoRaw);
            setHeightSrv(m_heightCpu.ptr != 0 ? m_heightCpu : (m_fogHeightDummy && m_fogHeightDummy->valid() ? m_fogHeightDummy->cpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{}));
            applyIblSrvs();
            ensureSsrDummyResources();
        }

        updateViewport();
        DE_LOG_INFO(LogCategory::Render, "Renderer: resized to {}x{} (resize drains {})", m_width, m_height, m_resizeDrains);
        m_targets->logStats("resize");
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
        if (m_gpuResources)
            m_gpuResources->tickRetired();
        // Graph targets retired from here on are covered by this frame's fence signal.
        if (m_targets)
        {
            m_targets->retireAfter(m_fenceValues[m_frameIndex]);
            m_targets->collect(m_fence->GetCompletedValue());
        }
        if (m_descriptorRing)
            m_descriptorRing->retire(m_fence->GetCompletedValue());

        // m_frameIndex was advanced in moveToNextFrame(), which already waited until
        // this slot's prior GPU work finished. Safe to reset its allocator now.
        if (!checkHr(m_commandAllocators[m_frameIndex]->Reset(), "CommandAllocator Reset"))
            return false;
        if (!checkHr(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "CommandList Reset"))
            return false;

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        {
            // PRESENT -> RENDER_TARGET and depth back to DEPTH_WRITE, one Barrier() call.
            EnhancedBarrierList barriers(m_commandList.Get());
            barriers.transitionTo(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (ID3D12Resource* depth = depthResource())
                barriers.transitionTo(depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = depthDsv();

        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        m_commandList->ClearRenderTargetView(rtv, m_clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depthClearValue(), 0, 0, nullptr);

        // The slot's previous submission has been waited out in moveToNextFrame.
        profileTimersBeginFrame(m_frameIndex, m_commandList.Get());
        profileCpuBegin("Frame", ProfileColor::Frame);
        profileGpuBegin(m_commandList.Get(), "Frame", ProfileColor::Frame);
        m_profileFrame = true;
        return true;
    }

    void Renderer::closeProfileFrame()
    {
        if (!m_profileFrame)
            return;
        m_profileFrame = false;
        if (!m_commandList)
            return;
        profileGpuEnd(m_commandList.Get());
        profileCpuEnd();
    }

    void Renderer::setClearColor(float r, float g, float b, float a)
    {
        m_clearColor[0] = r;
        m_clearColor[1] = g;
        m_clearColor[2] = b;
        m_clearColor[3] = a;
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

    void Renderer::bindBackBuffer()
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = backBufferRtv();
        if (!m_commandList || rtv.ptr == 0)
            return;
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    void Renderer::transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!m_sceneBuffers)
            return;
        m_sceneBuffers->transitionHdr(cmd, after);
    }

    void Renderer::transitionDepth(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        ID3D12Resource* depth = depthResource();
        if (!cmd || !depth)
            return;
        recordTransitionTo(cmd, depth, after);
    }

    bool Renderer::endFrame()
    {
        if (!m_valid || !m_commandList || !m_commandQueue)
        {
            if (m_shadowListOpen && m_frameIndex < kFrameCount && m_shadowList[m_frameIndex])
                checkHr(m_shadowList[m_frameIndex]->Close(), "Shadow list Close (abandoned)");
            m_shadowListOpen     = false;
            m_shadowListRecorded = false;
            closeProfileFrame();
            DE_LOG_ERROR(LogCategory::Render, "Renderer::endFrame: device not initialized");
            return false;
        }

        if (m_shadowListOpen)
        {
            DE_ASSERT(!m_shadowListOpen);
            DE_LOG_ERROR(LogCategory::Render, "Renderer::endFrame: shadow list was left open");
            m_shadowListOpen     = false;
            m_shadowListRecorded = false;
            if (m_frameIndex < kFrameCount && m_shadowList[m_frameIndex])
                checkHr(m_shadowList[m_frameIndex]->Close(), "Shadow list Close (abandoned)");
        }

        // A capture copies this back buffer before Present; no extra work without a request.
        recordBackBufferCapture();

        // RENDER_TARGET -> PRESENT (before-state from the tracker)
        recordTransitionTo(m_commandList.Get(), m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT);
        closeProfileFrame();
        profileTimersResolve(m_commandList.Get());

        if (!checkHr(m_commandList->Close(), "CommandList Close"))
        {
            m_shadowListRecorded = false;
            return false;
        }

        // The shadow worker's list first.
        ID3D12CommandList* lists[2]{};
        UINT listCount = 0;
        if (m_shadowListRecorded)
        {
            if (m_frameIndex < kFrameCount && m_shadowList[m_frameIndex])
                lists[listCount++] = m_shadowList[m_frameIndex].Get();
            m_shadowListRecorded = false;
        }
        lists[listCount++] = m_commandList.Get();
        m_commandQueue->ExecuteCommandLists(listCount, lists);
        m_frameSubmitted = true;
        if (m_captureRecorded)
            writeBackBufferCapture();
        return true;
    }

    ID3D12GraphicsCommandList* Renderer::shadowCommandList() const
    {
        if (m_frameIndex >= kFrameCount)
            return nullptr;
        return m_shadowList[m_frameIndex].Get();
    }

    ID3D12CommandAllocator* Renderer::shadowCommandAllocator() const
    {
        if (m_frameIndex >= kFrameCount)
            return nullptr;
        return m_shadowAlloc[m_frameIndex].Get();
    }

    void Renderer::noteShadowListReset()
    {
        m_shadowListOpen     = true;
        m_shadowListRecorded = false;
    }

    void Renderer::adoptClosedShadowList()
    {
        m_shadowListOpen     = false;
        m_shadowListRecorded = true;
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
                logDeviceRemoved("Present");
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
        // The frame's descriptor tables are free once this signal completes.
        if (m_descriptorRing)
            m_descriptorRing->endFrame(currentFenceValue);

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
            EnhancedBarrierList barriers(list.Get());
            barriers.transition(dstA, Rg::ResourceKind::Buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            if (dstB)
                barriers.transition(dstB, Rg::ResourceKind::Buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
            barriers.record();
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

    bool Renderer::submitTextureCopies(ID3D12Resource* dst, ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* footprints, UINT count, bool* queued)
    {
        if (queued)
            *queued = false;
        if (!m_device || !m_commandQueue || !dst || !upload || !footprints || count == 0 || count > 32)
            return false;

        auto transition = [](ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            recordTransition(list, resource, before, after); // untracked upload target: legacy states translated
        };

        ComPtr<ID3D12CommandAllocator>    alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (!checkHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (texture copy)")
            || !checkHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (texture copy)"))
            return false;

        // Graphics queue, not COPY. COPY lists must stay in COMMON (#1334 rejects
        // COPY_DEST) and that upload did not leave texels a shader could sample.
        // DIRECT does not decay, so the COPY_DEST barrier is legal. dst starts in COMMON.
        transition(list.Get(), dst, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        for (UINT mip = 0; mip < count; ++mip)
        {
            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource        = dst;
            dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = mip;

            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource       = upload;
            srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint = footprints[mip];
            list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        }
        transition(list.Get(), dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (!checkHr(list->Close(), "Close texture copy list"))
            return false;
        ID3D12CommandList* lists[] = { list.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
        if (queued)
            *queued = true;
        deferRelease(alloc.Get());
        deferRelease(list.Get());
        return true;
    }

    LUID Renderer::adapterLuid() const
    {
        if (!m_device)
        {
            LUID zero{};
            return zero;
        }
        return m_device->GetAdapterLuid();
    }

    bool Renderer::signalTexUpdate(ID3D12CommandQueue* queue, const char* what)
    {
        if (!queue || !m_texUpdateFence)
            return false;
        const UINT64 value = m_texUpdateFenceValue + 1;
        if (!checkHr(queue->Signal(m_texUpdateFence.Get(), value), what))
            return false;
        m_texUpdateFenceValue = value;
        return true;
    }

    ID3D12GraphicsCommandList* Renderer::beginTexUpdateDirect()
    {
        if (!m_valid || !m_device || m_frameIndex >= kFrameCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "beginTexUpdateDirect: device not initialized");
            return nullptr;
        }
        ID3D12CommandAllocator*    alloc = m_texUpdateDirectAlloc[m_frameIndex].Get();
        ID3D12GraphicsCommandList* list  = m_texUpdateDirectList[m_frameIndex].Get();
        if (!alloc || !list)
        {
            DE_LOG_ERROR(LogCategory::Render, "beginTexUpdateDirect: list is not ready");
            return nullptr;
        }
        // Closed in init, or by the previous execute. On a later visit moveToNextFrame
        // has waited this slot, so the allocator is idle. Callers record once.
        if (!checkHr(alloc->Reset(), "TexUpdate direct allocator Reset"))
            return nullptr;
        if (!checkHr(list->Reset(alloc, nullptr), "TexUpdate direct list Reset"))
            return nullptr;
        return list;
    }

    bool Renderer::executeTexUpdateDirect()
    {
        if (!m_valid || !m_commandQueue || !m_texUpdateFence || m_frameIndex >= kFrameCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "executeTexUpdateDirect: device not initialized");
            return false;
        }
        ID3D12GraphicsCommandList* list = m_texUpdateDirectList[m_frameIndex].Get();
        if (!list)
            return false;
        if (!checkHr(list->Close(), "TexUpdate direct Close"))
            return false;
        ID3D12CommandList* lists[] = { list };
        m_commandQueue->ExecuteCommandLists(1, lists);
        return signalTexUpdate(m_commandQueue.Get(), "TexUpdate direct Signal");
    }

    bool Renderer::submitTextureUpdate(ID3D12Resource* dst, D3D12_RESOURCE_STATES before, ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        if (!m_valid || !m_device || !m_commandQueue || !m_texUpdateFence)
        {
            DE_LOG_ERROR(LogCategory::Render, "submitTextureUpdate: device not initialized");
            return false;
        }
        if (!dst || !upload || footprint.Footprint.Width == 0 || footprint.Footprint.Height == 0 || footprint.Footprint.RowPitch == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "submitTextureUpdate: invalid destination or footprint");
            return false;
        }
        if (before != D3D12_RESOURCE_STATE_COMMON && before != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        {
            DE_LOG_ERROR(LogCategory::Render, "submitTextureUpdate: state must be COMMON or PIXEL_SHADER_RESOURCE");
            return false;
        }
        if (m_frameIndex >= kFrameCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "submitTextureUpdate: bad frame slot");
            return false;
        }

        const uint32_t             slot        = m_frameIndex;
        ID3D12CommandAllocator*    directAlloc = m_texUpdateDirectAlloc[slot].Get();
        ID3D12GraphicsCommandList* directList  = m_texUpdateDirectList[slot].Get();
        if (!directAlloc || !directList)
        {
            DE_LOG_ERROR(LogCategory::Render, "submitTextureUpdate: upload lists are not ready");
            return false;
        }

        // Closed in init or by the previous visit. This slot's prior GPU work has
        // finished: moveToNextFrame waited the graphics fence before the slot was reused.
        if (!checkHr(directAlloc->Reset(), "TexUpdate direct allocator Reset"))
            return false;
        if (!checkHr(directList->Reset(directAlloc, nullptr), "TexUpdate direct list Reset"))
            return false;

        auto transition = [](ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
        {
            recordTransition(list, resource, stateBefore, stateAfter); // untracked upload target: legacy states translated
        };

        // Same graphics-queue rule as submitTextureCopies. before is COMMON or SRV.
        transition(directList, dst, before, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = dst;
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = upload;
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;
        directList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        transition(directList, dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (!checkHr(directList->Close(), "TexUpdate direct Close"))
            return false;
        ID3D12CommandList* lists[] = { directList };
        m_commandQueue->ExecuteCommandLists(1, lists);
        return signalTexUpdate(m_commandQueue.Get(), "TexUpdate direct Signal");
    }

    bool Renderer::hasSceneBuffers() const
    {
        return m_sceneBuffers && m_sceneBuffers->valid();
    }

    DXGI_FORMAT Renderer::sceneColorFormat() const
    {
        return hasSceneBuffers() ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    bool Renderer::createSceneTargets()
    {
        if (!m_valid || !m_device)
        {
            DE_LOG_ERROR(LogCategory::Render, "createSceneTargets: device not initialized");
            return false;
        }

        // Graph-registered: Renderer::resize keeps them at the output size, so a valid set is final.
        if (m_sceneBuffers && m_sceneBuffers->valid())
            return true;
        if (!m_targets || !m_targets->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "createSceneTargets: graph targets not initialized");
            return false;
        }

        // Unregister a broken set first (its targets retire behind the frame fence; its lighting table
        // may still be referenced by in-flight lists, so that edge case drains).
        if (m_sceneBuffers)
        {
            waitForGpu();
            m_sceneBuffers.reset();
        }
        auto buffers = std::make_unique<SceneBuffers>();
        if (!buffers->create(*m_targets, m_clearColor))
        {
            DE_LOG_ERROR(LogCategory::Render, "createSceneTargets: SceneBuffers create failed");
            m_sceneBuffers.reset();
            return false;
        }
        m_sceneBuffers = std::move(buffers);
        m_sceneBuffers->setLightingAlbedoRaw(m_debugState.showAlbedoRaw);
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
        ensureIblBrdfLut();
        DE_LOG_INFO(LogCategory::Render, "createSceneTargets: {}x{}", m_width, m_height);
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
            m_sceneBuffers->setIblSrvs(m_iblIrrCpu, m_iblPrefCpu, m_iblLutCpu);
        else
            m_sceneBuffers->setIblSrvs(iblDummyCubeCpu(), iblDummyCubeCpu(), iblDummyLutCpu());
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

    void Renderer::setLocalShadowSrvs(D3D12_CPU_DESCRIPTOR_HANDLE arrayCpu, D3D12_CPU_DESCRIPTOR_HANDLE recordsCpu)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setLocalShadowSrvs(arrayCpu, recordsCpu);
    }

    void Renderer::setLightingAlbedoRaw(bool raw)
    {
        if (m_sceneBuffers)
            m_sceneBuffers->setLightingAlbedoRaw(raw);
    }

    void Renderer::setHeightSrv(D3D12_CPU_DESCRIPTOR_HANDLE heightCpu)
    {
        m_heightCpu = heightCpu;
        if (m_sceneBuffers)
            m_sceneBuffers->setHeightSrv(heightCpu);
    }

    void Renderer::bindHdr(bool bindDepth)
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindHdr: no SceneBuffers");
            return;
        }

        if (!bindDepth)
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
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = depthDsv();
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        }
        else
        {
            transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
#if defined(_DEBUG)
            DE_ASSERT(trackedInState(depthResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
            {
                DE_ASSERT(trackedInState(m_sceneBuffers->albedo(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
                DE_ASSERT(trackedInState(m_sceneBuffers->attrib(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
                DE_ASSERT(trackedInState(m_sceneBuffers->velocity(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
                DE_ASSERT(trackedInState(m_sceneBuffers->ao(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
            }
#endif
        }
    }

    ID3D12Resource* Renderer::albedoResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->albedo() : nullptr;
    }

    ID3D12Resource* Renderer::attribResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->attrib() : nullptr;
    }

    ID3D12Resource* Renderer::backBufferResource() const
    {
        return m_frameIndex < kFrameCount ? m_renderTargets[m_frameIndex].Get() : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::backBufferRtv() const
    {
        if (!m_rtvHeap || m_frameIndex >= kFrameCount)
            return {};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        return rtv;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::postRtv() const
    {
        return m_sceneBuffers && m_sceneBuffers->post() ? m_sceneBuffers->postRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    ID3D12Resource* Renderer::velocityResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->velocity() : nullptr;
    }

    ID3D12Resource* Renderer::aoResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->ao() : nullptr;
    }

    ID3D12Resource* Renderer::surfaceTagResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->surfaceTag() : nullptr;
    }

    ID3D12Resource* Renderer::postResource() const
    {
        return (m_sceneBuffers && m_sceneBuffers->valid()) ? m_sceneBuffers->post() : nullptr;
    }

    void Renderer::bindHdrColorTarget()
    {
        if (!m_commandList || !m_sceneBuffers || !m_sceneBuffers->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "bindHdrColorTarget: no HDR target");
            return;
        }
        m_sceneBuffers->transitionPost(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_sceneBuffers->transitionVelocity(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transitionDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_sceneBuffers->transitionHdr(m_commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneBuffers->hdrRtv();
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    ID3D12Resource* Renderer::hdr() const
    {
        return m_sceneBuffers ? m_sceneBuffers->hdr() : nullptr;
    }

    const float* Renderer::hdrClearColor() const
    {
        static constexpr float kNone[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        return m_sceneBuffers ? m_sceneBuffers->hdrClear() : kNone;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::albedoRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->albedoRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::attribRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->attribRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::velocityRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->velocityRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::aoRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->aoRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::surfaceTagRtv() const
    {
        return m_sceneBuffers ? m_sceneBuffers->surfaceTagRtv() : D3D12_CPU_DESCRIPTOR_HANDLE{};
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

    D3D12_CPU_DESCRIPTOR_HANDLE Renderer::surfaceTagSrvCpu() const
    {
        return m_sceneBuffers ? m_sceneBuffers->surfaceTagSrvCpu() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    LightingViews Renderer::lightingViews() const
    {
        return m_sceneBuffers ? m_sceneBuffers->lightingViews() : LightingViews{};
    }

} // namespace Dark
