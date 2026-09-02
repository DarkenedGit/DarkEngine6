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
    } // namespace

    void SceneBuffers::reset()
    {
        m_hdr.Reset();
        m_rtvHeap.Reset();
        m_hdrSrvHeap.Reset();
        m_hdrRtv    = {};
        m_hdrSrvCpu = {};
        m_hdrState  = D3D12_RESOURCE_STATE_COMMON;
        m_width     = 0;
        m_height    = 0;
    }

    bool SceneBuffers::createHdr(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        reset();
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::createHdr: invalid device or size");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "SceneBuffers CreateDescriptorHeap RTV"))
            return false;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clear.Color[0] = 0.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 1.0f;

        if (!checkHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&m_hdr)), "SceneBuffers CreateCommittedResource HDR"))
        {
            reset();
            return false;
        }
        m_hdr->SetName(L"DE.HdrColor");
        m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        m_hdrRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(m_hdr.Get(), nullptr, m_hdrRtv);

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!checkHr(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_hdrSrvHeap)), "SceneBuffers CreateDescriptorHeap HDR SRV"))
        {
            reset();
            return false;
        }
        m_hdrSrvCpu = m_hdrSrvHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;
        device->CreateShaderResourceView(m_hdr.Get(), &srvDesc, m_hdrSrvCpu);

        m_width  = width;
        m_height = height;
        DE_LOG_INFO(LogCategory::Render, "SceneBuffers: HDR {}x{} R16G16B16A16_FLOAT", width, height);
        return true;
    }

    void SceneBuffers::transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!cmd || !m_hdr || m_hdrState == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_hdr.Get();
        b.Transition.StateBefore = m_hdrState;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        m_hdrState = after;
    }

} // namespace Dark
