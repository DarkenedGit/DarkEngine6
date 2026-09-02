#include "Render/MeshPipeline.h"
#include "Render/PsoUtil.h"
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

    bool MeshPipeline::create(ID3D12Device* device, MeshPass pass)
    {
        m_rootSignature.Reset();
        m_psoSolid.Reset();
        m_psoWire.Reset();
        m_psoPoint.Reset();

        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "MeshPipeline::create: null device");
            return false;
        }

        const bool gbuffer = pass == MeshPass::GBuffer;

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = gbuffer ? 1u : kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[3]{};
        rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[kRootConstants].Constants.ShaderRegister = 0;
        rootParams[kRootConstants].Constants.RegisterSpace  = 0;
        rootParams[kRootConstants].Constants.Num32BitValues =
            gbuffer ? static_cast<UINT>(sizeof(MeshGBufferConstants) / 4) : static_cast<UINT>(sizeof(MeshFrameConstants) / 4);

        rootParams[kRootAlbedoSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[kRootAlbedoSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParams[kRootAlbedoSrv].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[kRootAlbedoSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

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

        UINT                     paramCount = 2;
        UINT                     sampCount  = 1;
        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        if (!gbuffer)
        {
            rootParams[kRootShadowCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[kRootShadowCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
            rootParams[kRootShadowCbv].Descriptor.ShaderRegister = 1;
            rootParams[kRootShadowCbv].Descriptor.RegisterSpace  = 0;
            paramCount = 3;
            sampCount  = 2;
        }
        rsDesc.NumParameters     = paramCount;
        rsDesc.pParameters       = rootParams;
        rsDesc.NumStaticSamplers = sampCount;
        rsDesc.pStaticSamplers   = samps;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }

        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature"))
        {
            return false;
        }

        const char* shader = gbuffer ? "shaders/BasicMeshGBuffer.hlsl" : "shaders/BasicMesh.hlsl";
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent(shader, "VSMain", "vs_5_0", vs) || !compileShaderFromContent(shader, "PSMain", "ps_5_0", ps))
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
        psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask                                       = UINT_MAX;

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        // MeshGen box faces are CCW from outside.
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.RasterizerState.DepthClipEnable       = TRUE;

        psoDesc.DepthStencilState.DepthEnable    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable  = FALSE;

        psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        if (gbuffer)
        {
            psoDesc.NumRenderTargets = 2;
            psoDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.RTVFormats[1]    = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        else
        {
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0]    = meshPassColorFormat(pass);
        }
        psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc            = { 1, 0 };

        if (!createFillVariantPsos(device, psoDesc, m_psoSolid, m_psoWire, m_psoPoint))
        {
            m_rootSignature.Reset();
            return false;
        }

        const char* passName = pass == MeshPass::GBuffer ? "GBuffer" : (pass == MeshPass::ForwardHdr ? "HDR16" : "UNORM");
        DE_LOG_INFO(LogCategory::Render, "MeshPipeline: ready (textured, solid/wire/point, {})", passName);
        return true;
    }

    void MeshPipeline::bind(ID3D12GraphicsCommandList* cmd, DebugFill fill) const
    {
        ID3D12PipelineState* pso = selectFillPso(fill, m_psoSolid.Get(), m_psoWire.Get(), m_psoPoint.Get());
        if (!cmd || !pso)
            return;
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
    }

    void MeshPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const MeshFrameConstants& constants) const
    {
        if (!cmd || !m_rootSignature)
            return;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(MeshFrameConstants) / 4), &constants, 0);
    }

    void MeshPipeline::setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshGBufferConstants& constants) const
    {
        if (!cmd || !m_rootSignature)
            return;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(MeshGBufferConstants) / 4), &constants, 0);
    }

} // namespace Dark
