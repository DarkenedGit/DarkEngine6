#include "Render/GtaoPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cstring>
#include <d3dcompiler.h>

namespace Dark
{

    namespace
    {
        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }

        void fillFullscreenPso(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso, ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps)
        {
            pso                                              = {};
            pso.pRootSignature                               = rs;
            pso.VS                                           = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS                                           = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.SampleMask                                   = UINT_MAX;
            pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pso.RasterizerState.FillMode                     = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode                     = D3D12_CULL_MODE_NONE;
            pso.RasterizerState.DepthClipEnable              = TRUE;
            pso.DepthStencilState.DepthEnable                = FALSE;
            pso.DepthStencilState.StencilEnable              = FALSE;
            pso.InputLayout                                  = { nullptr, 0 };
            pso.PrimitiveTopologyType                        = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets                             = 1;
            pso.RTVFormats[0]                                = DXGI_FORMAT_R8_UNORM;
            pso.DSVFormat                                    = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc                                   = { 1, 0 };
        }
    } // namespace

    bool GtaoPipeline::isValid() const
    {
        return m_psoGtao && m_psoUpsample && m_psoCompose && m_aoHalf.res && m_aoFull.res && m_aoHistory[0].res && m_aoHistory[1].res && m_aoCompose.res
            && m_rtvHeap && m_srvHeap && m_cpuSrvHeap && m_cbUpload && m_cbMapped;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::composeSrvCpu() const
    {
        return isValid() ? cpuSrv(kCpuSrvCompose) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::aoFullSrvCpu() const
    {
        return isValid() ? cpuSrv(kCpuSrvFull) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::rtvCpu(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_rtvIncr;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::cpuSrv(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuSrvStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GtaoPipeline::passSrvGpu(UINT frame, UINT pass) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu;
        h.ptr += static_cast<SIZE_T>((frame * kPassCount + pass) * kSrvPerPass) * m_srvIncr;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::passSrvCpu(UINT frame, UINT pass) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvCpu;
        h.ptr += static_cast<SIZE_T>((frame * kPassCount + pass) * kSrvPerPass) * m_srvIncr;
        return h;
    }

    void GtaoPipeline::copySrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src) const
    {
        if (!device || dst.ptr == 0 || src.ptr == 0)
            return;
        device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void GtaoPipeline::resetTargets()
    {
        m_aoHalf.res.Reset();
        m_aoFull.res.Reset();
        m_aoHistory[0].res.Reset();
        m_aoHistory[1].res.Reset();
        m_aoCompose.res.Reset();
        m_aoHalf.state        = D3D12_RESOURCE_STATE_COMMON;
        m_aoFull.state        = D3D12_RESOURCE_STATE_COMMON;
        m_aoHistory[0].state  = D3D12_RESOURCE_STATE_COMMON;
        m_aoHistory[1].state  = D3D12_RESOURCE_STATE_COMMON;
        m_aoCompose.state     = D3D12_RESOURCE_STATE_COMMON;
        m_rtvHeap.Reset();
        m_srvHeap.Reset();
        m_cpuSrvHeap.Reset();
        m_rtvStart     = {};
        m_srvCpu       = {};
        m_srvGpu       = {};
        m_cpuSrvStart  = {};
        m_width        = 0;
        m_height       = 0;
        m_halfW        = 0;
        m_halfH        = 0;
        m_historyIndex = 0;
        m_needReset    = true;
    }

    void GtaoPipeline::resetAll()
    {
        resetTargets();
        m_psoGtao.Reset();
        m_psoUpsample.Reset();
        m_psoCompose.Reset();
        m_rootSignature.Reset();
        if (m_cbUpload && m_cbMapped)
            m_cbUpload->Unmap(0, nullptr);
        m_cbUpload.Reset();
        m_cbMapped = nullptr;
        m_cbGpu    = 0;
    }

    void GtaoPipeline::transition(ID3D12GraphicsCommandList* cmd, Target& t, D3D12_RESOURCE_STATES after) const
    {
        if (!cmd || !t.res || t.state == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = t.res.Get();
        b.Transition.StateBefore = t.state;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        t.state = after;
    }

    bool GtaoPipeline::createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, const wchar_t* name, Target& out)
    {
        out.res.Reset();
        out.state = D3D12_RESOURCE_STATE_COMMON;
        if (!device || width == 0 || height == 0)
            return false;

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
        clear.Format   = format;
        clear.Color[0] = 1.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 0.0f;

        if (FailedHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&out.res)),
                     "CreateCommittedResource GTAO target"))
            return false;
        if (name)
            out.res->SetName(name);
        out.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return true;
    }

    bool GtaoPipeline::createPsos(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_psoGtao.Reset();
        m_psoUpsample.Reset();
        m_psoCompose.Reset();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 4;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE velRange{};
        velRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        velRange.NumDescriptors                    = 1;
        velRange.BaseShaderRegister                = 4;
        velRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootCbv].Descriptor.ShaderRegister = 0;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        params[kRootVelocity].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootVelocity].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootVelocity].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootVelocity].DescriptorTable.pDescriptorRanges   = &velRange;

        D3D12_STATIC_SAMPLER_DESC samps[2]{};
        samps[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[0].ShaderRegister   = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        samps[1].Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samps[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[1].ShaderRegister   = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 3;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 2;
        rsDesc.pStaticSamplers   = samps;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (GTAO)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "GTAO RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (GTAO)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> psGtao;
        ComPtr<ID3DBlob> psUp;
        ComPtr<ID3DBlob> psComp;
        if (!compileShaderFromContent("shaders/Gtao.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSGtao", "ps_5_0", psGtao)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSUpsampleTemporal", "ps_5_0", psUp)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSCompose", "ps_5_0", psComp))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psGtao.Get());
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoGtao)), "CreateGraphicsPipelineState (GTAO produce)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psUp.Get());
        pso.NumRenderTargets                             = 2;
        pso.RTVFormats[0]                                = DXGI_FORMAT_R8_UNORM;
        pso.RTVFormats[1]                                = DXGI_FORMAT_R16_FLOAT;
        pso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoUpsample)), "CreateGraphicsPipelineState (GTAO upsample)"))
        {
            m_psoGtao.Reset();
            m_rootSignature.Reset();
            return false;
        }

        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psComp.Get());
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoCompose)), "CreateGraphicsPipelineState (GTAO compose)"))
        {
            m_psoUpsample.Reset();
            m_psoGtao.Reset();
            m_rootSignature.Reset();
            return false;
        }
        return true;
    }

    bool GtaoPipeline::createCbv(ID3D12Device* device)
    {
        if (m_cbUpload && m_cbMapped)
            return true;

        m_cbUpload.Reset();
        m_cbMapped = nullptr;
        m_cbGpu    = 0;

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cbDesc{};
        cbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width            = static_cast<UINT64>(kCbBytes) * kFrameCount;
        cbDesc.Height           = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels        = 1;
        cbDesc.SampleDesc       = { 1, 0 };
        cbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&m_cbUpload)),
                     "CreateCommittedResource GTAO CBV"))
            return false;
        if (FailedHr(m_cbUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped)), "Map GTAO CBV"))
        {
            m_cbUpload.Reset();
            m_cbMapped = nullptr;
            return false;
        }
        m_cbGpu = m_cbUpload->GetGPUVirtualAddress();
        std::memset(m_cbMapped, 0, static_cast<size_t>(kCbBytes) * kFrameCount);
        return true;
    }

    bool GtaoPipeline::createTargets(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        resetTargets();
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "GtaoPipeline::createTargets: invalid device or size");
            return false;
        }

        m_halfW   = (width + 1u) / 2u;
        m_halfH   = (height + 1u) / 2u;
        m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = kRtvCount;
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap GTAO RTV"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kFrameCount * kPassCount * kSrvPerPass;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap GTAO SRV"))
        {
            m_rtvHeap.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC cpuDesc{};
        cpuDesc.NumDescriptors = kCpuSrvCount;
        cpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap GTAO CPU SRV"))
        {
            m_rtvHeap.Reset();
            m_srvHeap.Reset();
            return false;
        }

        m_rtvStart    = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvCpu      = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpu      = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_cpuSrvStart = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();

        if (!createColorTarget(device, m_halfW, m_halfH, DXGI_FORMAT_R8_UNORM, L"DE.Gtao.AoHalf", m_aoHalf)
            || !createColorTarget(device, width, height, DXGI_FORMAT_R8_UNORM, L"DE.Gtao.AoFull", m_aoFull)
            || !createColorTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, L"DE.Gtao.AoHistory0", m_aoHistory[0])
            || !createColorTarget(device, width, height, DXGI_FORMAT_R16_FLOAT, L"DE.Gtao.AoHistory1", m_aoHistory[1])
            || !createColorTarget(device, width, height, DXGI_FORMAT_R8_UNORM, L"DE.Gtao.AoCompose", m_aoCompose))
        {
            resetTargets();
            return false;
        }

        device->CreateRenderTargetView(m_aoHalf.res.Get(), nullptr, rtvCpu(kRtvHalf));
        device->CreateRenderTargetView(m_aoFull.res.Get(), nullptr, rtvCpu(kRtvFull));
        device->CreateRenderTargetView(m_aoHistory[0].res.Get(), nullptr, rtvCpu(kRtvHistory0));
        device->CreateRenderTargetView(m_aoHistory[1].res.Get(), nullptr, rtvCpu(kRtvHistory1));
        device->CreateRenderTargetView(m_aoCompose.res.Get(), nullptr, rtvCpu(kRtvCompose));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvR8{};
        srvR8.Format                  = DXGI_FORMAT_R8_UNORM;
        srvR8.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvR8.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvR8.Texture2D.MipLevels     = 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvR16 = srvR8;
        srvR16.Format                          = DXGI_FORMAT_R16_FLOAT;
        device->CreateShaderResourceView(m_aoHalf.res.Get(), &srvR8, cpuSrv(kCpuSrvHalf));
        device->CreateShaderResourceView(m_aoFull.res.Get(), &srvR8, cpuSrv(kCpuSrvFull));
        device->CreateShaderResourceView(m_aoHistory[0].res.Get(), &srvR16, cpuSrv(kCpuSrvHist0));
        device->CreateShaderResourceView(m_aoHistory[1].res.Get(), &srvR16, cpuSrv(kCpuSrvHist1));
        device->CreateShaderResourceView(m_aoCompose.res.Get(), &srvR8, cpuSrv(kCpuSrvCompose));

        m_width        = width;
        m_height       = height;
        m_historyIndex = 0;
        m_needReset    = true;
        return true;
    }

    bool GtaoPipeline::create(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GtaoPipeline::create: null device");
            resetAll();
            return false;
        }
        if (width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "GtaoPipeline::create: invalid size {}x{}", width, height);
            resetTargets();
            return false;
        }
        if (!m_psoGtao && !createPsos(device))
        {
            resetAll();
            return false;
        }
        if (!createCbv(device))
        {
            resetAll();
            return false;
        }
        if (!createTargets(device, width, height))
            return false;

        DE_LOG_INFO(LogCategory::Render, "GtaoPipeline: ready {}x{} (half {}x{})", width, height, m_halfW, m_halfH);
        m_loggedSkip = false;
        return true;
    }

    bool GtaoPipeline::resize(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (isValid() && m_width == width && m_height == height)
            return true;
        return create(device, width, height);
    }

    void GtaoPipeline::fillParams(GtaoGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings,
                                  bool resetHistory, uint32_t frameIndex) const
    {
        out = {};
        out.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(m_halfW, 1u));
        out.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(m_halfH, 1u));
        out.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_width, 1u));
        out.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_height, 1u));
        copyMatrix(out.invProj, camera.GetProj().Inverse());
        copyMatrix(out.view, camera.GetView());
        copyMatrix(out.reprojection, camera.GetViewProj().Inverse() * prevViewProj);
        out.radius      = settings.radius;
        out.power       = settings.power;
        out.intensity   = settings.intensity;
        out.thickness   = kThickness;
        out.nearZ       = camera.GetNearZ();
        out.farZ        = camera.GetFarZ();
        out.reset       = (resetHistory || m_needReset) ? 1.0f : 0.0f;
        out._pad        = static_cast<float>(frameIndex & 7u);
    }

    void GtaoPipeline::drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
                                      uint32_t w, uint32_t h, D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const
    {
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(nRtv, rtvs, FALSE, nullptr);
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, tableGpu);
        D3D12_GPU_DESCRIPTOR_HANDLE velGpu = tableGpu;
        velGpu.ptr += static_cast<SIZE_T>(4) * m_srvIncr;
        cmd->SetGraphicsRootDescriptorTable(kRootVelocity, velGpu);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void GtaoPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                            const GtaoSettings& settings, bool resetHistory)
    {
        if (!settings.enabled)
            return;
        if (!cmd || !isValid())
        {
            if (!isValid() && !m_loggedSkip)
            {
                DE_LOG_ERROR(LogCategory::Render, "GtaoPipeline::draw: not valid — skipping");
                m_loggedSkip = true;
            }
            return;
        }
        if (!renderer.hasGBuffer())
            return;

        ID3D12Device* device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE depth  = renderer.depthSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer.attribSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE vel    = renderer.velocitySrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE ao     = renderer.aoSrvCpu();
        if (!device || depth.ptr == 0 || attrib.ptr == 0 || vel.ptr == 0 || ao.ptr == 0)
            return;

        const UINT frame = renderer.frameIndex() % kFrameCount;
        GtaoGpuParams params{};
        fillParams(params, camera, prevViewProj, settings, resetHistory, renderer.frameIndex());
        std::memcpy(m_cbMapped + static_cast<size_t>(frame) * kCbBytes, &params, sizeof(params));
        const D3D12_GPU_VIRTUAL_ADDRESS cbVa = m_cbGpu + static_cast<UINT64>(frame) * kCbBytes;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, cbVa);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const UINT histRead  = m_historyIndex;
        const UINT histWrite = 1u - m_historyIndex;

        // Produce (half-res): t0 depth, t1 attrib.
        {
            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, 0);
            copySrv(device, table, depth);
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, depth);

            transition(cmd, m_aoHalf, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvCpu(kRtvHalf);
            drawFullscreen(cmd, m_psoGtao.Get(), 1, &rtv, m_halfW, m_halfH, passSrvGpu(frame, 0));
        }

        // Upsample + temporal: t0 AoHalf, t1 depth, t2 attrib, t3 history, t4 velocity.
        {
            transition(cmd, m_aoHalf, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transition(cmd, m_aoHistory[histRead], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transition(cmd, m_aoFull, D3D12_RESOURCE_STATE_RENDER_TARGET);
            transition(cmd, m_aoHistory[histWrite], D3D12_RESOURCE_STATE_RENDER_TARGET);

            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, 1);
            copySrv(device, table, cpuSrv(kCpuSrvHalf));
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, depth);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, cpuSrv(histRead == 0 ? kCpuSrvHist0 : kCpuSrvHist1));
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, vel);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rtvCpu(kRtvFull), rtvCpu(histWrite == 0 ? kRtvHistory0 : kRtvHistory1) };
            drawFullscreen(cmd, m_psoUpsample.Get(), 2, rtvs, m_width, m_height, passSrvGpu(frame, 1));
        }

        // Compose: t0 authored MRT3, t1 AoFull.
        {
            transition(cmd, m_aoFull, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transition(cmd, m_aoCompose, D3D12_RESOURCE_STATE_RENDER_TARGET);

            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, 2);
            copySrv(device, table, ao);
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, cpuSrv(kCpuSrvFull));

            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvCpu(kRtvCompose);
            drawFullscreen(cmd, m_psoCompose.Get(), 1, &rtv, m_width, m_height, passSrvGpu(frame, 2));
            transition(cmd, m_aoCompose, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        m_historyIndex = histWrite;
        m_needReset    = false;
    }

} // namespace Dark
