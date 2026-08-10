#include "Render/MeshPipeline.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <d3dcompiler.h>

namespace Dark
{
namespace {

bool FailedHr(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr))
        return false;
    DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

} // namespace

bool MeshPipeline::create(ID3D12Device* device)
{
    m_rootSignature.Reset();
    m_pso.Reset();

    if (!device)
    {
        DE_LOG_ERROR("MeshPipeline::create: null device");
        return false;
    }

    // Root signature:
    //  [0] 32-bit constants b0  (MeshFrameConstants)
    //  [1] descriptor table t0  (albedo SRV)
    //  static sampler s0        (linear wrap)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2]{};
    rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootConstants].Constants.ShaderRegister = 0;
    rootParams[kRootConstants].Constants.RegisterSpace  = 0;
    rootParams[kRootConstants].Constants.Num32BitValues =
        static_cast<UINT>(sizeof(MeshFrameConstants) / 4);

    rootParams[kRootAlbedoSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootAlbedoSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootAlbedoSrv].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootAlbedoSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MipLODBias       = 0.0f;
    samp.MaxAnisotropy    = 1;
    samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samp.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samp.MinLOD           = 0.0f;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.RegisterSpace    = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &samp;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
            "D3D12SerializeRootSignature"))
    {
        if (rsErr)
            DE_LOG_ERROR("Root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }

    if (FailedHr(
            device->CreateRootSignature(
                0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
            "CreateRootSignature"))
    {
        return false;
    }

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    if (!compileShaderFromContent("shaders/BasicMesh.hlsl", "VSMain", "vs_5_0", vs)
        || !compileShaderFromContent("shaders/BasicMesh.hlsl", "PSMain", "ps_5_0", ps))
    {
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask     = UINT_MAX;

    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    // MeshGen box faces are CCW from outside.
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

    if (FailedHr(
            device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)),
            "CreateGraphicsPipelineState"))
    {
        m_rootSignature.Reset();
        return false;
    }

    DE_LOG_INFO("MeshPipeline: ready (textured)");
    return true;
}

void MeshPipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void MeshPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const MeshFrameConstants& constants) const
{
    if (!cmd || !m_rootSignature)
        return;
    cmd->SetGraphicsRoot32BitConstants(
        kRootConstants, static_cast<UINT>(sizeof(MeshFrameConstants) / 4), &constants, 0);
}

} // namespace Dark
