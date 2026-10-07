#include "Render/SkyPipeline.h"
#include "Render/Profile.h"
#include "Render/DepthState.h"
#include "Render/ShaderCompile.h"
#include "Render/Camera3D.h"
#include "Render/Fog.h"
#include "Render/ShadowSystem.h"
#include "Core/Log.h"
#include "Sky/CloudLayer.h"

#include <cmath>
#include <cstring>

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

void makeSkyMacros(bool encodeSrgb, const char* cloudLayer, D3D_SHADER_MACRO out[3])
{
    out[0].Name       = "ENCODE_SRGB";
    out[0].Definition = encodeSrgb ? "1" : "0";
    out[1].Name       = "CLOUD_LAYER";
    out[1].Definition = cloudLayer;
    out[2].Name       = nullptr;
    out[2].Definition = nullptr;
}

bool createSkyPso(ID3D12Device* device, ID3D12RootSignature* root, ID3DBlob* vs, ID3DBlob* ps, bool deferredLast, DXGI_FORMAT colorFormat, ComPtr<ID3D12PipelineState>& out)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root;
    psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.SampleMask     = UINT_MAX;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable    = deferredLast ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc      = deferredLast ? skyDepthFunc() : D3D12_COMPARISON_FUNC_ALWAYS;

    psoDesc.InputLayout           = { nullptr, 0 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = colorFormat;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    return !FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out)), "CreateGraphicsPipelineState (sky)");
}

} // namespace

bool SkyPipeline::create(ID3D12Device* device, SkyPass pass, DXGI_FORMAT colorFormat)
{
    m_rootSignature.Reset();
    m_pso.Reset();
    m_psoCloud.Reset();
    m_shadowHeap.Reset();
    m_shadowGpu   = {};
    m_skyCb.Reset();
    m_skyMapped = nullptr;
    m_skyGpu    = 0;
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "SkyPipeline::create: null device");
        return false;
    }
    const bool deferredLast = pass == SkyPass::DeferredLast;

    D3D12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors                    = 1;
    shadowRange.BaseShaderRegister                = 0;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[kRootParameterCount]{};
    rootParams[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootCbv].Descriptor.ShaderRegister = 0;
    rootParams[kRootCbv].Descriptor.RegisterSpace  = 0;

    rootParams[kRootShadowCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kRootShadowCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootShadowCbv].Descriptor.ShaderRegister = 1;

    rootParams[kRootShadowSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootShadowSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootShadowSrv].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootShadowSrv].DescriptorTable.pDescriptorRanges   = &shadowRange;

    D3D12_STATIC_SAMPLER_DESC shadowSamp{};
    shadowSamp.Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSamp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.ComparisonFunc   = shadowCmpFunc();
    shadowSamp.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowSamp.MaxLOD           = D3D12_FLOAT32_MAX;
    shadowSamp.ShaderRegister   = 1;
    shadowSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = kRootParameterCount;
    rsDesc.pParameters       = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &shadowSamp;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (sky)"))
    {
        if (rsErr)
            DE_LOG_ERROR(LogCategory::Render, "Sky root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }

    if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (sky)"))
        return false;

    const bool encode = encodeSrgbForColorFormat(colorFormat);
    D3D_SHADER_MACRO plainMacros[3];
    D3D_SHADER_MACRO cloudMacros[3];
    makeSkyMacros(encode, "0", plainMacros);
    makeSkyMacros(encode, "1", cloudMacros);

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> psCloud;
    const char* vsEntry = deferredLast ? "VSMainDeferred" : "VSMain";
    // VS stays on CLOUD_LAYER 0 so a broken deck cannot take the sun disc with it.
    if (!compileShaderFromContent("shaders/Sky.hlsl", vsEntry, "vs_5_0", vs, plainMacros)
        || !compileShaderFromContent("shaders/Sky.hlsl", "PSMain", "ps_5_0", ps, plainMacros))
    {
        m_rootSignature.Reset();
        return false;
    }
    if (!compileShaderFromContent("shaders/Sky.hlsl", "PSMain", "ps_5_0", psCloud, cloudMacros))
    {
        DE_LOG_ERROR(LogCategory::Render, "SkyPipeline: cloud layer shader failed; analytic sky stays");
        psCloud.Reset();
    }

    if (!createSkyPso(device, m_rootSignature.Get(), vs.Get(), ps.Get(), deferredLast, colorFormat, m_pso))
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        return false;
    }
    if (psCloud && !createSkyPso(device, m_rootSignature.Get(), vs.Get(), psCloud.Get(), deferredLast, colorFormat, m_psoCloud))
    {
        DE_LOG_ERROR(LogCategory::Render, "SkyPipeline: cloud layer PSO failed; analytic sky stays");
        m_psoCloud.Reset();
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_shadowHeap)), "CreateDescriptorHeap (sky shadow)"))
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        m_psoCloud.Reset();
        return false;
    }
    m_shadowGpu = m_shadowHeap->GetGPUDescriptorHandleForHeapStart();

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC skyBufferDesc{};
    skyBufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    skyBufferDesc.Width            = static_cast<UINT64>(kSkySlotBytes) * kFrameCount;
    skyBufferDesc.Height           = 1;
    skyBufferDesc.DepthOrArraySize = 1;
    skyBufferDesc.MipLevels        = 1;
    skyBufferDesc.SampleDesc.Count = 1;
    skyBufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &skyBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skyCb)),
                 "CreateCommittedResource (sky cbuffer)"))
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        m_psoCloud.Reset();
        m_shadowHeap.Reset();
        return false;
    }

    const D3D12_RANGE readRange{ 0, 0 };
    if (FailedHr(m_skyCb->Map(0, &readRange, reinterpret_cast<void**>(&m_skyMapped)), "Map (sky cbuffer)"))
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        m_psoCloud.Reset();
        m_shadowHeap.Reset();
        m_skyCb.Reset();
        m_skyMapped = nullptr;
        return false;
    }
    std::memset(m_skyMapped, 0, static_cast<size_t>(kSkySlotBytes) * kFrameCount);
    m_skyGpu = m_skyCb->GetGPUVirtualAddress();

    DE_LOG_INFO(LogCategory::Render, "SkyPipeline: ready ({}, deck {})", deferredLast ? "DeferredLast" : "ForwardFirst", m_psoCloud ? "on" : "off");
    return true;
}

void SkyPipeline::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
{
    if (!device || !m_shadowHeap || shadowCpu.ptr == 0)
        return;
    device->CopyDescriptorsSimple(1, m_shadowHeap->GetCPUDescriptorHandleForHeapStart(), shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void SkyPipeline::bind(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !m_pso)
        return;
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_psoCloud ? m_psoCloud.Get() : m_pso.Get());
}

void SkyPipeline::upload(uint32_t frameIndex, const Camera3D& camera, const Sky::Environment& env, float exposure, float waterLevel, float fogScale)
{
    if (!m_skyMapped)
        return;

    const Math::Vector3f cam   = camera.GetPosition();
    const Math::Vector3f right = camera.GetRight();
    const Math::Vector3f up    = camera.GetUp();
    const Math::Vector3f look  = camera.GetLook();
    const float tanHalfFovY    = tanf(0.5f * camera.GetFovY());
    const float aspect         = camera.GetAspect() > 1.0e-4f ? camera.GetAspect() : 1.0f;
    const float tanHalfFovX    = tanHalfFovY * aspect;

    SkyFrameConstants cb{};
    cb.cameraPos[0] = cam.x;
    cb.cameraPos[1] = cam.y;
    cb.cameraPos[2] = cam.z;
    cb.coverage     = env.weather.cloudCoverage;
    cb.sunDir[0]    = env.sunDir().x;
    cb.sunDir[1]    = env.sunDir().y;
    cb.sunDir[2]    = env.sunDir().z;
    cb.turbidity    = env.weather.turbidity;
    cb.sunColor[0]  = env.sunColor().x;
    cb.sunColor[1]  = env.sunColor().y;
    cb.sunColor[2]  = env.sunColor().z;
    cb.cloudTime    = env.cloudClockSec();
    cb.moonDir[0]   = env.moonDir().x;
    cb.moonDir[1]   = env.moonDir().y;
    cb.moonDir[2]   = env.moonDir().z;
    cb.windSpeed    = env.weather.windSpeed;
    cb.moonColor[0] = env.moonColor().x;
    cb.moonColor[1] = env.moonColor().y;
    cb.moonColor[2] = env.moonColor().z;
    cb.rain         = env.weather.rain;
    cb.windDir[0]   = env.weather.windDir.x;
    cb.windDir[1]   = env.weather.windDir.y;
    cb.sunElevation = env.sunElevation();
    cb.exposure     = exposure >= 0.0f ? exposure : env.exposure();
    cb.cameraRight[0] = right.x;
    cb.cameraRight[1] = right.y;
    cb.cameraRight[2] = right.z;
    cb.tanHalfFovX    = tanHalfFovX;
    cb.cameraUp[0]    = up.x;
    cb.cameraUp[1]    = up.y;
    cb.cameraUp[2]    = up.z;
    cb.tanHalfFovY    = tanHalfFovY;
    cb.cameraLook[0]  = look.x;
    cb.cameraLook[1]  = look.y;
    cb.cameraLook[2]  = look.z;
    const FogGpu fog        = makeFogGpu(&env, waterLevel, fogScale > 0.0f);
    cb.fogDensity           = fog.fogDensity;
    cb.fogColor[0]          = fog.fogColor[0];
    cb.fogColor[1]          = fog.fogColor[1];
    cb.fogColor[2]          = fog.fogColor[2];
    cb.heightFogDensity     = fog.heightFogDensity;
    cb.lightColor[0]        = env.lightColor().x;
    cb.lightColor[1]        = env.lightColor().y;
    cb.lightColor[2]        = env.lightColor().z;
    cb.heightFogFalloff     = fog.heightFogFalloff;
    cb.ambientColor[0]      = env.ambientColor().x;
    cb.ambientColor[1]      = env.ambientColor().y;
    cb.ambientColor[2]      = env.ambientColor().z;
    cb.heightFogHeight      = fog.heightFogHeight;
    cb.volumetricFogDensity = fog.volumetricFogDensity;
    cb.volumetricHeight     = fog.volumetricHeight;
    cb.waterLevel           = waterLevel;
    cb.fogScale             = fogScale;
    cb.fogLightDir[0]       = env.lightDir().x;
    cb.fogLightDir[1]       = env.lightDir().y;
    cb.fogLightDir[2]       = env.lightDir().z;

    Sky::CloudLayerGpu layer{};
    Sky::writeCloudLayer(layer, env);
    std::memcpy(reinterpret_cast<uint8_t*>(&cb) + offsetof(SkyFrameConstants, clLightColor), &layer, sizeof(layer));

    const UINT slot = frameIndex % kFrameCount;
    std::memcpy(m_skyMapped + static_cast<size_t>(slot) * kSkySlotBytes, &cb, sizeof(cb));
}

void SkyPipeline::draw(ID3D12GraphicsCommandList* cmd, uint32_t frameIndex, const ShadowSystem* shadows) const
{
    if (!cmd || !m_pso || !m_skyMapped)
        return;

    const GpuScope sky(cmd, "Sky", ProfileColor::Sky);
    bind(cmd);

    const UINT slot = frameIndex % kFrameCount;
    cmd->SetGraphicsRootConstantBufferView(kRootCbv, m_skyGpu + static_cast<UINT64>(slot) * kSkySlotBytes);
    if (m_shadowHeap && m_shadowGpu.ptr != 0)
    {
        ID3D12DescriptorHeap* heaps[] = { m_shadowHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(kRootShadowSrv, m_shadowGpu);
    }
    if (shadows && shadows->isValid())
        shadows->bindReceiverCbv(cmd, kRootShadowCbv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

} // namespace Dark
