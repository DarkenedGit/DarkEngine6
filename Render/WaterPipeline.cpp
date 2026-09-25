#include "Render/WaterPipeline.h"
#include "Render/Camera3D.h"
#include "Render/DepthState.h"
#include "Render/PsoUtil.h"
#include "Render/ShaderCompile.h"
#include "Render/Fog.h"
#include "Core/Log.h"
#include "Math/Matrix4f.h"
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

void copyMatrix(float dst[16], const Math::Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

void fillSkyEval(SkyEvalParams& out, const Sky::Environment* env)
{
    out = {};
    if (!env)
        return;
    out.sunDir[0]     = env->sunDir().x;
    out.sunDir[1]     = env->sunDir().y;
    out.sunDir[2]     = env->sunDir().z;
    out.coverage      = env->weather.cloudCoverage;
    out.sunColor[0]   = env->sunColor().x;
    out.sunColor[1]   = env->sunColor().y;
    out.sunColor[2]   = env->sunColor().z;
    out.turbidity     = env->weather.turbidity;
    out.moonDir[0]    = env->moonDir().x;
    out.moonDir[1]    = env->moonDir().y;
    out.moonDir[2]    = env->moonDir().z;
    out.rain          = env->weather.rain;
    out.moonColor[0]  = env->moonColor().x;
    out.moonColor[1]  = env->moonColor().y;
    out.moonColor[2]  = env->moonColor().z;
    out.windSpeed     = env->weather.windSpeed;
    out.windDir[0]    = env->weather.windDir.x;
    out.windDir[1]    = env->weather.windDir.y;
    out.sunElevation  = env->sunElevation();
    out.exposure      = 1.0f; // HybridDeferred sky pass: tonemap owns exposure
    out.cloudTime     = env->timeOfDay;
}

void writeTex2dSrv(ID3D12Device* device, ID3D12Resource* res, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                     = format;
    srv.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels        = 1;
    device->CreateShaderResourceView(res, &srv, dest);
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
    m_srvHeap.Reset();
    m_dummySsrColor.Reset();
    m_dummySsrDepth.Reset();
    m_device      = nullptr;
    m_cbMapped    = nullptr;
    m_cbGpu       = 0;
    m_dummyGpu    = 0;
    m_heightGpu   = {};
    m_shadowGpu   = {};
    m_ssrGpu      = {};
    m_srvIncr     = 0;
    m_cbSlot      = 0;
    m_haveHeight  = false;
    m_haveShadow  = false;
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "WaterPipeline::create: null device");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE heightRange{};
    heightRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    heightRange.NumDescriptors                    = 1;
    heightRange.BaseShaderRegister                = 1;
    heightRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors                    = 1;
    shadowRange.BaseShaderRegister                = 2;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE ssrRange{};
    ssrRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ssrRange.NumDescriptors                    = 2;
    ssrRange.BaseShaderRegister                = 3;
    ssrRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[6]{};
    rootParams[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootCbv].Descriptor.ShaderRegister = 0;
    rootParams[kRootCbv].Descriptor.RegisterSpace  = 0;

    rootParams[kRootLightsSrv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[kRootLightsSrv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[kRootLightsSrv].Descriptor.ShaderRegister = 0;
    rootParams[kRootLightsSrv].Descriptor.RegisterSpace  = 0;

    rootParams[kRootHeightSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootHeightSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootHeightSrv].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootHeightSrv].DescriptorTable.pDescriptorRanges   = &heightRange;

    rootParams[kRootShadowCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[kRootShadowCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootShadowCbv].Descriptor.ShaderRegister = 1;

    rootParams[kRootShadowSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootShadowSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootShadowSrv].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootShadowSrv].DescriptorTable.pDescriptorRanges   = &shadowRange;

    rootParams[kRootSsrSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootSsrSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootSsrSrv].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootSsrSrv].DescriptorTable.pDescriptorRanges   = &ssrRange;

    D3D12_STATIC_SAMPLER_DESC samps[3]{};
    samps[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samps[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[0].MaxLOD           = D3D12_FLOAT32_MAX;
    samps[0].ShaderRegister   = 0;
    samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samps[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samps[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samps[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samps[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samps[1].ComparisonFunc   = shadowCmpFunc();
    samps[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samps[1].MaxLOD           = D3D12_FLOAT32_MAX;
    samps[1].ShaderRegister   = 1;
    samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samps[2].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samps[2].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[2].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[2].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samps[2].MaxLOD           = D3D12_FLOAT32_MAX;
    samps[2].ShaderRegister   = 2;
    samps[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 6;
    rsDesc.pParameters       = rootParams;
    rsDesc.NumStaticSamplers = 3;
    rsDesc.pStaticSamplers   = samps;
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
    D3D_SHADER_MACRO encodeMacros[2];
    makeEncodeSrgbMacros(encodeSrgbForColorFormat(colorFormat), encodeMacros);
    if (!compileShaderFromContent("shaders/Water.hlsl", "VSMain", "vs_5_0", vs, encodeMacros)
        || !compileShaderFromContent("shaders/Water.hlsl", "PSMain", "ps_5_0", ps, encodeMacros))
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
    psoDesc.DepthStencilState.DepthFunc      = sceneDepthFuncGreaterEqual();
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

    m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 4;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap (water height+shadow+ssr)"))
    {
        m_rootSignature.Reset();
        m_psoSolid.Reset();
        m_psoWire.Reset();
        m_psoPoint.Reset();
        return false;
    }
    m_heightGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_shadowGpu = m_heightGpu;
    m_shadowGpu.ptr += m_srvIncr;
    m_ssrGpu = m_shadowGpu;
    m_ssrGpu.ptr += m_srvIncr;
    if (!createSsrDummies(device))
    {
        m_rootSignature.Reset();
        m_psoSolid.Reset();
        m_psoWire.Reset();
        m_psoPoint.Reset();
        m_cbUpload.Reset();
        m_dummyLights.Reset();
        m_cbMapped = nullptr;
        m_srvHeap.Reset();
        m_dummySsrColor.Reset();
        m_dummySsrDepth.Reset();
        return false;
    }

    m_device = device;
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

bool WaterPipeline::createSsrDummies(ID3D12Device* device)
{
    m_dummySsrColor.Reset();
    m_dummySsrDepth.Reset();
    if (!device)
        return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = 1;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc       = { 1, 0 };
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FailedHr(
            device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_dummySsrColor)),
            "CreateCommittedResource water SSR color dummy"))
    {
        return false;
    }
    rd.Format = DXGI_FORMAT_R32_FLOAT;
    if (FailedHr(
            device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_dummySsrDepth)),
            "CreateCommittedResource water SSR depth dummy"))
    {
        m_dummySsrColor.Reset();
        return false;
    }
    m_dummySsrColor->SetName(L"DE.Water.SsrDummyColor");
    m_dummySsrDepth->SetName(L"DE.Water.SsrDummyDepth");
    packSsrDummySrvs(device);
    return true;
}

void WaterPipeline::packSsrDummySrvs(ID3D12Device* device)
{
    if (!device || !m_srvHeap || !m_dummySsrColor || !m_dummySsrDepth)
        return;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += static_cast<SIZE_T>(m_srvIncr) * 2u;
    writeTex2dSrv(device, m_dummySsrColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, dst);
    dst.ptr += m_srvIncr;
    writeTex2dSrv(device, m_dummySsrDepth.Get(), DXGI_FORMAT_R32_FLOAT, dst);
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

void WaterPipeline::setHeightMap(ID3D12GraphicsCommandList* cmd, ID3D12DescriptorHeap* heap, D3D12_GPU_DESCRIPTOR_HANDLE gpu) const
{
    if (!cmd || !heap || gpu.ptr == 0)
        return;
    ID3D12DescriptorHeap* heaps[] = { heap };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootDescriptorTable(kRootHeightSrv, gpu);
}

void WaterPipeline::setHeightSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE heightCpu)
{
    if (!device || !m_srvHeap || heightCpu.ptr == 0)
        return;
    device->CopyDescriptorsSimple(1, m_srvHeap->GetCPUDescriptorHandleForHeapStart(), heightCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_haveHeight = true;
}

void WaterPipeline::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
{
    if (!device || !m_srvHeap || shadowCpu.ptr == 0)
        return;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += m_srvIncr;
    device->CopyDescriptorsSimple(1, dst, shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_haveShadow = true;
}

void WaterPipeline::setSsrSrvs(D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCpu, D3D12_CPU_DESCRIPTOR_HANDLE depthCpu)
{
    if (!m_device || !m_srvHeap)
        return;
    if (sceneColorCpu.ptr == 0 || depthCpu.ptr == 0)
    {
        packSsrDummySrvs(m_device);
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += static_cast<SIZE_T>(m_srvIncr) * 2u;
    m_device->CopyDescriptorsSimple(1, dst, sceneColorCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst.ptr += m_srvIncr;
    m_device->CopyDescriptorsSimple(1, dst, depthCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void WaterPipeline::bindReceiverSrvs(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !hasReceiverSrvs())
        return;
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootDescriptorTable(kRootHeightSrv, m_heightGpu);
    cmd->SetGraphicsRootDescriptorTable(kRootShadowSrv, m_shadowGpu);
    cmd->SetGraphicsRootDescriptorTable(kRootSsrSrv, m_ssrGpu);
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
        const FogGpu fog      = makeFogGpu(env, params.waterLevel, lighting);
        out.fogColor[0]       = fog.fogColor[0];
        out.fogColor[1]       = fog.fogColor[1];
        out.fogColor[2]       = fog.fogColor[2];
        out.fogDensity        = fog.fogDensity;
        out.heightFogDensity  = fog.heightFogDensity;
        out.heightFogFalloff  = fog.heightFogFalloff;
        out.heightFogHeight   = fog.heightFogHeight;
        out.volumetricFogDensity = fog.volumetricFogDensity;
        out.volumetricHeight  = fog.volumetricHeight;
        out.lightColor[0]     = env->lightColor().x;
        out.lightColor[1]     = env->lightColor().y;
        out.lightColor[2]     = env->lightColor().z;
        out.ambientColor[0]   = env->ambientColor().x;
        out.ambientColor[1]   = env->ambientColor().y;
        out.ambientColor[2]   = env->ambientColor().z;
    }

    for (int i = 0; i < kWaterWaveCount; ++i)
    {
        const Math::Vector2f d = waveDirection(params, i);
        out.waves[i][0] = d.x;
        out.waves[i][1] = d.y;
        out.waves[i][2] = params.waves[i].frequency;
        out.waves[i][3] = scaledWaveAmplitude(params, i);
        out.waveSpeed[i] = scaledWaveSpeed(params, i);
    }

    if (!lighting)
        out.specPower = -1.0f;

    out.lightCount = 0;
    for (uint32_t i = 0; i < kWaterLocalLightMax; ++i)
        out.waterIndex[i] = 0;
}

void WaterPipeline::fillSsr(
    WaterFrameConstants& out,
    const Camera3D& camera,
    const SsrSettings* settings,
    bool debugEnabled,
    bool hasSceneColor,
    const Sky::Environment* env)
{
    const Math::Matrix4f viewProj = camera.GetViewProj();
    copyMatrix(out.invViewProj, viewProj.Inverse());
    copyMatrix(out.viewProj, viewProj);
    out.nearZ        = camera.GetNearZ();
    const bool on    = settings && settings->enabled && debugEnabled && hasSceneColor;
    out.ssrEnabled   = on ? 1.0f : 0.0f;
    const SsrSettings defaults{};
    const SsrSettings& s = settings ? *settings : defaults;
    out.thickness    = s.thickness;
    out.stride       = s.stride;
    out.edgeFade     = s.edgeFade;
    out.maxRoughness = s.maxRoughness;
    out.ssrPad0      = 0.0f;
    out.ssrPad1      = 0.0f;
    fillSkyEval(out.skyEval, env);
}

} // namespace Dark
