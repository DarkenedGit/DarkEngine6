#pragma once

#include "Render/ShadowCascades.h"
#include "Render/ShadowPipeline.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Camera3D;
class Renderer;

using Microsoft::WRL::ComPtr;

class ShadowSystem
{
public:
    ShadowSystem() = default;

    bool create(ID3D12Device* device, const ShadowSettings& settings = {});

    void update(
        const Camera3D& camera,
        const Math::Vector3f& lightDirToward,
        const Math::Aabb3f& sceneBounds,
        float sunElevation,
        float cloudCoverage);

    // After beginFrame: render each cascade, then restore the scene targets.
    void beginCapture(ID3D12GraphicsCommandList* cmd);
    void beginCascade(ID3D12GraphicsCommandList* cmd, int cascade);
    void endCapture(ID3D12GraphicsCommandList* cmd);

    void bindReceiverCbv(ID3D12GraphicsCommandList* cmd, UINT rootCbv) const;
    void writeSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dest) const;

    const ShadowPipeline& pipeline() const { return m_pipeline; }
    const ShadowSettings& settings() const { return m_settings; }
    const CascadeData&    cascade(int i) const { return m_cascades[i]; }
    int  cascadeCount() const { return m_cascadeCount; }
    bool enabled() const { return m_enabled; }
    bool isValid() const { return m_resource != nullptr && m_pipeline.isValid(); }

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu() const { return m_srvCpu; }

private:
    bool createResources(ID3D12Device* device);

    ShadowSettings m_settings;
    ShadowPipeline m_pipeline;
    CascadeData    m_cascades[kMaxShadowCascades];
    ShadowConstants m_cpuConstants{};
    int            m_cascadeCount = kMaxShadowCascades;
    bool           m_enabled      = false;

    ComPtr<ID3D12Resource>       m_resource;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12Resource>       m_cbUpload;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvCpu[kMaxShadowCascades]{};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
    UINT                         m_dsvIncr = 0;
    UINT8*                       m_cbMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
    D3D12_RESOURCE_STATES        m_state    = D3D12_RESOURCE_STATE_COMMON;
};

} // namespace Dark
