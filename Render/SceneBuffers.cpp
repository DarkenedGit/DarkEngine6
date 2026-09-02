#include "Render/SceneBuffers.h"
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
    } // namespace

    void SceneBuffers::reset()
    {
        m_hdr.Reset();
        m_albedo.Reset();
        m_attrib.Reset();
        m_rtvHeap.Reset();
        m_hdrSrvHeap.Reset();
        m_lightingHeap.Reset();
        m_hdrRtv      = {};
        m_albedoRtv   = {};
        m_attribRtv   = {};
        m_hdrSrvCpu     = {};
        m_lightingGpu   = {};
        m_lightingCpu   = {};
        m_hdrState    = D3D12_RESOURCE_STATE_COMMON;
        m_albedoState = D3D12_RESOURCE_STATE_COMMON;
        m_attribState = D3D12_RESOURCE_STATE_COMMON;
        m_width       = 0;
        m_height      = 0;
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

    bool SceneBuffers::createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, const float clearColor[4], const wchar_t* name, ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_STATES& state)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = format;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
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

    bool SceneBuffers::create(ID3D12Device* device, uint32_t width, uint32_t height, bool gbuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE savedShadow = m_shadowCpu;
        reset();
        m_shadowCpu = savedShadow;
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::create: invalid device or size");
            return false;
        }

        m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = gbuffer ? 3u : 1u;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "SceneBuffers CreateDescriptorHeap RTV"))
            return false;

        const float hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (!createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, hdrClear, L"DE.HdrColor", m_hdr, m_hdrState))
        {
            reset();
            return false;
        }
        m_hdrRtv = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvHdr, m_rtvIncr);
        device->CreateRenderTargetView(m_hdr.Get(), nullptr, m_hdrRtv);

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
            const float albedoClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            const float attribClear[4] = { 0.5f, 0.5f, 1.0f, 0.0f };
            if (!createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, albedoClear, L"DE.GBuffer.Albedo", m_albedo, m_albedoState)
                || !createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, attribClear, L"DE.GBuffer.Attrib", m_attrib, m_attribState))
            {
                reset();
                return false;
            }
            m_albedoRtv = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvAlbedo, m_rtvIncr);
            m_attribRtv = offsetHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), kRtvAttrib, m_rtvIncr);
            device->CreateRenderTargetView(m_albedo.Get(), nullptr, m_albedoRtv);
            device->CreateRenderTargetView(m_attrib.Get(), nullptr, m_attribRtv);

            D3D12_DESCRIPTOR_HEAP_DESC lightDesc{};
            lightDesc.NumDescriptors = kLightingCount;
            lightDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            lightDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (!checkHr(device->CreateDescriptorHeap(&lightDesc, IID_PPV_ARGS(&m_lightingHeap)), "SceneBuffers CreateDescriptorHeap lighting"))
            {
                reset();
                return false;
            }
            m_lightingCpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
            m_lightingGpu = m_lightingHeap->GetGPUDescriptorHandleForHeapStart();
            packLightingHeap(device, depthSrvCpu);
        }

        m_width  = width;
        m_height = height;
        DE_LOG_INFO(LogCategory::Render, "SceneBuffers: HDR {}x{}{} ", width, height, gbuffer ? " + G-buffer" : "");
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

    void SceneBuffers::packLightingHeap(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu)
    {
        if (!device || !m_lightingHeap || !m_albedo || !m_attrib)
            return;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC unorm{};
        unorm.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        unorm.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        unorm.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        unorm.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(m_albedo.Get(), &unorm, offsetHandle(cpu, kLightingAlbedo, m_srvIncr));
        device->CreateShaderResourceView(m_attrib.Get(), &unorm, offsetHandle(cpu, kLightingAttrib, m_srvIncr));

        if (depthSrvCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingDepth, m_srvIncr), depthSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_shadowCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), m_shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneBuffers::albedoSrvCpu() const
    {
        if (!m_lightingHeap)
            return {};
        return m_lightingCpu;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneBuffers::attribSrvCpu() const
    {
        if (!m_lightingHeap)
            return {};
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_lightingCpu;
        h.ptr += static_cast<SIZE_T>(kLightingAttrib) * m_srvIncr;
        return h;
    }

    void SceneBuffers::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        m_shadowCpu = shadowCpu;
        if (!device || !m_lightingHeap || shadowCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

} // namespace Dark
