#include "Render/WaterPipeline.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Water/WaterWaves.h"

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
    DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

} // namespace

bool WaterPipeline::create(ID3D12Device* device)
{
    m_rootSignature.Reset();
    m_pso.Reset();
    if (!device)
    {
        DE_LOG_ERROR("WaterPipeline::create: null device");
        return false;
    }

    D3D12_ROOT_PARAMETER rootParams[1]{};
    rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootConstants].Constants.ShaderRegister = 0;
    rootParams[kRootConstants].Constants.RegisterSpace  = 0;
    rootParams[kRootConstants].Constants.Num32BitValues =
        static_cast<UINT>(sizeof(WaterFrameConstants) / 4);

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 1;
    rsDesc.pParameters       = rootParams;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers   = nullptr;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
            "D3D12SerializeRootSignature (water)"))
    {
        if (rsErr)
            DE_LOG_ERROR("Water root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }

    if (FailedHr(
            device->CreateRootSignature(
                0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
            "CreateRootSignature (water)"))
    {
        return false;
    }

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    if (!compileShaderFromContent("shaders/Water.hlsl", "VSMain", "vs_5_0", vs)
        || !compileShaderFromContent("shaders/Water.hlsl", "PSMain", "ps_5_0", ps))
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
    psoDesc.SampleMask     = UINT_MAX;

    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable           = TRUE;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp               = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;

    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;

    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
            "CreateGraphicsPipelineState (water)"))
    {
        m_rootSignature.Reset();
        return false;
    }

    DE_LOG_INFO("WaterPipeline: ready (Gerstner + Fresnel)");
    return true;
}

void WaterPipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void WaterPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const WaterFrameConstants& constants) const
{
    if (!cmd || !m_rootSignature)
        return;
    cmd->SetGraphicsRoot32BitConstants(
        kRootConstants, static_cast<UINT>(sizeof(WaterFrameConstants) / 4), &constants, 0);
}

void WaterPipeline::fillConstants(
    WaterFrameConstants& out,
    const float worldViewProj[16],
    const float cameraPos[3],
    float time,
    const float lightDir[3],
    const Water::WaterParams& params,
    const Sky::Environment* env)
{
    std::memcpy(out.worldViewProj, worldViewProj, sizeof(float) * 16);
    out.cameraPos[0] = cameraPos[0];
    out.cameraPos[1] = cameraPos[1];
    out.cameraPos[2] = cameraPos[2];
    out.time         = time;
    out.lightDir[0]  = lightDir[0];
    out.lightDir[1]  = lightDir[1];
    out.lightDir[2]  = lightDir[2];
    out.waterLevel   = params.waterLevel;
    out.flowDir[0]   = params.flowDir.x;
    out.flowDir[1]   = params.flowDir.y;
    out.flowStrength = params.flowStrength;
    out.specPower    = 96.0f;
    out.opacity      = 0.78f;
    out.shoreDepth   = 2.4f;
    out.fresnelF0    = 0.04f;
    out.steepness    = params.steepness;

    out.deepColor[0]    = 0.03f;
    out.deepColor[1]    = 0.12f;
    out.deepColor[2]    = 0.18f;
    out.shallowColor[0] = 0.12f;
    out.shallowColor[1] = 0.38f;
    out.shallowColor[2] = 0.36f;
    out.skyZenith[0]    = 0.22f;
    out.skyZenith[1]    = 0.40f;
    out.skyZenith[2]    = 0.62f;
    out.skyHorizon[0]   = 0.62f;
    out.skyHorizon[1]    = 0.72f;
    out.skyHorizon[2]   = 0.82f;

    if (env)
    {
        out.lightDir[0]   = env->lightDir().x;
        out.lightDir[1]   = env->lightDir().y;
        out.lightDir[2]   = env->lightDir().z;
        out.skyZenith[0]  = env->skyZenith().x;
        out.skyZenith[1]  = env->skyZenith().y;
        out.skyZenith[2]  = env->skyZenith().z;
        out.skyHorizon[0] = env->skyHorizon().x;
        out.skyHorizon[1] = env->skyHorizon().y;
        out.skyHorizon[2] = env->skyHorizon().z;
        // Dim body color a little under heavy cloud / rain so lakes match the sky.
        const float dim = 1.0f - 0.35f * env->weather.cloudCoverage - 0.15f * env->weather.rain;
        out.deepColor[0] *= dim;
        out.deepColor[1] *= dim;
        out.deepColor[2] *= dim;
        out.specPower = 96.0f * (1.0f - 0.55f * env->weather.cloudCoverage);
    }

    for (int i = 0; i < Water::kWaterWaveCount; ++i)
    {
        const Math::Vector2f d = Water::waveDirection(params, i);
        out.waves[i][0] = d.x;
        out.waves[i][1] = d.y;
        out.waves[i][2] = params.waves[i].frequency;
        out.waves[i][3] = params.waves[i].amplitude;
        out.waveSpeed[i] = params.waves[i].speed;
    }
}

} // namespace Dark
