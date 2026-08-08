#include "Render/LinePipeline.h"
#include "Core/Log.h"

#include <d3dcompiler.h>
#include <cstring>

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

static const char kLineHlsl[] = R"HLSL(
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4   color;
};

struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return color;
}
)HLSL";

bool CompileShader(const char* src, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode)
{
    ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const HRESULT hr = D3DCompile(
        src, strlen(src), "Line.hlsl", nullptr, nullptr, entry, target, flags, 0, &outBytecode, &errors);
    if (FAILED(hr))
    {
        const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown";
        DE_LOG_ERROR("Line shader compile failed ({}): {}", entry, msg);
        return false;
    }
    return true;
}

} // namespace

bool LinePipeline::create(ID3D12Device* device)
{
    m_rootSignature.Reset();
    m_pso.Reset();
    if (!device)
    {
        DE_LOG_ERROR("LinePipeline::create: null device");
        return false;
    }

    D3D12_ROOT_PARAMETER rootParam{};
    rootParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace  = 0;
    rootParam.Constants.Num32BitValues = static_cast<UINT>(sizeof(LineFrameConstants) / 4);

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &rootParam;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
            "Line D3D12SerializeRootSignature"))
    {
        return false;
    }
    if (FailedHr(
            device->CreateRootSignature(
                0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
            "Line CreateRootSignature"))
    {
        return false;
    }

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    if (!CompileShader(kLineHlsl, "VSMain", "vs_5_0", vs) || !CompileShader(kLineHlsl, "PSMain", "ps_5_0", ps))
        return false;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask     = UINT_MAX;

    psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // grid doesn't occlude
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    if (FailedHr(
            device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)),
            "Line CreateGraphicsPipelineState"))
    {
        return false;
    }

    DE_LOG_INFO("LinePipeline: ready");
    return true;
}

void LinePipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void LinePipeline::setConstants(ID3D12GraphicsCommandList* cmd, const LineFrameConstants& constants) const
{
    if (!cmd || !m_rootSignature)
        return;
    cmd->SetGraphicsRoot32BitConstants(
        kRootConstants, static_cast<UINT>(sizeof(LineFrameConstants) / 4), &constants, 0);
}

} // namespace Dark
