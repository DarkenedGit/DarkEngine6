#include "Render/TerrainPipeline.h"
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
            DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

    } // namespace

    bool TerrainPipeline::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_pso.Reset();

        if (!device)
        {
            DE_LOG_ERROR("TerrainPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[3]{};
        rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[kRootConstants].Constants.ShaderRegister = 0;
        rootParams[kRootConstants].Constants.RegisterSpace  = 0;
        rootParams[kRootConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(TerrainFrameConstants) / 4);

        rootParams[kRootSrvTable].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[kRootSrvTable].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParams[kRootSrvTable].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[kRootSrvTable].DescriptorTable.pDescriptorRanges   = &srvRange;

        rootParams[kRootShadowCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[kRootShadowCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParams[kRootShadowCbv].Descriptor.ShaderRegister = 1;
        rootParams[kRootShadowCbv].Descriptor.RegisterSpace  = 0;

        D3D12_STATIC_SAMPLER_DESC samps[2]{};
        samps[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samps[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samps[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samps[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[0].ShaderRegister   = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        samps[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samps[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samps[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samps[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samps[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samps[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samps[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[1].ShaderRegister   = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 3;
        rsDesc.pParameters       = rootParams;
        rsDesc.NumStaticSamplers = 2;
        rsDesc.pStaticSamplers   = samps;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (terrain)"))
        {
            if (rsErr)
                DE_LOG_ERROR("Terrain root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }

        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (terrain)"))
        {
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Terrain.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/Terrain.hlsl", "PSMain", "ps_5_0", ps))
        {
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature                                   = m_rootSignature.Get();
        psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask                                       = UINT_MAX;

        psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.RasterizerState.DepthClipEnable       = TRUE;

        psoDesc.DepthStencilState.DepthEnable    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable  = FALSE;

        psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (terrain)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        DE_LOG_INFO("TerrainPipeline: ready (4 layers + splat)");
        return true;
    }

    void TerrainPipeline::bind(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_pso)
            return;
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
    }

    void TerrainPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const TerrainFrameConstants& constants) const
    {
        if (!cmd || !m_rootSignature)
            return;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(TerrainFrameConstants) / 4), &constants, 0);
    }

} // namespace Dark
