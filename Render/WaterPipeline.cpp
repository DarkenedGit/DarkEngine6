#include "Render/WaterPipeline.h"
#include "Render/PsoUtil.h"
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
    DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

} // namespace

bool WaterPipeline::create(ID3D12Device* device, DXGI_FORMAT colorFormat)
{
    m_rootSignature.Reset();
    m_psoSolid.Reset();
    m_psoWire.Reset();
    m_psoPoint.Reset();
    m_cbUpload.Reset();
    m_dummyLights.Reset();
    m_cbMapped = nullptr;
    m_cbGpu    = 0;
    m_dummyGpu = 0;
    m_cbSlot   = 0;
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "WaterPipeline::create: null device");
        return false;
    }

    D3D12_ROOT_PARAMETER rootParams[2]{};
    rootParams[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootCbv].Descriptor.ShaderRegister = 0;
    rootParams[kRootCbv].Descriptor.RegisterSpace  = 0;

    rootParams[kRootLightsSrv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[kRootLightsSrv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootLightsSrv].Descriptor.ShaderRegister = 0;
    rootParams[kRootLightsSrv].Descriptor.RegisterSpace  = 0;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 2;
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
            DE_LOG_ERROR(LogCategory::Render, "Water root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
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
    psoDesc.RTVFormats[0]         = colorFormat;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    if (!createFillVariantPsos(device, psoDesc, m_psoSolid, m_psoWire, m_psoPoint))
    {
        m_rootSignature.Reset();
        return false;
    }

    if (!createConstantBuffers(device))
    {
        m_rootSignature.Reset();
        m_psoSolid.Reset();
        m_psoWire.Reset();
        m_psoPoint.Reset();
        return false;
    }

    DE_LOG_INFO(LogCategory::Render, "WaterPipeline: ready (Gerstner + GGX, CBV + 8 local lights)");
    return true;
}

bool WaterPipeline::createConstantBuffers(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc{};
    cbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width            = static_cast<UINT64>(cbBytes()) * kBufferedFrames;
    cbDesc.Height           = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels        = 1;
    cbDesc.SampleDesc       = { 1, 0 };
    cbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FailedHr(
            device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbUpload)),
            "CreateCommittedResource water CBV"))
    {
        return false;
    }

    D3D12_RESOURCE_DESC dummyDesc = cbDesc;
    dummyDesc.Width               = 64;
    if (FailedHr(
            device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &dummyDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_dummyLights)),
            "CreateCommittedResource water dummy lights"))
    {
        m_cbUpload.Reset();
        return false;
    }

    if (FailedHr(m_cbUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped)), "Map water CBV"))
    {
        m_cbUpload.Reset();
        m_dummyLights.Reset();
        m_cbMapped = nullptr;
        return false;
    }

    UINT8* dummyMapped = nullptr;
    if (FailedHr(m_dummyLights->Map(0, nullptr, reinterpret_cast<void**>(&dummyMapped)), "Map water dummy lights"))
    {
        m_cbUpload.Reset();
        m_dummyLights.Reset();
        m_cbMapped = nullptr;
        return false;
    }
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes()) * kBufferedFrames);
    std::memset(dummyMapped, 0, 64);

    m_cbGpu    = m_cbUpload->GetGPUVirtualAddress();
    m_dummyGpu = m_dummyLights->GetGPUVirtualAddress();
    return true;
}

UINT WaterPipeline::cbBytes() const
{
    return (static_cast<UINT>(sizeof(WaterFrameConstants)) + 255u) & ~255u;
}

void WaterPipeline::bind(ID3D12GraphicsCommandList* cmd, DebugFill fill) const
{
    ID3D12PipelineState* pso = selectFillPso(fill, m_psoSolid.Get(), m_psoWire.Get(), m_psoPoint.Get());
    if (!cmd || !pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(pso);
}

void WaterPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const WaterFrameConstants& constants, uint32_t frameIndex)
{
    if (!cmd || !m_cbMapped || !m_cbGpu)
        return;
    m_cbSlot = frameIndex % kBufferedFrames;
    std::memcpy(m_cbMapped + static_cast<size_t>(m_cbSlot) * cbBytes(), &constants, sizeof(constants));
    cmd->SetGraphicsRootConstantBufferView(kRootCbv, m_cbGpu + static_cast<UINT64>(m_cbSlot) * cbBytes());
}

void WaterPipeline::setLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS lightsVa) const
{
    if (!cmd)
        return;
    const D3D12_GPU_VIRTUAL_ADDRESS va = lightsVa ? lightsVa : m_dummyGpu;
    if (!va)
        return;
    cmd->SetGraphicsRootShaderResourceView(kRootLightsSrv, va);
}

void WaterPipeline::fillConstants(
    WaterFrameConstants& out,
    const float worldViewProj[16],
    const float cameraPos[3],
    float time,
    const float lightDir[3],
    const WaterParams& params,
    const Sky::Environment* env,
    bool lighting)
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

    for (int i = 0; i < kWaterWaveCount; ++i)
    {
        const Math::Vector2f d = waveDirection(params, i);
        out.waves[i][0] = d.x;
        out.waves[i][1] = d.y;
        out.waves[i][2] = params.waves[i].frequency;
        out.waves[i][3] = params.waves[i].amplitude;
        out.waveSpeed[i] = params.waves[i].speed;
    }

    if (!lighting)
        out.specPower = -1.0f;

    out.lightCount = 0;
    for (uint32_t i = 0; i < kWaterLocalLightMax; ++i)
        out.waterIndex[i] = 0;
}

} // namespace Dark
