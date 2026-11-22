#include "Render/AutoExposure.h"
#include "Render/Profile.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/GraphTargets.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

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

const char* const kLevelNames[AutoExposurePipeline::kMaxLevels] = {
    "DE.AutoExposure.L0",  "DE.AutoExposure.L1",  "DE.AutoExposure.L2",  "DE.AutoExposure.L3",
    "DE.AutoExposure.L4",  "DE.AutoExposure.L5",  "DE.AutoExposure.L6",  "DE.AutoExposure.L7",
    "DE.AutoExposure.L8",  "DE.AutoExposure.L9",  "DE.AutoExposure.L10", "DE.AutoExposure.L11",
    "DE.AutoExposure.L12", "DE.AutoExposure.L13", "DE.AutoExposure.L14", "DE.AutoExposure.L15",
};

bool createSrvRoot(ID3D12Device* device, UINT srvCount, UINT constantCount, const char* label, ComPtr<ID3D12RootSignature>& out)
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = srvCount;
    range.BaseShaderRegister                = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = constantCount;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &range;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = params;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), label))
    {
        if (rsErr)
            DE_LOG_ERROR(LogCategory::Render, "AutoExposure RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }
    return !FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&out)), label);
}

bool createPso(ID3D12Device* device, ID3D12RootSignature* root, ID3DBlob* vs, ID3DBlob* ps, const char* label, ComPtr<ID3D12PipelineState>& out)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature                                   = root;
    pso.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.SampleMask                                       = UINT_MAX;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable                  = TRUE;
    pso.DepthStencilState.DepthEnable                    = FALSE;
    pso.DepthStencilState.StencilEnable                  = FALSE;
    pso.InputLayout                                      = { nullptr, 0 };
    pso.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets                                 = 1;
    pso.RTVFormats[0]                                    = DXGI_FORMAT_R32G32_FLOAT;
    pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc                                       = { 1, 0 };
    return !FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out)), label);
}

} // namespace

float measuredLumaFromSums(float logSum, float weight)
{
    if (!(weight > 1.0e-6f))
        return 0.0f;
    return std::exp2(logSum / weight);
}

float logAverageLuma(const LumaSample* samples, int count)
{
    if (!samples || count <= 0)
        return 0.0f;
    float logSum = 0.0f;
    float weight = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float w = samples[i].weight;
        if (!(w > 0.0f))
            continue;
        const float luma = samples[i].luma > 1.0e-4f ? samples[i].luma : 1.0e-4f;
        logSum += std::log2(luma) * w;
        weight += w;
    }
    return measuredLumaFromSums(logSum, weight);
}

AutoExposureResult adaptExposure(AutoExposureState& state, float measuredLuma, float envExposure, float dt, const AutoExposureSettings& settings)
{
    AutoExposureResult result;
    result.measuredLuma = measuredLuma > 0.0f ? measuredLuma : 0.0f;

    if (settings.mode != ExposureMode::Auto)
    {
        result.evCorrection  = 0.0f;
        result.finalExposure = envExposure;
        return result;
    }

    if (!(measuredLuma > 0.0f))
    {
        result.evCorrection  = state.ev;
        result.finalExposure = envExposure * std::exp2(state.ev);
        return result;
    }

    const float grey     = settings.targetGrey > 1.0e-4f ? settings.targetGrey : 1.0e-4f;
    const float measured = measuredLuma > 1.0e-4f ? measuredLuma : 1.0e-4f;
    const float evTarget = std::log2(grey / measured) + settings.evBias;
    const float rawSpeed = evTarget < state.ev ? settings.adaptBright : settings.adaptDark;
    const float speed    = rawSpeed > 0.0f ? rawSpeed : 0.0f;
    const float stepDt   = dt > 0.0f ? dt : 0.0f;
    const float t        = 1.0f - std::exp(-stepDt * speed);
    state.ev             = Math::Lerp(state.ev, evTarget, t);
    const float maxEv    = settings.maxEv > 0.0f ? settings.maxEv : 0.0f;
    state.ev             = Math::Clamp(state.ev, -maxEv, maxEv);

    result.evCorrection  = state.ev;
    result.finalExposure = envExposure * std::exp2(state.ev);
    return result;
}

AutoExposurePipeline::~AutoExposurePipeline()
{
    release();
}

void AutoExposurePipeline::release()
{
    resetTargets();
    m_targets = nullptr;
    m_psoReduce.Reset();
    m_psoDownsample.Reset();
    m_rootReduce.Reset();
    m_rootDownsample.Reset();
    m_loggedSkip = false;
}

void AutoExposurePipeline::resetTargets()
{
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        if (m_readback[i] && m_readbackMapped[i])
            m_readback[i].get()->Unmap(0, nullptr);
        m_readbackMapped[i] = nullptr;
        // The GPU may still copy into it: Renderer defers the release (resource and its D3D12MA
        // allocation) behind the frame fence.
        if (m_renderer && m_readback[i])
        {
            m_renderer->deferRelease(m_readback[i].get());
            if (IUnknown* allocation = m_readback[i].allocation())
                m_renderer->deferRelease(allocation);
        }
        m_readback[i].reset();
        m_slotWritten[i] = false;
    }
    for (GraphTarget& level : m_levels)
    {
        if (m_targets)
            m_targets->unregisterTarget(level);
        level = {};
    }
    m_srvHeap.Reset();
    m_srvCpu       = {};
    m_srvGpu       = {};
    m_viewsWritten = false;
}

bool AutoExposurePipeline::isValid() const
{
    return m_psoReduce != nullptr && m_targets != nullptr && m_targets->resource(m_levels[0].id) != nullptr && m_readback[0];
}

uint64_t AutoExposurePipeline::generation() const
{
    return m_targets ? m_targets->generation(m_levels[0].id) : 0;
}

uint32_t AutoExposurePipeline::levelCount() const
{
    // Level k is max(floor(w / (4 * 2^k)), 1); the pyramid ends at the first 1x1 level.
    for (UINT i = 0; i < kMaxLevels; ++i)
        if (levelWidth(i) <= 1u && levelHeight(i) <= 1u)
            return i + 1u;
    return 0; // never reaches 1x1: meter() refuses
}

uint32_t AutoExposurePipeline::levelWidth(UINT index) const
{
    return m_targets->width(m_levels[index].id);
}

uint32_t AutoExposurePipeline::levelHeight(UINT index) const
{
    return m_targets->height(m_levels[index].id);
}

void AutoExposurePipeline::refreshViews() const
{
    // Level SRVs sit at fixed slots of the shader-visible table; a new generation only happens
    // inside Renderer::resize's drained window, so rewriting them here races no in-flight list.
    bool recreated = false;
    for (GraphTarget& level : m_levels)
        recreated = m_targets->refresh(level) || recreated;
    if (!recreated && m_viewsWritten)
        return;
    ID3D12Device* device = m_targets->device();
    for (UINT i = 0; i < kMaxLevels; ++i)
        device->CopyDescriptorsSimple(1, srvCpu(kFrameSrv + i), m_targets->srv(m_levels[i].id), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_viewsWritten = true;
}

void AutoExposurePipeline::invalidateReadback()
{
    m_slotWritten[0] = false;
    m_slotWritten[1] = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE AutoExposurePipeline::srvCpu(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvCpu;
    h.ptr += static_cast<SIZE_T>(index) * m_srvIncr;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE AutoExposurePipeline::srvGpu(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu;
    h.ptr += static_cast<SIZE_T>(index) * m_srvIncr;
    return h;
}

void AutoExposurePipeline::transitionLevel(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const
{
    if (index < kMaxLevels)
        m_targets->transition(cmd, m_levels[index], after);
}

bool AutoExposurePipeline::createPsos(ID3D12Device* device)
{
    m_rootReduce.Reset();
    m_rootDownsample.Reset();
    m_psoReduce.Reset();
    m_psoDownsample.Reset();

    if (!createSrvRoot(device, 2, kConstantCount, "CreateRootSignature (auto exposure reduce)", m_rootReduce))
        return false;
    if (!createSrvRoot(device, 1, kConstantCount, "CreateRootSignature (auto exposure downsample)", m_rootDownsample))
    {
        m_rootReduce.Reset();
        return false;
    }

    const D3D_SHADER_MACRO reduceMacros[] = {
        { "AE_REDUCE", "1" },
        { nullptr, nullptr },
    };
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psReduce;
    ComPtr<ID3DBlob> psDown;
    if (!compileShaderFromContent("shaders/AutoExposure.hlsl", "VSMain", ShaderStage::Vertex, vs)
        || !compileShaderFromContent("shaders/AutoExposure.hlsl", "PSReduce", ShaderStage::Pixel, psReduce, reduceMacros)
        || !compileShaderFromContent("shaders/AutoExposure.hlsl", "PSDownsample", ShaderStage::Pixel, psDown))
    {
        m_rootReduce.Reset();
        m_rootDownsample.Reset();
        return false;
    }

    if (!createPso(device, m_rootReduce.Get(), vs.Get(), psReduce.Get(), "CreateGraphicsPipelineState (auto exposure reduce)", m_psoReduce))
    {
        m_rootReduce.Reset();
        m_rootDownsample.Reset();
        return false;
    }
    if (!createPso(device, m_rootDownsample.Get(), vs.Get(), psDown.Get(), "CreateGraphicsPipelineState (auto exposure downsample)", m_psoDownsample))
    {
        m_psoReduce.Reset();
        m_rootReduce.Reset();
        m_rootDownsample.Reset();
        return false;
    }
    return true;
}

bool AutoExposurePipeline::createTargets(ID3D12Device* device)
{
    resetTargets();

    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = DXGI_FORMAT_R32G32_FLOAT;
    const bool formatOk = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))
        && (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0
        && (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
    if (!formatOk)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline: R32G32_FLOAT is not supported as a render target");
        return false;
    }

    m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = kFrameSrv + kMaxLevels;
    srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap auto exposure SRV"))
    {
        resetTargets();
        return false;
    }
    m_srvHeap->SetName(L"DE.AutoExposure.Tables");
    m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    // Every level is registered (output-relative, max(floor(w / (4 * 2^k)), 1)) so a resize never
    // changes the registry; levels past the first 1x1 stay 1x1 and are not drawn.
    const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (UINT i = 0; i < kMaxLevels; ++i)
    {
        const Rg::TargetDesc desc = colorTargetDesc(DXGI_FORMAT_R32G32_FLOAT, Rg::TargetSize::OutputFloor, 4u << i, D3D12_RESOURCE_STATE_RENDER_TARGET, zero4);
        m_levels[i] = m_targets->registerTarget(kLevelNames[i], desc);
        if (!m_levels[i].id.valid())
        {
            resetTargets();
            return false;
        }
    }
    if (levelCount() == 0)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline: pyramid did not reach 1x1 ({}x{})", levelWidth(0), levelHeight(0));
        resetTargets();
        return false;
    }

    // Size independent: survives every resize (the host still drops the samples, see invalidateReadback).
    const wchar_t* readNames[kFrameCount] = { L"DE.AutoExposure.Read0", L"DE.AutoExposure.Read1" };
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        if (!m_targets->allocator().createBuffer(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_FLAG_NONE, readNames[i], m_readback[i]))
        {
            DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline: readback buffer allocation failed");
            resetTargets();
            return false;
        }
        if (FailedHr(m_readback[i].get()->Map(0, nullptr, &m_readbackMapped[i]), "Map auto exposure readback"))
        {
            resetTargets();
            return false;
        }
        std::memset(m_readbackMapped[i], 0, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    }
    refreshViews();
    return true;
}

bool AutoExposurePipeline::create(GraphTargets& targets, Renderer* renderer)
{
    ID3D12Device* device = targets.device();
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline::create: null device");
        release();
        return false;
    }
    if (m_targets != &targets)
        resetTargets();
    m_targets  = &targets;
    m_renderer = renderer;
    if (!m_psoReduce && !createPsos(device))
    {
        resetTargets();
        return false;
    }
    if (!createTargets(device))
        return false;

    DE_LOG_INFO(LogCategory::Render, "AutoExposurePipeline: ready {}x{} ({} levels, graph targets)", levelWidth(0), levelHeight(0), levelCount());
    m_loggedSkip = false;
    return true;
}

void AutoExposurePipeline::drawLevel(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* root, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE table,
                                     D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH) const
{
    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(dstW);
    vp.Height   = static_cast<float>(dstH);
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(dstW), static_cast<LONG>(dstH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cmd->SetGraphicsRootSignature(root);
    cmd->SetPipelineState(pso);
    cmd->SetGraphicsRootDescriptorTable(kRootSrv, table);
    const float constants[kConstantCount] = {
        1.0f / static_cast<float>(Math::Max(dstW, 1u)),
        1.0f / static_cast<float>(Math::Max(dstH, 1u)),
        static_cast<float>(Math::Max(srcW, 1u)),
        static_cast<float>(Math::Max(srcH, 1u)),
    };
    cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
    cmd->DrawInstanced(3, 1, 0, 0);
}

bool AutoExposurePipeline::meter(ID3D12GraphicsCommandList* cmd, Renderer& renderer)
{
    if (!cmd || !isValid())
    {
        if (!isValid() && !m_loggedSkip)
        {
            DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline::meter: not valid — skipping");
            m_loggedSkip = true;
        }
        return false;
    }

    ID3D12Device* device = renderer.device();
    const D3D12_CPU_DESCRIPTOR_HANDLE hdrSrv   = renderer.hdrSrvCpu();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthSrv = renderer.depthSrvCpu();
    if (!device || hdrSrv.ptr == 0 || depthSrv.ptr == 0)
        return false;

    refreshViews();
    const uint32_t levels = levelCount();
    if (levels == 0)
        return false;
    const GpuScope meterScope(cmd, "Auto Exposure", ProfileColor::AutoExposure);
    const UINT frame = renderer.frameIndex() % kFrameCount;
    device->CopyDescriptorsSimple(1, srvCpu(frame * 2u), hdrSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1, srvCpu(frame * 2u + 1u), depthSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    renderer.transitionDepth(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    transitionLevel(cmd, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    drawLevel(cmd, m_rootReduce.Get(), m_psoReduce.Get(), srvGpu(frame * 2u), m_targets->rtv(m_levels[0].id), levelWidth(0), levelHeight(0), renderer.width(),
              renderer.height());

    for (UINT i = 0; i + 1 < levels; ++i)
    {
        transitionLevel(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transitionLevel(cmd, i + 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
        drawLevel(cmd, m_rootDownsample.Get(), m_psoDownsample.Get(), srvGpu(kFrameSrv + i), m_targets->rtv(m_levels[i + 1].id), levelWidth(i + 1), levelHeight(i + 1),
                  levelWidth(i), levelHeight(i));
    }

    // The 1×1 cannot be copied while it is still bound as a render target.
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    const UINT last = levels - 1;
    transitionLevel(cmd, last, D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource       = m_readback[frame].get();
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R32G32_FLOAT;
    dst.PlacedFootprint.Footprint.Width    = 1;
    dst.PlacedFootprint.Footprint.Height   = 1;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = m_targets->resource(m_levels[last].id);
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_BOX box{};
    box.right  = 1;
    box.bottom = 1;
    box.back   = 1;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    m_slotWritten[frame] = true;

    renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return true;
}

float AutoExposurePipeline::readMeasuredLuma(uint32_t frameIndex) const
{
    const UINT slot = frameIndex % kFrameCount;
    if (!m_slotWritten[slot] || !m_readbackMapped[slot])
        return 0.0f;
    const float* pair = static_cast<const float*>(m_readbackMapped[slot]);
    return measuredLumaFromSums(pair[0], pair[1]);
}

} // namespace Dark
