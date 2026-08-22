#include "Render/ShadowSystem.h"
#include "Render/Camera3D.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cstring>

namespace Dark
{

using namespace Math;

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

bool ShadowSystem::create(ID3D12Device* device, const ShadowSettings& settings)
{
    m_settings = settings;
    if (m_settings.cascadeCount < 1)
        m_settings.cascadeCount = 1;
    if (m_settings.cascadeCount > kMaxShadowCascades)
        m_settings.cascadeCount = kMaxShadowCascades;
    if (m_settings.mapSize < 256)
        m_settings.mapSize = 256;
    m_cascadeCount = m_settings.cascadeCount;

    if (!m_pipeline.create(device))
        return false;
    return createResources(device);
}

bool ShadowSystem::createResources(ID3D12Device* device)
{
    if (!device)
        return false;

    m_resource.Reset();
    m_dsvHeap.Reset();
    m_srvHeap.Reset();
    m_cbUpload.Reset();
    m_cbMapped = nullptr;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = m_settings.mapSize;
    desc.Height           = m_settings.mapSize;
    desc.DepthOrArraySize = static_cast<UINT16>(m_cascadeCount * kBufferedFrames);
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format             = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    if (FailedHr(
            device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_resource)),
            "CreateCommittedResource shadow map"))
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = static_cast<UINT>(m_cascadeCount * kBufferedFrames);
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FailedHr(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap shadow DSV"))
        return false;

    m_dsvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int frame = 0; frame < kBufferedFrames; ++frame)
    {
        for (int i = 0; i < m_cascadeCount; ++i)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.MipSlice        = 0;
            dsvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(frame * m_cascadeCount + i);
            dsvDesc.Texture2DArray.ArraySize       = 1;
            device->CreateDepthStencilView(m_resource.Get(), &dsvDesc, dsv);
            m_dsvCpu[frame][i] = dsv;
            dsv.ptr += m_dsvIncr;
        }
        m_state[frame] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FailedHr(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap shadow SRV"))
        return false;

    m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                        = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MipLevels      = 1;
    srvDesc.Texture2DArray.ArraySize      = static_cast<UINT>(m_cascadeCount * kBufferedFrames);
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_srvCpu);

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
            "CreateCommittedResource shadow CBV"))
    {
        return false;
    }

    if (FailedHr(m_cbUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped)), "Map shadow CBV"))
        return false;
    m_cbGpu = m_cbUpload->GetGPUVirtualAddress();

    DE_LOG_INFO("ShadowSystem: {} cascades @ {}px ({} buffered frames)", m_cascadeCount, m_settings.mapSize, kBufferedFrames);
    return true;
}

UINT ShadowSystem::cbBytes() const
{
    return (sizeof(ShadowConstants) + 255u) & ~255u;
}

UINT ShadowSystem::sliceIndex(int cascade) const
{
    return static_cast<UINT>(m_frame * m_cascadeCount + cascade);
}

void ShadowSystem::update(
    const Camera3D& camera,
    const Vector3f& lightDirToward,
    const Aabb3f& sceneBounds,
    float sunElevation,
    float cloudCoverage,
    uint32_t frameIndex)
{
    m_frame   = static_cast<int>(frameIndex % static_cast<uint32_t>(kBufferedFrames));
    m_enabled = m_debugEnabled && isValid() && sunElevation > 0.04f && cloudCoverage < 0.92f;
    const float strength = m_enabled
        ? Clamp((sunElevation - 0.04f) / 0.10f, 0.0f, 1.0f) * (1.0f - SmoothStep(0.55f, 0.92f, cloudCoverage))
        : 0.0f;

    const float nearZ = Max(camera.GetNearZ(), 0.5f);
    const float farZ  = Min(camera.GetFarZ(), m_settings.maxDistance);
    float splits[kMaxShadowCascades];
    computePracticalSplits(nearZ, farZ, m_cascadeCount, m_settings.splitLambda, splits);

    float sliceNear = nearZ;
    for (int i = 0; i < m_cascadeCount; ++i)
    {
        Vector3f corners[8];
        extractFrustumCorners(camera, sliceNear, splits[i], corners);
        m_cascades[i].splitNear = sliceNear;
        m_cascades[i].splitFar  = splits[i];
        if (!buildCascadeMatrix(
                corners, lightDirToward, sceneBounds, m_settings.casterMargin, m_settings.mapSize, m_cascades[i]))
        {
            m_enabled = false;
        }
        sliceNear = splits[i];
    }

    packShadowConstants(
        m_cpuConstants,
        m_cascades,
        m_cascadeCount,
        m_settings.mapSize,
        m_settings.depthBias,
        strength,
        camera.GetLook(),
        static_cast<float>(m_frame * m_cascadeCount));
    if (m_cbMapped)
        std::memcpy(m_cbMapped + static_cast<size_t>(m_frame) * cbBytes(), &m_cpuConstants, sizeof(m_cpuConstants));
}

void ShadowSystem::beginCapture(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_resource)
        return;
    if (m_state[m_frame] == D3D12_RESOURCE_STATE_DEPTH_WRITE)
        return;

    D3D12_RESOURCE_BARRIER barriers[kMaxShadowCascades]{};
    for (int i = 0; i < m_cascadeCount; ++i)
    {
        barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource   = m_resource.Get();
        barriers[i].Transition.StateBefore = m_state[m_frame];
        barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[i].Transition.Subresource = sliceIndex(i);
    }
    cmd->ResourceBarrier(static_cast<UINT>(m_cascadeCount), barriers);
    m_state[m_frame] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void ShadowSystem::beginCascade(ID3D12GraphicsCommandList* cmd, int cascade)
{
    if (!cmd || cascade < 0 || cascade >= m_cascadeCount)
        return;

    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(m_settings.mapSize);
    vp.Height   = static_cast<float>(m_settings.mapSize);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(m_settings.mapSize), static_cast<LONG>(m_settings.mapSize) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_dsvCpu[m_frame][cascade]);
    cmd->ClearDepthStencilView(m_dsvCpu[m_frame][cascade], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_pipeline.bind(cmd);
    m_pipeline.setWvp(cmd, m_cascades[cascade].viewProj.m_afEntry);
}

void ShadowSystem::endCapture(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_resource)
        return;
    if (m_state[m_frame] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        return;

    D3D12_RESOURCE_BARRIER barriers[kMaxShadowCascades]{};
    for (int i = 0; i < m_cascadeCount; ++i)
    {
        barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource   = m_resource.Get();
        barriers[i].Transition.StateBefore = m_state[m_frame];
        barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource = sliceIndex(i);
    }
    cmd->ResourceBarrier(static_cast<UINT>(m_cascadeCount), barriers);
    m_state[m_frame] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ShadowSystem::bindReceiverCbv(ID3D12GraphicsCommandList* cmd, UINT rootCbv) const
{
    if (!cmd || !m_cbGpu)
        return;
    cmd->SetGraphicsRootConstantBufferView(rootCbv, m_cbGpu + static_cast<UINT64>(m_frame) * cbBytes());
}

void ShadowSystem::writeSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dest) const
{
    if (!device || !m_srvHeap)
        return;
    device->CopyDescriptorsSimple(1, dest, m_srvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

} // namespace Dark
