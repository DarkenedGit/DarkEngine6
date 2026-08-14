#include "Render/Renderer.h"
#include "Core/Window.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <stdexcept>
#include <string>

namespace Dark
{

    void ThrowIfFailed(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            throw std::runtime_error(what);
        }
    }

#if defined(_DEBUG)
    void EnableDebugLayer()
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            DE_LOG_INFO("D3D12 debug layer enabled");
        }
    }
#endif

    Renderer::Renderer(Window& window)
    {
        initD3D12(window);
    }

    Renderer::~Renderer()
    {
        waitForGpu();
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
    }

    void Renderer::initD3D12(Window& window)
    {
        m_width  = window.width();
        m_height = window.height();

        HWND hwnd = static_cast<HWND>(window.nativeHandle());
        if (!hwnd)
        {
            throw std::runtime_error("Renderer: window has no HWND");
        }

#if defined(_DEBUG)
        EnableDebugLayer();
#endif

        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

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
            ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
            ThrowIfFailed(warp.As(&adapter), "WARP As IDXGIAdapter1");
            DE_LOG_WARN("Renderer: using WARP software adapter");
        }

        ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice");

        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            char name[128]{};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            DE_LOG_INFO("D3D12 device: {}", name);
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue");

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width       = m_width;
        scDesc.Height      = m_height;
        scDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc  = { 1, 0 };
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = kFrameCount;
        scDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.Flags       = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain1), "CreateSwapChainForHwnd");

        // Alt-Enter handled by the app if desired; disable DXGI's default fullscreen toggle.
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        ThrowIfFailed(swapChain1.As(&m_swapChain), "QueryInterface IDXGISwapChain3");
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // RTV heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = kFrameCount;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap RTV");
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])), "SwapChain GetBuffer");
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])), "CreateCommandAllocator");
        }

        // DSV heap + depth buffer
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap DSV");
        createDepthResources();

        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList");
        // Start closed; beginFrame resets and opens it each frame.
        ThrowIfFailed(m_commandList->Close(), "CommandList Close (init)");

        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");
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
            throw std::runtime_error("CreateEvent for fence failed");
        }

        m_viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
        m_scissor  = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };

        DE_LOG_INFO("Renderer: D3D12 ready ({}x{}, {} buffers)", m_width, m_height, kFrameCount);
    }

    void Renderer::createDepthResources()
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width            = m_width;
        depthDesc.Height           = m_height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels        = 1;
        depthDesc.Format           = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc       = { 1, 0 };
        depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear{};
        clear.Format               = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth   = 1.0f;
        clear.DepthStencil.Stencil = 0;

        ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_depthStencil)),
                      "CreateCommittedResource depth");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
        m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    void Renderer::beginFrame()
    {
        m_stats = {};

        if (!m_device || !m_commandList || !m_fence)
        {
            throw std::runtime_error("Renderer::beginFrame: device not initialized");
        }

        // m_frameIndex was advanced in moveToNextFrame(), which already waited until
        // this slot's prior GPU work finished. Safe to reset its allocator now.
        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset(), "CommandAllocator Reset");
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "CommandList Reset");

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissor);

        // PRESENT -> RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        const float clearColor[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
        m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    void Renderer::endFrame()
    {
        // RENDER_TARGET -> PRESENT
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(m_commandList->Close(), "CommandList Close");

        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
    }

    void Renderer::present()
    {
        ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
        moveToNextFrame();
    }

    void Renderer::moveToNextFrame()
    {
        // Signal a unique, monotonically increasing fence value for the work just
        // submitted with this back-buffer / allocator slot.
        const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue), "Queue Signal");

        // Advance to the swap-chain's next buffer.
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Block only if the GPU has not finished the last submission that used
        // this slot's command allocator (cannot Reset it until then).
        if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "SetEventOnCompletion (next frame)");
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        // Next signal for this slot must be strictly greater than any prior signal.
        m_fenceValues[m_frameIndex] = currentFenceValue + 1;
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

} // namespace Dark
