#include "Render/ShadowPipeline.h"
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

bool ShadowPipeline::create(ID3D12Device* device)
{
    m_rootSignature.Reset();
    m_pso.Reset();
    if (!device)
        return false;

    D3D12_ROOT_PARAMETER root{};
    root.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
    root.Constants.ShaderRegister = 0;
    root.Constants.RegisterSpace  = 0;
    root.Constants.Num32BitValues = 16;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &root;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
            "D3D12SerializeRootSignature (shadow)"))
    {
        if (rsErr)
            DE_LOG_ERROR("Shadow root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }

    if (FailedHr(
            device->CreateRootSignature(
                0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
            "CreateRootSignature (shadow)"))
    {
        return false;
    }

    ComPtr<ID3DBlob> vs;
    if (!compileShaderFromContent("shaders/ShadowDepth.hlsl", "VSMain", "vs_5_0", vs))
        return false;

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS             = { nullptr, 0 };
    pso.SampleMask     = UINT_MAX;

    pso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.RasterizerState.DepthClipEnable       = TRUE;
    pso.RasterizerState.DepthBias             = 2500;
    pso.RasterizerState.SlopeScaledDepthBias  = 1.5f;
    pso.RasterizerState.DepthBiasClamp        = 0.0f;

    pso.DepthStencilState.DepthEnable    = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState.StencilEnable  = FALSE;

    pso.InputLayout           = { layout, _countof(layout) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 0;
    pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc            = { 1, 0 };

    if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (shadow)"))
    {
        m_rootSignature.Reset();
        return false;
    }

    DE_LOG_INFO("ShadowPipeline: ready (depth-only CSM caster)");
    return true;
}

void ShadowPipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void ShadowPipeline::setWvp(ID3D12GraphicsCommandList* cmd, const float wvp[16]) const
{
    if (!cmd || !m_rootSignature || !wvp)
        return;
    cmd->SetGraphicsRoot32BitConstants(kRootWvp, 16, wvp, 0);
}

} // namespace Dark
