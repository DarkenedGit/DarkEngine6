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

    // frameIndex must match Renderer::frameIndex() so in-flight frames do not
    // share a cascade slice or CBV slot.
    void update(
        const Camera3D& camera,
        const Math::Vector3f& lightDirToward,
        const Math::Aabb3f& sceneBounds,
        float sunElevation,
        float cloudCoverage,
        uint32_t frameIndex);

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

    // Debug overlay / F7: when false, skip capture and pack strength 0.
    void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    bool debugEnabled() const { return m_debugEnabled; }

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu() const { return m_srvCpu; }
    int debugSliceOffset() const { return m_frame * m_cascadeCount; }

    static constexpr int kBufferedFrames = 2;

private:
    bool createResources(ID3D12Device* device);
    UINT sliceIndex(int cascade) const;
    UINT cbBytes() const;

    ShadowSettings m_settings;
    ShadowPipeline m_pipeline;
    CascadeData    m_cascades[kMaxShadowCascades];
    ShadowConstants m_cpuConstants{};
    int            m_cascadeCount = kMaxShadowCascades;
    int            m_frame        = 0;
    bool           m_enabled      = false;
    bool           m_debugEnabled = true;

    ComPtr<ID3D12Resource>       m_resource;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12Resource>       m_cbUpload;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvCpu[kBufferedFrames][kMaxShadowCascades]{};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
    UINT                         m_dsvIncr  = 0;
    UINT8*                       m_cbMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
    D3D12_RESOURCE_STATES        m_state[kBufferedFrames]{
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON
    };
};

} // namespace Dark
