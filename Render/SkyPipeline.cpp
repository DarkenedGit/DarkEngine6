#include "Render/SkyPipeline.h"
#include "Render/ShaderCompile.h"
#include "Render/Camera3D.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"

#include <cmath>
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
    DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

} // namespace

bool SkyPipeline::create(ID3D12Device* device, SkyPass pass, DXGI_FORMAT colorFormat)
{
    m_rootSignature.Reset();
    m_pso.Reset();
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "SkyPipeline::create: null device");
        return false;
    }
    const bool deferredLast = pass == SkyPass::DeferredLast;

    D3D12_ROOT_PARAMETER rootParams[1]{};
    rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootConstants].Constants.ShaderRegister = 0;
    rootParams[kRootConstants].Constants.RegisterSpace  = 0;
    rootParams[kRootConstants].Constants.Num32BitValues =
        static_cast<UINT>(sizeof(SkyFrameConstants) / 4);

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = rootParams;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
            "D3D12SerializeRootSignature (sky)"))
    {
        if (rsErr)
            DE_LOG_ERROR(LogCategory::Render, "Sky root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }

    if (FailedHr(
            device->CreateRootSignature(
                0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
            "CreateRootSignature (sky)"))
    {
        return false;
    }

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    const char* vsEntry = deferredLast ? "VSMainDeferred" : "VSMain";
    if (!compileShaderFromContent("shaders/Sky.hlsl", vsEntry, "vs_5_0", vs)
        || !compileShaderFromContent("shaders/Sky.hlsl", "PSMain", "ps_5_0", ps))
    {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.SampleMask     = UINT_MAX;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable    = deferredLast ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc      = deferredLast ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;

    psoDesc.InputLayout           = { nullptr, 0 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = colorFormat;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    if (FailedHr(
            device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)),
            "CreateGraphicsPipelineState (sky)"))
    {
        m_rootSignature.Reset();
        return false;
    }

    DE_LOG_INFO(LogCategory::Render, "SkyPipeline: ready ({})", deferredLast ? "DeferredLast" : "ForwardFirst");
    return true;
}

void SkyPipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void SkyPipeline::draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const Sky::Environment& env, float exposure) const
{
    if (!cmd || !m_pso)
        return;

    bind(cmd);

    const Math::Vector3f cam   = camera.GetPosition();
    const Math::Vector3f right = camera.GetRight();
    const Math::Vector3f up    = camera.GetUp();
    const Math::Vector3f look  = camera.GetLook();
    const float tanHalfFovY    = tanf(0.5f * camera.GetFovY());
    const float aspect         = camera.GetAspect() > 1.0e-4f ? camera.GetAspect() : 1.0f;
    const float tanHalfFovX    = tanHalfFovY * aspect;

    SkyFrameConstants cb{};
    cb.cameraPos[0]   = cam.x;
    cb.cameraPos[1]   = cam.y;
    cb.cameraPos[2]   = cam.z;
    cb.coverage       = env.weather.cloudCoverage;
    cb.sunDir[0]      = env.sunDir().x;
    cb.sunDir[1]      = env.sunDir().y;
    cb.sunDir[2]      = env.sunDir().z;
    cb.turbidity      = env.weather.turbidity;
    cb.sunColor[0]    = env.sunColor().x;
    cb.sunColor[1]    = env.sunColor().y;
    cb.sunColor[2]    = env.sunColor().z;
    cb.cloudTime      = env.timeOfDay;
    cb.moonDir[0]     = env.moonDir().x;
    cb.moonDir[1]     = env.moonDir().y;
    cb.moonDir[2]     = env.moonDir().z;
    cb.windSpeed      = env.weather.windSpeed;
    cb.moonColor[0]   = env.moonColor().x;
    cb.moonColor[1]   = env.moonColor().y;
    cb.moonColor[2]   = env.moonColor().z;
    cb.rain           = env.weather.rain;
    cb.windDir[0]     = env.weather.windDir.x;
    cb.windDir[1]     = env.weather.windDir.y;
    cb.sunElevation    = env.sunElevation();
    cb.exposure        = exposure >= 0.0f ? exposure : env.exposure();
    cb.cameraRight[0]  = right.x;
    cb.cameraRight[1]  = right.y;
    cb.cameraRight[2]  = right.z;
    cb.tanHalfFovX     = tanHalfFovX;
    cb.cameraUp[0]     = up.x;
    cb.cameraUp[1]     = up.y;
    cb.cameraUp[2]     = up.z;
    cb.tanHalfFovY     = tanHalfFovY;
    cb.cameraLook[0]   = look.x;
    cb.cameraLook[1]   = look.y;
    cb.cameraLook[2]   = look.z;

    cmd->SetGraphicsRoot32BitConstants(
        kRootConstants, static_cast<UINT>(sizeof(SkyFrameConstants) / 4), &cb, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

} // namespace Dark
