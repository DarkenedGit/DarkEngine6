#include "Render/BloomPipeline.h"
#include "Render/GraphTargets.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"


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

        void fillBlendAdditive(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
            rt.BlendEnable                     = TRUE;
            rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
            rt.SrcBlend                        = D3D12_BLEND_ONE;
            rt.DestBlend                       = D3D12_BLEND_ONE;
            rt.BlendOp                         = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha                   = D3D12_BLEND_ZERO;
            rt.DestBlendAlpha                  = D3D12_BLEND_ONE;
            rt.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;
        }

        const char* const kMipNames[BloomPipeline::kMipCount] = {
            "DE.Bloom.Extract", "DE.Bloom.Down0", "DE.Bloom.Down1", "DE.Bloom.Down2", "DE.Bloom.Down3", "DE.Bloom.Down4",
        };
    } // namespace

    BloomPipeline::~BloomPipeline()
    {
        release();
    }

    void BloomPipeline::resetTargets()
    {
        for (GraphTarget& mip : m_mips)
        {
            if (m_targets)
                m_targets->unregisterTarget(mip);
            mip = {};
        }
        m_srvHeap.Reset();
        m_srvCpu = {};
        m_srvGpu = {};
    }

    void BloomPipeline::release()
    {
        resetTargets();
        m_targets = nullptr;
        m_psoExtract.Reset();
        m_psoDownsample.Reset();
        m_psoUpsample.Reset();
        m_psoComposite.Reset();
        m_rootSignature.Reset();
    }

    bool BloomPipeline::isValid() const
    {
        return m_psoExtract != nullptr && m_srvHeap != nullptr && m_targets != nullptr && m_targets->resource(m_mips[0].id) != nullptr;
    }

    uint32_t BloomPipeline::mipWidth(UINT index) const
    {
        return m_targets->width(m_mips[index].id);
    }

    uint32_t BloomPipeline::mipHeight(UINT index) const
    {
        return m_targets->height(m_mips[index].id);
    }

    void BloomPipeline::refreshViews() const
    {
        // Mip SRVs sit at fixed slots of the shader-visible table. A recreated mip (new generation)
        // only happens inside Renderer::resize's drained window, so no in-flight list still reads
        // the old slot contents when they are rewritten here.
        bool recreated = false;
        for (GraphTarget& mip : m_mips)
            recreated = m_targets->refresh(mip) || recreated;
        if (!recreated && m_viewsWritten)
            return;
        ID3D12Device* device = m_targets->device();
        for (UINT i = 0; i < kMipCount; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvCpu;
            dst.ptr += static_cast<SIZE_T>(kHdrSlots + i) * m_srvIncr;
            device->CopyDescriptorsSimple(1, dst, m_targets->srv(m_mips[i].id), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        m_viewsWritten = true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE BloomPipeline::srvGpu(UINT slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    void BloomPipeline::transitionMip(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const
    {
        if (index < kMipCount)
            m_targets->transition(cmd, m_mips[index], after);
    }

    bool BloomPipeline::createPsos(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_psoExtract.Reset();
        m_psoDownsample.Reset();
        m_psoUpsample.Reset();
        m_psoComposite.Reset();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = kConstantCount;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &samp;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (bloom)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Bloom RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (bloom)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> psExtract;
        ComPtr<ID3DBlob> psDown;
        ComPtr<ID3DBlob> psUp;
        ComPtr<ID3DBlob> psComp;
        if (!compileShaderFromContent("shaders/Bloom.hlsl", "VSMain", ShaderStage::Vertex, vs)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSExtract", ShaderStage::Pixel, psExtract)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSDownsample", ShaderStage::Pixel, psDown)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSUpsample", ShaderStage::Pixel, psUp)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSComposite", ShaderStage::Pixel, psComp))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature                                   = m_rootSignature.Get();
        pso.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.SampleMask                                       = UINT_MAX;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable                  = TRUE;
        pso.DepthStencilState.DepthEnable                    = FALSE;
        pso.DepthStencilState.StencilEnable                  = FALSE;
        pso.InputLayout                                      = { nullptr, 0 };
        pso.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                                 = 1;
        pso.RTVFormats[0]                                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc                                       = { 1, 0 };

        pso.PS = { psExtract->GetBufferPointer(), psExtract->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoExtract)), "CreateGraphicsPipelineState (bloom extract)"))
        {
            m_rootSignature.Reset();
            return false;
        }
        pso.PS = { psDown->GetBufferPointer(), psDown->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoDownsample)), "CreateGraphicsPipelineState (bloom downsample)"))
        {
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        fillBlendAdditive(pso);
        pso.PS = { psUp->GetBufferPointer(), psUp->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoUpsample)), "CreateGraphicsPipelineState (bloom upsample)"))
        {
            m_psoDownsample.Reset();
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        pso.PS = { psComp->GetBufferPointer(), psComp->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoComposite)), "CreateGraphicsPipelineState (bloom composite)"))
        {
            m_psoUpsample.Reset();
            m_psoDownsample.Reset();
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        return true;
    }

    bool BloomPipeline::createTargets(ID3D12Device* device)
    {
        resetTargets();
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kSrvCount;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap bloom SRV"))
            return false;
        m_srvHeap->SetName(L"DE.Bloom.Tables");
        m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        // Mip k is max(floor(w / 2^(k+1)), 1): the old halving chain from w/2, as output-relative targets.
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (UINT i = 0; i < kMipCount; ++i)
        {
            const Rg::TargetDesc desc =
                colorTargetDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, Rg::TargetSize::OutputFloor, 2u << i, D3D12_RESOURCE_STATE_RENDER_TARGET, clearColor);
            m_mips[i] = m_targets->registerTarget(kMipNames[i], desc);
            if (!m_mips[i].id.valid())
            {
                resetTargets();
                return false;
            }
        }
        m_viewsWritten = false;
        refreshViews();
        return true;
    }

    bool BloomPipeline::create(GraphTargets& targets)
    {
        ID3D12Device* device = targets.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "BloomPipeline::create: null device");
            release();
            return false;
        }
        if (m_targets != &targets)
            resetTargets();
        m_targets = &targets;
        if (!m_psoExtract && !createPsos(device))
        {
            resetTargets();
            return false;
        }
        if (!createTargets(device))
            return false;

        DE_LOG_INFO(LogCategory::Render, "BloomPipeline: ready {}x{} ({} mips, graph targets)", mipWidth(0), mipHeight(0), kMipCount);
        m_loggedSkip = false;
        return true;
    }

    void BloomPipeline::drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srcGpu,
                                       D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH, float strength,
                                       float exposure) const
    {
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(dstW);
        vp.Height   = static_cast<float>(dstH);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(dstW), static_cast<LONG>(dstH) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, srcGpu);
        const float constants[kConstantCount] = {
            1.0f / static_cast<float>(Math::Max(srcW, 1u)),
            1.0f / static_cast<float>(Math::Max(srcH, 1u)),
            1.0f / static_cast<float>(Math::Max(dstW, 1u)),
            1.0f / static_cast<float>(Math::Max(dstH, 1u)),
            bloomThresholdForExposure(exposure),
            bloomKneeForExposure(exposure),
            strength,
            0.0f,
        };
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void BloomPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float strength, float exposure) const
    {
        if (!cmd || !isValid())
        {
            if (!isValid() && !m_loggedSkip)
            {
                DE_LOG_ERROR(LogCategory::Render, "BloomPipeline::draw: not valid — skipping");
                m_loggedSkip = true;
            }
            return;
        }
        ID3D12Device* device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdrSrv = renderer.hdrSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = renderer.hdrRtv();
        if (!device || hdrSrv.ptr == 0 || hdrRtv.ptr == 0)
            return;

        refreshViews();
        const GpuScope bloom(cmd, "Bloom", ProfileColor::Bloom);
        const UINT hdrSlot = renderer.frameIndex() % kHdrSlots;
        D3D12_CPU_DESCRIPTOR_HANDLE hdrDst = m_srvCpu;
        hdrDst.ptr += static_cast<SIZE_T>(hdrSlot) * m_srvIncr;
        device->CopyDescriptorsSimple(1, hdrDst, hdrSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Unbind HDR/DSV, then HDR → SRV for extract.
        transitionMip(cmd, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE extractRtv = m_targets->rtv(m_mips[0].id);
        cmd->OMSetRenderTargets(1, &extractRtv, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        drawFullscreen(cmd, m_psoExtract.Get(), srvGpu(hdrSlot), extractRtv, mipWidth(0), mipHeight(0), renderer.width(), renderer.height(), strength, exposure);

        for (UINT i = 0; i < kDownCount; ++i)
        {
            transitionMip(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transitionMip(cmd, i + 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
            drawFullscreen(cmd, m_psoDownsample.Get(), srvGpu(kHdrSlots + i), m_targets->rtv(m_mips[i + 1].id), mipWidth(i + 1), mipHeight(i + 1), mipWidth(i), mipHeight(i), strength, exposure);
        }

        for (UINT i = kMipCount - 1; i > 0; --i)
        {
            transitionMip(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transitionMip(cmd, i - 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
            drawFullscreen(cmd, m_psoUpsample.Get(), srvGpu(kHdrSlots + i), m_targets->rtv(m_mips[i - 1].id), mipWidth(i - 1), mipHeight(i - 1), mipWidth(i), mipHeight(i), strength, exposure);
        }

        transitionMip(cmd, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        drawFullscreen(cmd, m_psoComposite.Get(), srvGpu(kHdrSlots + 0), hdrRtv, renderer.width(), renderer.height(), mipWidth(0), mipHeight(0), strength, exposure);
        // HDR remains RENDER_TARGET for TaaPipeline::draw → bindPostHdr.
    }

} // namespace Dark
