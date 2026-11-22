#include "Render/GtaoPipeline.h"
#include "Render/Profile.h"
#include "Render/Camera3D.h"
#include "Render/GraphTargets.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cstring>

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
            m.store(dst);
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

    GtaoPipeline::~GtaoPipeline()
    {
        release();
    }

    Rg::TargetDesc GtaoPipeline::historyDesc()
    {
        const float identity[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        return colorTargetDesc(DXGI_FORMAT_R16_FLOAT, Rg::TargetSize::OutputFloor, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, identity);
    }

    bool GtaoPipeline::isValid() const
    {
        return m_psoGtao && m_psoUpsample && m_psoCompose && m_targets && m_targets->resource(m_aoHalf.id) && m_targets->resource(m_aoFull.id) && m_targets->resource(m_aoCompose.id) && m_cbUpload &&
               m_cbMapped;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::composeSrvCpu() const
    {
        return isValid() ? m_targets->srv(m_aoCompose.id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::aoFullSrvCpu() const
    {
        return isValid() ? m_targets->srv(m_aoFull.id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    void GtaoPipeline::transitionAoFull(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (m_targets)
            m_targets->transition(cmd, m_aoFull, after);
    }

    void GtaoPipeline::clearAoFullIdentity(ID3D12GraphicsCommandList* cmd)
    {
        if (!cmd || !m_targets || !m_targets->resource(m_aoFull.id))
            return;
        m_targets->transition(cmd, m_aoFull, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float white[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        cmd->ClearRenderTargetView(m_targets->rtv(m_aoFull.id), white, 0, nullptr);
    }

    const GraphTarget& GtaoPipeline::target(GtaoTarget which) const
    {
        switch (which)
        {
        case GtaoTarget::Half:
            return m_aoHalf;
        case GtaoTarget::Full:
            return m_aoFull;
        case GtaoTarget::Compose:
            break;
        }
        return m_aoCompose;
    }

    ID3D12Resource* GtaoPipeline::resource(GtaoTarget which) const
    {
        return m_targets ? m_targets->resource(target(which).id) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::srv(GtaoTarget which) const
    {
        return m_targets ? m_targets->srv(target(which).id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GtaoPipeline::rtv(GtaoTarget which) const
    {
        return m_targets ? m_targets->rtv(target(which).id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    const char* GtaoPipeline::name(GtaoTarget which)
    {
        switch (which)
        {
        case GtaoTarget::Half:
            return "DE.Gtao.AoHalf";
        case GtaoTarget::Full:
            return "DE.Gtao.AoFull";
        case GtaoTarget::Compose:
            break;
        }
        return "DE.Gtao.AoCompose";
    }

    void GtaoPipeline::resetTargets()
    {
        if (m_targets)
        {
            m_targets->unregisterTarget(m_aoHalf);
            m_targets->unregisterTarget(m_aoFull);
            m_targets->unregisterTarget(m_aoCompose);
        }
        m_aoHalf    = {};
        m_aoFull    = {};
        m_aoCompose = {};
    }

    void GtaoPipeline::release()
    {
        resetTargets();
        m_targets = nullptr;
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
        if (!compileShaderFromContent("shaders/Gtao.hlsl", "VSMain", ShaderStage::Vertex, vs)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSGtao", ShaderStage::Pixel, psGtao)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSUpsampleTemporal", ShaderStage::Pixel, psUp)
            || !compileShaderFromContent("shaders/Gtao.hlsl", "PSCompose", ShaderStage::Pixel, psComp))
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

    bool GtaoPipeline::createTargets()
    {
        resetTargets();

        // Half is rounded up (ceil(w/2)), matching the produce pass's 2x2 footprint.
        const float identity[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        const D3D12_RESOURCE_STATES rt = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_aoHalf                                = m_targets->registerTarget(name(GtaoTarget::Half), colorTargetDesc(DXGI_FORMAT_R8_UNORM, Rg::TargetSize::OutputCeil, 2, rt, identity));
        m_aoFull                                = m_targets->registerTarget(name(GtaoTarget::Full), colorTargetDesc(DXGI_FORMAT_R8_UNORM, Rg::TargetSize::OutputFloor, 1, rt, identity));
        m_aoCompose                             = m_targets->registerTarget(name(GtaoTarget::Compose), colorTargetDesc(DXGI_FORMAT_R8_UNORM, Rg::TargetSize::OutputFloor, 1, rt, identity));
        if (!m_aoHalf.id.valid() || !m_aoFull.id.valid() || !m_aoCompose.id.valid())
        {
            resetTargets();
            return false;
        }
        return true;
    }

    bool GtaoPipeline::create(GraphTargets& targets)
    {
        ID3D12Device* device = targets.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GtaoPipeline::create: null device");
            release();
            return false;
        }
        if (m_targets != &targets)
            resetTargets();
        m_targets = &targets;
        if (!m_psoGtao && !createPsos(device))
        {
            release();
            return false;
        }
        if (!createCbv(device))
        {
            release();
            return false;
        }
        if (!createTargets())
            return false;
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        DE_LOG_INFO(LogCategory::Render, "GtaoPipeline: ready {}x{} (half {}x{}, graph targets)", targets.width(m_aoFull.id), targets.height(m_aoFull.id), targets.width(m_aoHalf.id),
                    targets.height(m_aoHalf.id));
        return true;
    }

    void GtaoPipeline::fillParams(GtaoGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings,
                                  bool resetHistory, uint32_t frameIndex) const
    {
        out = {};
        out.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_aoHalf.id), 1u));
        out.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_aoHalf.id), 1u));
        out.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_aoFull.id), 1u));
        out.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_aoFull.id), 1u));
        copyMatrix(out.invProj, camera.GetProj().Inverse());
        copyMatrix(out.view, camera.GetView());
        copyMatrix(out.reprojection, camera.GetViewProj().Inverse() * prevViewProj);
        out.radius      = settings.radius;
        out.power       = settings.power;
        out.intensity   = settings.intensity;
        out.thickness   = kThickness;
        out.nearZ       = camera.GetNearZ();
        out.farZ        = camera.GetFarZ();
        out.reset       = resetHistory ? 1.0f : 0.0f;
        out._pad        = static_cast<float>(frameIndex & 7u);
    }

    void GtaoPipeline::drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, uint32_t w, uint32_t h,
                                      D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const
    {
        if (!cmd || !pso || params == 0 || tableGpu.ptr == 0 || m_srvIncr == 0)
            return;
        for (UINT i = 0; i < nRtv; ++i)
            if (rtvs[i].ptr == 0)
                return;
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(nRtv, rtvs, FALSE, nullptr);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, params);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, tableGpu);
        D3D12_GPU_DESCRIPTOR_HANDLE velGpu = tableGpu;
        velGpu.ptr += static_cast<UINT64>(4) * m_srvIncr;
        cmd->SetGraphicsRootDescriptorTable(kRootVelocity, velGpu);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    D3D12_GPU_VIRTUAL_ADDRESS GtaoPipeline::uploadParams(const Camera3D& camera, const Math::Matrix4f& prevViewProj, const GtaoSettings& settings, bool resetHistory, uint32_t frameIndex)
    {
        if (!isValid())
            return 0;
        const UINT    frame = frameIndex % kFrameCount;
        GtaoGpuParams params{};
        fillParams(params, camera, prevViewProj, settings, resetHistory, frameIndex);
        std::memcpy(m_cbMapped + static_cast<size_t>(frame) * kCbBytes, &params, sizeof(params));
        return m_cbGpu + static_cast<UINT64>(frame) * kCbBytes;
    }

    D3D12_GPU_VIRTUAL_ADDRESS GtaoPipeline::paramsAddress(uint32_t frameIndex) const
    {
        return isValid() ? m_cbGpu + static_cast<UINT64>(frameIndex % kFrameCount) * kCbBytes : 0;
    }

    void GtaoPipeline::drawProduce(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table) const
    {
        if (!isValid())
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[1] = { rtv(GtaoTarget::Half) };
        drawFullscreen(cmd, m_psoGtao.Get(), 1, rtvs, m_targets->width(m_aoHalf.id), m_targets->height(m_aoHalf.id), params, table);
    }

    void GtaoPipeline::drawUpsample(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table, D3D12_CPU_DESCRIPTOR_HANDLE historyRtv) const
    {
        if (!isValid())
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rtv(GtaoTarget::Full), historyRtv };
        drawFullscreen(cmd, m_psoUpsample.Get(), 2, rtvs, m_targets->width(m_aoFull.id), m_targets->height(m_aoFull.id), params, table);
    }

    void GtaoPipeline::drawCompose(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS params, D3D12_GPU_DESCRIPTOR_HANDLE table) const
    {
        if (!isValid())
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[1] = { rtv(GtaoTarget::Compose) };
        drawFullscreen(cmd, m_psoCompose.Get(), 1, rtvs, m_targets->width(m_aoCompose.id), m_targets->height(m_aoCompose.id), params, table);
    }

} // namespace Dark
