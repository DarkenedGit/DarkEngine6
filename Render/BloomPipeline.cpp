#include "Render/BloomPipeline.h"
#include "Render/GraphTargets.h"
#include "Render/Profile.h"
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
        return m_psoExtract != nullptr && m_targets != nullptr && m_targets->resource(m_mips[0].id) != nullptr;
    }

    uint32_t BloomPipeline::mipWidth(UINT index) const
    {
        return m_targets->width(m_mips[index].id);
    }

    uint32_t BloomPipeline::mipHeight(UINT index) const
    {
        return m_targets->height(m_mips[index].id);
    }

    bool BloomPipeline::prepare() const
    {
        if (!m_targets)
            return false;
        // A recreated mip (new generation) only happens inside Renderer::resize's drained window.
        for (GraphTarget& mip : m_mips)
            m_targets->refresh(mip);
        return isValid();
    }

    const char* BloomPipeline::mipName(UINT index)
    {
        return index < kMipCount ? kMipNames[index] : "DE.Bloom.?";
    }

    ID3D12Resource* BloomPipeline::mipResource(UINT index) const
    {
        return m_targets && index < kMipCount ? m_targets->resource(m_mips[index].id) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE BloomPipeline::mipSrv(UINT index) const
    {
        return m_targets && index < kMipCount ? m_targets->srv(m_mips[index].id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE BloomPipeline::mipRtv(UINT index) const
    {
        return m_targets && index < kMipCount ? m_targets->rtv(m_mips[index].id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
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

    bool BloomPipeline::createTargets()
    {
        resetTargets();

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
        if (!createTargets())
            return false;

        DE_LOG_INFO(LogCategory::Render, "BloomPipeline: ready {}x{} ({} mips, graph targets)", mipWidth(0), mipHeight(0), kMipCount);
        return true;
    }

    void BloomPipeline::drawStep(ID3D12GraphicsCommandList* cmd, BloomStep step, D3D12_GPU_DESCRIPTOR_HANDLE src, D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW,
                                 uint32_t dstH, uint32_t srcW, uint32_t srcH, float strength, float exposure) const
    {
        if (!cmd || !m_rootSignature || src.ptr == 0 || rtv.ptr == 0)
            return;
        ID3D12PipelineState* pso = nullptr;
        switch (step)
        {
        case BloomStep::Extract:
            pso = m_psoExtract.Get();
            break;
        case BloomStep::Downsample:
            pso = m_psoDownsample.Get();
            break;
        case BloomStep::Upsample:
            pso = m_psoUpsample.Get();
            break;
        case BloomStep::Composite:
            pso = m_psoComposite.Get();
            break;
        }
        if (!pso)
            return;
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(dstW);
        vp.Height   = static_cast<float>(dstH);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(dstW), static_cast<LONG>(dstH) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, src);
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

} // namespace Dark
