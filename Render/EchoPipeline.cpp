#include "Render/EchoPipeline.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

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

bool EchoPipeline::create(ID3D12Device* device)
{
    m_snapRoot.Reset();
    m_compositeRoot.Reset();
    m_snapPso.Reset();
    m_compositePso.Reset();
    m_rtvHeap.Reset();
    m_cpuSrvHeap.Reset();
    m_srvHeap.Reset();
    m_snap.Reset();
    m_copy.Reset();
    m_haveSnapshot = false;
    m_snapW        = 0;
    m_snapH        = 0;
    m_copyW        = 0;
    m_copyH        = 0;
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
    if (!compileShaderFromContent("shaders/Echo.hlsl", "VSMain", "vs_5_0", vs)
        || !compileShaderFromContent("shaders/Echo.hlsl", "PSSnapshot", "ps_5_0", snapPs, snapMacro)
        || !compileShaderFromContent("shaders/Echo.hlsl", "PSComposite", "ps_5_0", compositePs))
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

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    D3D12_DESCRIPTOR_HEAP_DESC cpuDesc{};
    cpuDesc.NumDescriptors = kCpuSrvCount;
    cpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    D3D12_DESCRIPTOR_HEAP_DESC gpuDesc{};
    gpuDesc.NumDescriptors = kBufferedFrames * kSrvStride;
    gpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    gpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap echo RTV")
        || FailedHr(device->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap echo CPU SRV")
        || FailedHr(device->CreateDescriptorHeap(&gpuDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap echo SRV"))
    {
        m_snapPso.Reset();
        m_compositePso.Reset();
        m_snapRoot.Reset();
        m_compositeRoot.Reset();
        m_rtvHeap.Reset();
        m_cpuSrvHeap.Reset();
        m_srvHeap.Reset();
        return false;
    }

    m_rtv     = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cpuSrv  = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();
    m_gpuCpu  = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_gpu     = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    DE_LOG_INFO(LogCategory::Render, "EchoPipeline: ready");
    return true;
}

bool EchoPipeline::ensureSnapshot(Renderer& renderer)
{
    ID3D12Device* device = renderer.device();
    const uint32_t fullW = Math::Max(renderer.width(), 1u);
    const uint32_t fullH = Math::Max(renderer.height(), 1u);
    const uint32_t w     = Math::Max(fullW / kSnapshotDivisor, 1u);
    const uint32_t h     = Math::Max(fullH / kSnapshotDivisor, 1u);
    if (!device)
        return false;
    if (m_snap && m_snapW == w && m_snapH == h)
        return true;

    m_haveSnapshot = false;
    if (m_snap)
        renderer.waitForGpu();
    m_snap.Reset();
    m_snapW     = 0;
    m_snapH     = 0;
    m_snapState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R16G16_FLOAT;
    rd.SampleDesc       = { 1, 0 };
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R16G16_FLOAT;

    if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&m_snap)),
                 "CreateCommittedResource echo snapshot"))
        return false;
    m_snap->SetName(L"DE.Echo.Snapshot");

    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format        = DXGI_FORMAT_R16G16_FLOAT;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(m_snap.Get(), &rtv, m_rtv);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                  = DXGI_FORMAT_R16G16_FLOAT;
    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(m_snap.Get(), &srv, m_cpuSrv);

    m_snapW     = w;
    m_snapH     = h;
    m_snapState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return true;
}

bool EchoPipeline::ensureCopy(Renderer& renderer)
{
    ID3D12Device* device = renderer.device();
    const uint32_t w     = renderer.width();
    const uint32_t h     = renderer.height();
    if (!device || w == 0 || h == 0)
        return false;
    if (m_copy && m_copyW == w && m_copyH == h)
        return true;

    if (m_copy)
        renderer.waitForGpu();
    m_copy.Reset();
    m_copyW     = 0;
    m_copyH     = 0;
    m_copyState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc       = { 1, 0 };
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_copy)),
                 "CreateCommittedResource echo beauty copy"))
        return false;
    m_copy->SetName(L"DE.Echo.BeautyCopy");

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels     = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpuSrv;
    cpu.ptr += m_srvIncr;
    device->CreateShaderResourceView(m_copy.Get(), &srv, cpu);

    m_copyW     = w;
    m_copyH     = h;
    m_copyState = D3D12_RESOURCE_STATE_COMMON;
    return true;
}

void EchoPipeline::transitionSnapshot(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
{
    if (!cmd || !m_snap || m_snapState == after)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = m_snap.Get();
    b.Transition.StateBefore = m_snapState;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    m_snapState = after;
}

void EchoPipeline::transitionCopy(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
{
    if (!cmd || !m_copy || m_copyState == after)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = m_copy.Get();
    b.Transition.StateBefore = m_copyState;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    m_copyState = after;
}

bool EchoPipeline::capture(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoSnapshotFrame& frame)
{
    if (!cmd || !m_snapPso || !m_snapRoot || !renderer.hasGBuffer())
        return false;
    if (!ensureSnapshot(renderer))
        return false;

    ID3D12Device* device = renderer.device();
    const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer.attribSrvCpu();
    const D3D12_CPU_DESCRIPTOR_HANDLE depth  = renderer.depthSrvCpu();
    if (!device || attrib.ptr == 0 || depth.ptr == 0)
        return false;

    const GpuScope snapScope(cmd, "Echo Snapshot", ProfileColor::Echo);
    renderer.bindHdr(false);
    transitionSnapshot(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(m_snapW);
    vp.Height   = static_cast<float>(m_snapH);
    vp.MaxDepth = 1.0f;
    const D3D12_RECT sc{ 0, 0, static_cast<LONG>(m_snapW), static_cast<LONG>(m_snapH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->OMSetRenderTargets(1, &m_rtv, FALSE, nullptr);

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
    constants[21] = static_cast<float>(m_snapW);
    constants[22] = static_cast<float>(m_snapH);

    cmd->SetGraphicsRootSignature(m_snapRoot.Get());
    cmd->SetPipelineState(m_snapPso.Get());
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRoot32BitConstants(kRootConstants, kSnapConstantCount, constants, 0);
    cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    transitionSnapshot(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_haveSnapshot = true;
    return true;
}

void EchoPipeline::composite(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoCompositeFrame& frame)
{
    if (!cmd || !m_compositePso || !m_compositeRoot || !hasSnapshot() || !renderer.hasGBuffer())
        return;

    ID3D12Device* device = renderer.device();
    ID3D12Resource* hdr  = renderer.hdr();
    const D3D12_CPU_DESCRIPTOR_HANDLE depth = renderer.depthSrvCpu();
    if (!device || !hdr || depth.ptr == 0 || !ensureCopy(renderer))
        return;

    const GpuScope viewScope(cmd, "Echo Composite", ProfileColor::Echo);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transitionCopy(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(m_copy.Get(), hdr);
    transitionCopy(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    renderer.bindHdrColorTarget();
    transitionSnapshot(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const UINT frameSlot = renderer.frameIndex() % kBufferedFrames;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuCpu;
    dst.ptr += static_cast<SIZE_T>(frameSlot * kSrvStride + kCaptureSrvCount) * m_srvIncr;
    device->CopyDescriptorsSimple(1, dst, m_cpuSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE depthDst = dst;
    depthDst.ptr += m_srvIncr;
    device->CopyDescriptorsSimple(1, depthDst, depth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE beautyCpu = m_cpuSrv;
    beautyCpu.ptr += m_srvIncr;
    D3D12_CPU_DESCRIPTOR_HANDLE beautyDst = depthDst;
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
    constants[41] = kRangeMeters;

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
