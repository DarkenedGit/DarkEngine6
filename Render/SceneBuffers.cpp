#include "Render/SceneBuffers.h"
#include "Math/Color.h"
#include "Core/Log.h"

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

        D3D12_CPU_DESCRIPTOR_HANDLE offsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT index, UINT incr)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE h = start;
            h.ptr += static_cast<SIZE_T>(index) * incr;
            return h;
        }

        bool isTypeless(DXGI_FORMAT format)
        {
            return format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        }

        void createColorRtv(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT resourceFormat, DXGI_FORMAT viewFormat, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            // nullptr RTV desc is only legal when resourceFormat == viewFormat and neither is TYPELESS.
            if (resourceFormat == viewFormat && !isTypeless(resourceFormat) && !isTypeless(viewFormat))
            {
                device->CreateRenderTargetView(resource, nullptr, dest);
                return;
            }
            D3D12_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format        = viewFormat;
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(resource, &desc, dest);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC tex2dSrv(DXGI_FORMAT format)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                  = format;
            srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels     = 1;
            return srv;
        }
    } // namespace

    void SceneBuffers::reset()
    {
        m_hdr.Reset();
        m_albedo.Reset();
        m_attrib.Reset();
        m_velocity.Reset();
        m_post.Reset();
        m_history.Reset();
        m_ao.Reset();
        m_rtvHeap.Reset();
        m_hdrSrvHeap.Reset();
        m_velocitySrvHeap.Reset();
        m_postSrvHeap.Reset();
        m_historySrvHeap.Reset();
        m_albedoSrvHeap.Reset();
        m_attribSrvHeap.Reset();
        m_aoSrvHeap.Reset();
        m_lightingHeap.Reset();
        m_hdrRtv         = {};
        m_albedoRtv      = {};
        m_attribRtv      = {};
        m_velocityRtv    = {};
        m_postRtv        = {};
        m_historyRtv     = {};
        m_aoRtv          = {};
        m_hdrSrvCpu      = {};
        m_velocitySrvCpu = {};
        m_postSrvCpu     = {};
        m_historySrvCpu  = {};
        m_albedoSrvCpu   = {};
        m_attribSrvCpu   = {};
        m_aoSrvCpu       = {};
        m_lightingGpu    = {};
        m_heightGpu      = {};
        m_aoGpu          = {};
        m_shadowCpu      = {};
        m_heightCpu      = {};
        m_lightingAlbedoRaw = false;
        m_hdrState       = D3D12_RESOURCE_STATE_COMMON;
        m_albedoState    = D3D12_RESOURCE_STATE_COMMON;
        m_attribState    = D3D12_RESOURCE_STATE_COMMON;
        m_velocityState  = D3D12_RESOURCE_STATE_COMMON;
        m_postState      = D3D12_RESOURCE_STATE_COMMON;
        m_historyState   = D3D12_RESOURCE_STATE_COMMON;
        m_aoState        = D3D12_RESOURCE_STATE_COMMON;
        m_width       = 0;
        m_height      = 0;
        m_hdrClear[0] = 0.0f;
        m_hdrClear[1] = 0.0f;
        m_hdrClear[2] = 0.0f;
        m_hdrClear[3] = 1.0f;
    }

    void SceneBuffers::transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after)
    {
        if (!cmd || !res || state == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res;
        b.Transition.StateBefore = state;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        state = after;
    }

    bool SceneBuffers::createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT resourceFormat, DXGI_FORMAT viewFormat, const float clearColor[4],
                                         const wchar_t* name, ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_STATES& state)
    {
        if (!device || width == 0 || height == 0 || !clearColor)
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::createColorTarget: invalid args");
            return false;
        }
        if (resourceFormat == DXGI_FORMAT_UNKNOWN || viewFormat == DXGI_FORMAT_UNKNOWN || isTypeless(viewFormat))
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::createColorTarget: view format cannot be TYPELESS/UNKNOWN");
            return false;
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = resourceFormat;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format   = viewFormat;
        clear.Color[0] = clearColor[0];
        clear.Color[1] = clearColor[1];
        clear.Color[2] = clearColor[2];
        clear.Color[3] = clearColor[3];

        if (!checkHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&out)), "SceneBuffers CreateCommittedResource"))
            return false;
        if (name)
            out->SetName(name);
        state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return true;
    }

    bool SceneBuffers::create(ID3D12Device* device, uint32_t width, uint32_t height, bool gbuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu, const float hdrClear[4])
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE savedShadow = m_shadowCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedHeight = m_heightCpu;
        reset();
        m_shadowCpu = savedShadow;
        m_heightCpu = savedHeight;
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::create: invalid device or size");
            return false;
        }
        if (hdrClear)
        {
            // UI / swapchain clears are sRGB; tonemap applies IEC OETF. Store linear so kVoid stays #081018 on screen.
            m_hdrClear[0] = Color::srgbToLinear(hdrClear[0]);
            m_hdrClear[1] = Color::srgbToLinear(hdrClear[1]);
            m_hdrClear[2] = Color::srgbToLinear(hdrClear[2]);
            m_hdrClear[3] = hdrClear[3];
        }

        m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = gbuffer ? kRtvCountGBuffer : 1u;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "SceneBuffers CreateDescriptorHeap RTV"))
            return false;

        if (!createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, m_hdrClear, L"DE.HdrColor", m_hdr, m_hdrState))
        {
            reset();
            return false;
        }
        m_hdrRtv = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvHdr, m_rtvIncr);
        createColorRtv(device, m_hdr.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, m_hdrRtv);

        D3D12_DESCRIPTOR_HEAP_DESC hdrSrvDesc{};
        hdrSrvDesc.NumDescriptors = 1;
        hdrSrvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hdrSrvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(device->CreateDescriptorHeap(&hdrSrvDesc, IID_PPV_ARGS(&m_hdrSrvHeap)), "SceneBuffers CreateDescriptorHeap HDR SRV"))
        {
            reset();
            return false;
        }
        m_hdrSrvCpu = m_hdrSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrv{};
        hdrSrv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        hdrSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        hdrSrv.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(m_hdr.Get(), &hdrSrv, m_hdrSrvCpu);

        if (gbuffer)
        {
            if (!createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kAlbedoClear, L"DE.GBuffer.Albedo", m_albedo, m_albedoState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, kAttribClear, L"DE.GBuffer.Attrib", m_attrib, m_attribState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, kVelocityClear, L"DE.GBuffer.Velocity", m_velocity, m_velocityState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, kPostClear, L"DE.HdrPost", m_post, m_postState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, kPostClear, L"DE.TaaHistory", m_history, m_historyState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, kAoClear, L"DE.GBuffer.Ao", m_ao, m_aoState))
            {
                reset();
                return false;
            }
            m_albedoRtv   = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvAlbedo, m_rtvIncr);
            m_attribRtv   = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvAttrib, m_rtvIncr);
            m_velocityRtv = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvVelocity, m_rtvIncr);
            m_postRtv     = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvPost, m_rtvIncr);
            m_historyRtv  = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvHistory, m_rtvIncr);
            m_aoRtv       = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvAo, m_rtvIncr);
            createColorRtv(device, m_albedo.Get(), DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_albedoRtv);
            createColorRtv(device, m_attrib.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, m_attribRtv);
            createColorRtv(device, m_velocity.Get(), DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, m_velocityRtv);
            createColorRtv(device, m_post.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, m_postRtv);
            createColorRtv(device, m_history.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, m_historyRtv);
            createColorRtv(device, m_ao.Get(), DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, m_aoRtv);

            D3D12_DESCRIPTOR_HEAP_DESC velSrvDesc{};
            velSrvDesc.NumDescriptors = 1;
            velSrvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            velSrvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (!checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_velocitySrvHeap)), "SceneBuffers CreateDescriptorHeap velocity SRV")
                || !checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_postSrvHeap)), "SceneBuffers CreateDescriptorHeap post SRV")
                || !checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_historySrvHeap)), "SceneBuffers CreateDescriptorHeap history SRV")
                || !checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_albedoSrvHeap)), "SceneBuffers CreateDescriptorHeap albedo SRV")
                || !checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_attribSrvHeap)), "SceneBuffers CreateDescriptorHeap attrib SRV")
                || !checkHr(device->CreateDescriptorHeap(&velSrvDesc, IID_PPV_ARGS(&m_aoSrvHeap)), "SceneBuffers CreateDescriptorHeap AO SRV"))
            {
                reset();
                return false;
            }
            m_velocitySrvCpu = m_velocitySrvHeap->GetCPUDescriptorHandleForHeapStart();
            m_postSrvCpu     = m_postSrvHeap->GetCPUDescriptorHandleForHeapStart();
            m_historySrvCpu  = m_historySrvHeap->GetCPUDescriptorHandleForHeapStart();
            m_albedoSrvCpu   = m_albedoSrvHeap->GetCPUDescriptorHandleForHeapStart();
            m_attribSrvCpu   = m_attribSrvHeap->GetCPUDescriptorHandleForHeapStart();
            m_aoSrvCpu       = m_aoSrvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_SHADER_RESOURCE_VIEW_DESC velSrv{};
            velSrv.Format                  = DXGI_FORMAT_R16G16_FLOAT;
            velSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            velSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            velSrv.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(m_velocity.Get(), &velSrv, m_velocitySrvCpu);
            D3D12_SHADER_RESOURCE_VIEW_DESC postSrv{};
            postSrv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
            postSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            postSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            postSrv.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(m_post.Get(), &postSrv, m_postSrvCpu);
            device->CreateShaderResourceView(m_history.Get(), &postSrv, m_historySrvCpu);
            D3D12_SHADER_RESOURCE_VIEW_DESC unorm = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM);
            device->CreateShaderResourceView(m_albedo.Get(), &unorm, m_albedoSrvCpu);
            device->CreateShaderResourceView(m_attrib.Get(), &unorm, m_attribSrvCpu);
            const D3D12_SHADER_RESOURCE_VIEW_DESC aoUnorm = tex2dSrv(DXGI_FORMAT_R8_UNORM);
            device->CreateShaderResourceView(m_ao.Get(), &aoUnorm, m_aoSrvCpu);

            D3D12_DESCRIPTOR_HEAP_DESC lightDesc{};
            lightDesc.NumDescriptors = kLightingCount;
            lightDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            lightDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (!checkHr(device->CreateDescriptorHeap(&lightDesc, IID_PPV_ARGS(&m_lightingHeap)), "SceneBuffers CreateDescriptorHeap lighting"))
            {
                reset();
                return false;
            }
            m_lightingGpu = m_lightingHeap->GetGPUDescriptorHandleForHeapStart();
            m_heightGpu   = m_lightingGpu;
            m_heightGpu.ptr += static_cast<SIZE_T>(kLightingHeight) * m_srvIncr;
            m_aoGpu = m_lightingGpu;
            m_aoGpu.ptr += static_cast<SIZE_T>(kLightingAo) * m_srvIncr;
            packLightingHeap(device, depthSrvCpu);
        }

        m_width  = width;
        m_height = height;
        DE_LOG_INFO(LogCategory::Render, "SceneBuffers: HDR {}x{}{}", width, height, gbuffer ? " + G-buffer + velocity + TAA history" : "");
        return true;
    }

    void SceneBuffers::transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_hdr.Get(), m_hdrState, after);
    }

    void SceneBuffers::transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_albedo.Get(), m_albedoState, after);
    }

    void SceneBuffers::transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_attrib.Get(), m_attribState, after);
    }

    void SceneBuffers::transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_velocity.Get(), m_velocityState, after);
    }

    void SceneBuffers::transitionPost(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_post.Get(), m_postState, after);
    }

    void SceneBuffers::transitionHistory(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_history.Get(), m_historyState, after);
    }

    void SceneBuffers::transitionAo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_ao.Get(), m_aoState, after);
    }

    void SceneBuffers::packLightingHeap(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu)
    {
        if (!device || !m_lightingHeap || !m_albedo || !m_attrib || !m_ao)
            return;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();

        const D3D12_SHADER_RESOURCE_VIEW_DESC attribUnorm = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM);
        applyLightingAlbedoView(device);
        device->CreateShaderResourceView(m_attrib.Get(), &attribUnorm, offsetHandle(cpu, kLightingAttrib, m_srvIncr));

        if (depthSrvCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingDepth, m_srvIncr), depthSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_shadowCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), m_shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_heightCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingHeight, m_srvIncr), m_heightCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_aoSrvCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingAo, m_srvIncr), m_aoSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        m_shadowCpu = shadowCpu;
        if (!device || !m_lightingHeap || shadowCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::applyLightingAlbedoView(ID3D12Device* device)
    {
        if (!device || !m_lightingHeap || !m_albedo)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE dest = offsetHandle(m_lightingHeap->GetCPUDescriptorHandleForHeapStart(), kLightingAlbedo, m_srvIncr);
        if (m_lightingAlbedoRaw)
        {
            if (m_albedoSrvCpu.ptr != 0)
                device->CopyDescriptorsSimple(1, dest, m_albedoSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return;
        }
        const D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrgb = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        device->CreateShaderResourceView(m_albedo.Get(), &albedoSrgb, dest);
    }

    void SceneBuffers::setLightingAlbedoRaw(ID3D12Device* device, bool raw)
    {
        m_lightingAlbedoRaw = raw;
        applyLightingAlbedoView(device);
    }

    void SceneBuffers::setHeightSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE heightCpu)
    {
        m_heightCpu = heightCpu;
        if (!device || !m_lightingHeap || heightCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingHeight, m_srvIncr), heightCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_heightGpu = m_lightingGpu;
        m_heightGpu.ptr += static_cast<SIZE_T>(kLightingHeight) * m_srvIncr;
    }

} // namespace Dark
