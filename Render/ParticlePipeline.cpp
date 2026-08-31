#include "Render/ParticlePipeline.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

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

    } // namespace

    bool ParticlePipeline::create(ID3D12Device* device, bool additive, int depthBias, float slopeScaledDepthBias)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        if (!device)
            return false;

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = 16;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "Particle RS serialize"))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "Particle CreateRootSignature"))
            return false;

        ComPtr<ID3DBlob> vs, ps;
        if (!compileShaderFromContent("shaders/Particle.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/Particle.hlsl", "PSMain", "ps_5_0", ps))
            return false;

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask     = UINT_MAX;

        // Alpha or additive blend
        D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
        rt.BlendEnable                     = TRUE;
        rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (additive)
        {
            rt.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend      = D3D12_BLEND_ONE;
            rt.BlendOp        = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ONE;
            rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        }
        else
        {
            rt.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp        = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        }

        pso.RasterizerState.FillMode             = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode             = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable      = TRUE;
        pso.RasterizerState.DepthBias            = depthBias;
        pso.RasterizerState.SlopeScaledDepthBias = slopeScaledDepthBias;

        pso.DepthStencilState.DepthEnable    = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

        pso.InputLayout           = { layout, _countof(layout) };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets      = 1;
        pso.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "Particle PSO"))
            return false;

        DE_LOG_INFO(LogCategory::Render, "ParticlePipeline: ready ({})", additive ? "additive" : "alpha");
        return true;
    }

    void ParticlePipeline::bind(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_pso)
            return;
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
    }

    void ParticlePipeline::setConstants(ID3D12GraphicsCommandList* cmd, const ParticleFrameConstants& c) const
    {
        if (!cmd)
            return;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, 16, &c, 0);
    }

} // namespace Dark
