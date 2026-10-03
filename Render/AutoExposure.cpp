#include "Render/AutoExposure.h"
#include "Render/Profile.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

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

const wchar_t* kLevelNames[AutoExposurePipeline::kMaxLevels] = {
    L"DE.AutoExposure.L0",  L"DE.AutoExposure.L1",  L"DE.AutoExposure.L2",  L"DE.AutoExposure.L3",
    L"DE.AutoExposure.L4",  L"DE.AutoExposure.L5",  L"DE.AutoExposure.L6",  L"DE.AutoExposure.L7",
    L"DE.AutoExposure.L8",  L"DE.AutoExposure.L9",  L"DE.AutoExposure.L10", L"DE.AutoExposure.L11",
    L"DE.AutoExposure.L12", L"DE.AutoExposure.L13", L"DE.AutoExposure.L14", L"DE.AutoExposure.L15",
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
            m_readback[i]->Unmap(0, nullptr);
        m_readbackMapped[i] = nullptr;
        m_readback[i].Reset();
        m_slotWritten[i] = false;
    }
    for (UINT i = 0; i < kMaxLevels; ++i)
    {
        m_levels[i].res.Reset();
        m_levels[i].w     = 0;
        m_levels[i].h     = 0;
        m_levels[i].state = D3D12_RESOURCE_STATE_COMMON;
    }
    m_rtvHeap.Reset();
    m_srvHeap.Reset();
    m_rtvCpu     = {};
    m_srvCpu     = {};
    m_srvGpu     = {};
    m_width      = 0;
    m_height     = 0;
    m_levelCount = 0;
}

void AutoExposurePipeline::invalidateReadback()
{
    m_slotWritten[0] = false;
    m_slotWritten[1] = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE AutoExposurePipeline::rtvCpu(UINT level) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvCpu;
    h.ptr += static_cast<SIZE_T>(level) * m_rtvIncr;
    return h;
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
    if (!cmd || index >= m_levelCount || !m_levels[index].res || m_levels[index].state == after)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = m_levels[index].res.Get();
    b.Transition.StateBefore = m_levels[index].state;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    m_levels[index].state = after;
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
    if (!compileShaderFromContent("shaders/AutoExposure.hlsl", "VSMain", "vs_5_0", vs)
        || !compileShaderFromContent("shaders/AutoExposure.hlsl", "PSReduce", "ps_5_0", psReduce, reduceMacros)
        || !compileShaderFromContent("shaders/AutoExposure.hlsl", "PSDownsample", "ps_5_0", psDown))
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

bool AutoExposurePipeline::createTargets(ID3D12Device* device, uint32_t width, uint32_t height)
{
    resetTargets();
    if (!device || width == 0 || height == 0)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline::createTargets: invalid device or size");
        return false;
    }

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

    m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kMaxLevels;
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap auto exposure RTV"))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = kFrameSrv + kMaxLevels;
    srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap auto exposure SRV"))
    {
        resetTargets();
        return false;
    }

    m_rtvCpu = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    uint32_t levelW = Math::Max(width / 4u, 1u);
    uint32_t levelH = Math::Max(height / 4u, 1u);
    m_levelCount    = 0;
    while (m_levelCount < kMaxLevels)
    {
        const UINT i = m_levelCount;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = levelW;
        rd.Height           = levelH;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R32G32_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_R32G32_FLOAT;

        if (FailedHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&m_levels[i].res)),
                     "CreateCommittedResource auto exposure level"))
        {
            resetTargets();
            return false;
        }
        m_levels[i].res->SetName(kLevelNames[i]);
        m_levels[i].w     = levelW;
        m_levels[i].h     = levelH;
        m_levels[i].state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        device->CreateRenderTargetView(m_levels[i].res.Get(), nullptr, rtvCpu(i));

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format                    = DXGI_FORMAT_R32G32_FLOAT;
        srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels       = 1;
        device->CreateShaderResourceView(m_levels[i].res.Get(), &srv, srvCpu(kFrameSrv + i));

        ++m_levelCount;
        if (levelW == 1 && levelH == 1)
            break;
        levelW = Math::Max(levelW / 2u, 1u);
        levelH = Math::Max(levelH / 2u, 1u);
    }

    if (m_levelCount == 0 || m_levels[m_levelCount - 1].w != 1 || m_levels[m_levelCount - 1].h != 1)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline: pyramid did not reach 1x1 ({}x{})", width, height);
        resetTargets();
        return false;
    }

    D3D12_HEAP_PROPERTIES readHeap{};
    readHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width            = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    bd.Height           = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels        = 1;
    bd.Format           = DXGI_FORMAT_UNKNOWN;
    bd.SampleDesc.Count = 1;
    bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    const wchar_t* readNames[kFrameCount] = { L"DE.AutoExposure.Read0", L"DE.AutoExposure.Read1" };
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        if (FailedHr(device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback[i])),
                     "CreateCommittedResource auto exposure readback"))
        {
            resetTargets();
            return false;
        }
        m_readback[i]->SetName(readNames[i]);
        if (FailedHr(m_readback[i]->Map(0, nullptr, &m_readbackMapped[i]), "Map auto exposure readback"))
        {
            resetTargets();
            return false;
        }
        std::memset(m_readbackMapped[i], 0, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    }

    m_width  = width;
    m_height = height;
    return true;
}

bool AutoExposurePipeline::create(ID3D12Device* device, uint32_t width, uint32_t height)
{
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "AutoExposurePipeline::create: null device");
        resetTargets();
        m_psoReduce.Reset();
        m_psoDownsample.Reset();
        m_rootReduce.Reset();
        m_rootDownsample.Reset();
        return false;
    }
    if (!m_psoReduce && !createPsos(device))
    {
        resetTargets();
        return false;
    }
    if (!createTargets(device, width, height))
        return false;

    DE_LOG_INFO(LogCategory::Render, "AutoExposurePipeline: ready {}x{} ({} levels)", width, height, m_levelCount);
    m_loggedSkip = false;
    return true;
}

bool AutoExposurePipeline::resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    if (isValid() && m_width == width && m_height == height)
        return true;
    return create(device, width, height);
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
    drawLevel(cmd, m_rootReduce.Get(), m_psoReduce.Get(), srvGpu(frame * 2u), rtvCpu(0), m_levels[0].w, m_levels[0].h, renderer.width(), renderer.height());

    for (UINT i = 0; i + 1 < m_levelCount; ++i)
    {
        transitionLevel(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transitionLevel(cmd, i + 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
        drawLevel(cmd, m_rootDownsample.Get(), m_psoDownsample.Get(), srvGpu(kFrameSrv + i), rtvCpu(i + 1), m_levels[i + 1].w, m_levels[i + 1].h, m_levels[i].w, m_levels[i].h);
    }

    // The 1×1 cannot be copied while it is still bound as a render target.
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    const UINT last = m_levelCount - 1;
    transitionLevel(cmd, last, D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource       = m_readback[frame].Get();
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R32G32_FLOAT;
    dst.PlacedFootprint.Footprint.Width    = 1;
    dst.PlacedFootprint.Footprint.Height   = 1;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = m_levels[last].res.Get();
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
