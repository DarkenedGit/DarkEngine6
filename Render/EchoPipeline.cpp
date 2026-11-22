#include "Render/EchoPipeline.h"
#include "Render/GraphTargets.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

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

bool CreateRoot(ID3D12Device* device, UINT constantCount, UINT srvCount, bool pointSampler, const char* label, ComPtr<ID3D12RootSignature>& out)
{
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = srvCount;
    srvRange.BaseShaderRegister                = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = constantCount;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = pointSampler ? 1u : 0u;
    rsDesc.pStaticSamplers   = pointSampler ? &samp : nullptr;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), label))
    {
        if (rsErr)
            DE_LOG_ERROR(LogCategory::Render, "{}: {}", label, static_cast<const char*>(rsErr->GetBufferPointer()));
        return false;
    }
    return !FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&out)), label);
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC FullscreenPso(ID3D12RootSignature* root, ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT rtv)
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
    pso.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets                                 = 1;
    pso.RTVFormats[0]                                    = rtv;
    pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc                                       = { 1, 0 };
    return pso;
}

} // namespace

EchoPipeline::~EchoPipeline()
{
    release();
}

void EchoPipeline::release()
{
    if (m_targets)
    {
        m_targets->unregisterTarget(m_snap);
        m_targets->unregisterTarget(m_copy);
    }
    m_snap    = {};
    m_copy    = {};
    m_targets = nullptr;
    m_snapRoot.Reset();
    m_compositeRoot.Reset();
    m_snapPso.Reset();
    m_compositePso.Reset();
    m_srvHeap.Reset();
    m_haveSnapshot       = false;
    m_snapshotGeneration = 0;
}

bool EchoPipeline::hasSnapshot() const
{
    // A resize recreates the snapshot (new generation): the old chirp is gone with it.
    return m_haveSnapshot && m_targets && m_targets->resource(m_snap.id) && m_targets->generation(m_snap.id) == m_snapshotGeneration;
}

bool EchoPipeline::create(ID3D12Device* device)
{
    release();
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "EchoPipeline::create: null device");
        return false;
    }

    if (!CreateRoot(device, kSnapConstantCount, kCaptureSrvCount, false, "CreateRootSignature (echo snapshot)", m_snapRoot)
        || !CreateRoot(device, kViewConstantCount, kCompositeSrvCount, true, "CreateRootSignature (echo composite)", m_compositeRoot))
        return false;

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> snapPs;
    ComPtr<ID3DBlob> compositePs;
    D3D_SHADER_MACRO snapMacro[2] = { { "ECHO_SNAPSHOT", "1" }, { nullptr, nullptr } };
    if (!compileShaderFromContent("shaders/Echo.hlsl", "VSMain", ShaderStage::Vertex, vs)
        || !compileShaderFromContent("shaders/Echo.hlsl", "PSSnapshot", ShaderStage::Pixel, snapPs, snapMacro)
        || !compileShaderFromContent("shaders/Echo.hlsl", "PSComposite", ShaderStage::Pixel, compositePs))
    {
        m_snapRoot.Reset();
        m_compositeRoot.Reset();
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC snapDesc = FullscreenPso(m_snapRoot.Get(), vs.Get(), snapPs.Get(), DXGI_FORMAT_R16G16_FLOAT);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC viewDesc = FullscreenPso(m_compositeRoot.Get(), vs.Get(), compositePs.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (FailedHr(device->CreateGraphicsPipelineState(&snapDesc, IID_PPV_ARGS(&m_snapPso)), "CreateGraphicsPipelineState (echo snapshot)")
        || FailedHr(device->CreateGraphicsPipelineState(&viewDesc, IID_PPV_ARGS(&m_compositePso)), "CreateGraphicsPipelineState (echo composite)"))
    {
        m_snapPso.Reset();
        m_compositePso.Reset();
        m_snapRoot.Reset();
        m_compositeRoot.Reset();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC gpuDesc{};
    gpuDesc.NumDescriptors = kBufferedFrames * kSrvStride;
    gpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    gpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FailedHr(device->CreateDescriptorHeap(&gpuDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap echo SRV"))
    {
        release();
        return false;
    }

    m_gpuCpu  = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_gpu     = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    DE_LOG_INFO(LogCategory::Render, "EchoPipeline: ready");
    return true;
}

bool EchoPipeline::ensureTargets(Renderer& renderer)
{
    // Registered on first use (only echolocation pays for them); Renderer::resize keeps them at the
    // output size, so there is no lazy resize and no GPU wait here.
    if (m_targets && m_snap.id.valid() && m_copy.id.valid())
        return m_targets->resource(m_snap.id) && m_targets->resource(m_copy.id);
    GraphTargets* targets = renderer.targets();
    if (!targets || !targets->valid())
        return false;
    m_targets = targets;
    if (!m_snap.id.valid())
    {
        const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_snap = targets->registerTarget("DE.Echo.Snapshot", colorTargetDesc(DXGI_FORMAT_R16G16_FLOAT, Rg::TargetSize::OutputFloor, kSnapshotDivisor,
                                                                             D3D12_RESOURCE_STATE_RENDER_TARGET, zero4));
    }
    if (!m_copy.id.valid())
        m_copy = targets->registerTarget("DE.Echo.BeautyCopy", copyTargetDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_COMMON));
    return m_snap.id.valid() && m_copy.id.valid();
}

bool EchoPipeline::capture(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoSnapshotFrame& frame)
{
    if (!cmd || !m_snapPso || !m_snapRoot || !renderer.hasSceneBuffers())
        return false;
    if (!ensureTargets(renderer))
        return false;
    const uint32_t snapW = m_targets->width(m_snap.id);
    const uint32_t snapH = m_targets->height(m_snap.id);

    ID3D12Device* device = renderer.device();
    const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer.attribSrvCpu();
    const D3D12_CPU_DESCRIPTOR_HANDLE depth  = renderer.depthSrvCpu();
    if (!device || attrib.ptr == 0 || depth.ptr == 0)
        return false;

    const GpuScope snapScope(cmd, "Echo Snapshot", ProfileColor::Echo);
    renderer.bindHdr(false);
    m_targets->transition(cmd, m_snap, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(snapW);
    vp.Height   = static_cast<float>(snapH);
    vp.MaxDepth = 1.0f;
    const D3D12_RECT sc{ 0, 0, static_cast<LONG>(snapW), static_cast<LONG>(snapH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    const D3D12_CPU_DESCRIPTOR_HANDLE snapRtv = m_targets->rtv(m_snap.id);
    cmd->OMSetRenderTargets(1, &snapRtv, FALSE, nullptr);

    const UINT frameSlot = renderer.frameIndex() % kBufferedFrames;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuCpu;
    dst.ptr += static_cast<SIZE_T>(frameSlot * kSrvStride) * m_srvIncr;
    device->CopyDescriptorsSimple(1, dst, attrib, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE depthDst = dst;
    depthDst.ptr += m_srvIncr;
    device->CopyDescriptorsSimple(1, depthDst, depth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_gpu;
    gpu.ptr += static_cast<SIZE_T>(frameSlot * kSrvStride) * m_srvIncr;

    float constants[kSnapConstantCount]{};
    std::memcpy(constants, frame.invViewProj, sizeof(float) * 16);
    constants[16] = frame.cameraPos[0];
    constants[17] = frame.cameraPos[1];
    constants[18] = frame.cameraPos[2];
    constants[19] = static_cast<float>(Math::Max(renderer.width(), 1u));
    constants[20] = static_cast<float>(Math::Max(renderer.height(), 1u));
    constants[21] = static_cast<float>(snapW);
    constants[22] = static_cast<float>(snapH);

    cmd->SetGraphicsRootSignature(m_snapRoot.Get());
    cmd->SetPipelineState(m_snapPso.Get());
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRoot32BitConstants(kRootConstants, kSnapConstantCount, constants, 0);
    cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    m_targets->transition(cmd, m_snap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_haveSnapshot       = true;
    m_snapshotGeneration = m_targets->generation(m_snap.id);
    return true;
}

void EchoPipeline::composite(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoCompositeFrame& frame)
{
    if (!cmd || !m_compositePso || !m_compositeRoot || !hasSnapshot() || !renderer.hasSceneBuffers())
        return;

    ID3D12Device* device = renderer.device();
    ID3D12Resource* hdr  = renderer.hdr();
    const D3D12_CPU_DESCRIPTOR_HANDLE depth = renderer.depthSrvCpu();
    if (!device || !hdr || depth.ptr == 0 || !ensureTargets(renderer))
        return;
    const uint32_t snapW = m_targets->width(m_snap.id);
    const uint32_t snapH = m_targets->height(m_snap.id);

    const GpuScope viewScope(cmd, "Echo Composite", ProfileColor::Echo);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_targets->transition(cmd, m_copy, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(m_targets->resource(m_copy.id), hdr);
    m_targets->transition(cmd, m_copy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    renderer.bindHdrColorTarget();
    m_targets->transition(cmd, m_snap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const UINT frameSlot = renderer.frameIndex() % kBufferedFrames;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuCpu;
    dst.ptr += static_cast<SIZE_T>(frameSlot * kSrvStride + kCaptureSrvCount) * m_srvIncr;
    device->CopyDescriptorsSimple(1, dst, m_targets->srv(m_snap.id), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE depthDst = dst;
    depthDst.ptr += m_srvIncr;
    device->CopyDescriptorsSimple(1, depthDst, depth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    const D3D12_CPU_DESCRIPTOR_HANDLE beautyCpu = m_targets->srv(m_copy.id);
    D3D12_CPU_DESCRIPTOR_HANDLE       beautyDst = depthDst;
    beautyDst.ptr += m_srvIncr;
    device->CopyDescriptorsSimple(1, beautyDst, beautyCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_gpu;
    gpu.ptr += static_cast<SIZE_T>(frameSlot * kSrvStride + kCaptureSrvCount) * m_srvIncr;

    const float fullW = static_cast<float>(Math::Max(renderer.width(), 1u));
    const float fullH = static_cast<float>(Math::Max(renderer.height(), 1u));
    float constants[kViewConstantCount]{};
    std::memcpy(constants, frame.currInvViewProj, sizeof(float) * 16);
    std::memcpy(constants + 16, frame.chirpViewProj, sizeof(float) * 16);
    constants[32] = frame.chirpCamera[0];
    constants[33] = frame.chirpCamera[1];
    constants[34] = frame.chirpCamera[2];
    constants[35] = frame.radius;
    constants[36] = frame.strength;
    constants[37] = fullW;
    constants[38] = fullH;
    constants[39] = kRangeTolerance;
    constants[40] = kEdgeMeters;
    constants[41] = frame.maxRange > 0.0f ? frame.maxRange : kRangeMeters;
    constants[42] = static_cast<float>(snapW);
    constants[43] = static_cast<float>(snapH);

    cmd->SetGraphicsRootSignature(m_compositeRoot.Get());
    cmd->SetPipelineState(m_compositePso.Get());
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRoot32BitConstants(kRootConstants, kViewConstantCount, constants, 0);
    cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

} // namespace Dark
