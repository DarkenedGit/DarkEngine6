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

    // Creates the map in DEPTH_WRITE and tracks it in the frame's ResourceStateTracker: the graph
    // plans its barriers, and the map starts every frame writable (Render/ShadowInputs.h).
    bool create(ID3D12Device* device, const ShadowSettings& settings = {});
    // Stops tracking the map and releases everything (call instead of reassigning).
    void release();

    // frameIndex must match Renderer::frameIndex() so in-flight frames do not
    // share a cascade slice or CBV slot.
    void update(
        const Camera3D& camera,
        const Math::Vector3f& lightDirToward,
        const Math::AABox3f& sceneBounds,
        float sunElevation,
        float cloudCoverage,
        uint32_t frameIndex);

    // Inside the Shadows pass, on whichever list records the cascades: beginCascade binds that
    // cascade's DSV and clears it. No barriers: the map is writable for the whole pass.
    // beginCapture / endCapture open and close the profile scope.
    void beginCapture(ID3D12GraphicsCommandList* cmd, bool profile = true);
    void beginCascade(ID3D12GraphicsCommandList* cmd, int cascade, bool profile = true);
    void endCapture(ID3D12GraphicsCommandList* cmd);

    void bindReceiverCbv(ID3D12GraphicsCommandList* cmd, UINT rootCbv) const;
    D3D12_GPU_VIRTUAL_ADDRESS receiverCbvGpu() const
    {
        if (m_cbGpu == 0)
            return 0;
        return m_cbGpu + static_cast<UINT64>(m_frame) * cbBytes();
    }
    void writeSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dest) const;

    const ShadowPipeline& pipeline() const { return m_pipeline; }
    // The shadow map the frame graph imports (with srvCpu() as its view).
    ID3D12Resource*       resource() const { return m_resource.Get(); }
    const ShadowSettings& settings() const { return m_settings; }
    const CascadeData&    cascade(int i) const { return m_cascades[i]; }
    int  cascadeCount() const { return m_cascadeCount; }
    bool enabled() const { return m_enabled; }
    bool isValid() const { return m_resource != nullptr && m_pipeline.isValid(); }
    // ShadowConstants params[1]: 0 means unshadowed. Packed by update() and disableThisFrame().
    float strength() const { return m_cpuConstants.params[1]; }

    // This frame is unshadowed: enabled is false and the packed strength is 0. Call after update()
    // so the CBV slot matches this frame. The map stays allocated.
    void disableThisFrame();
    // Multiply the packed strength (params[1]) by s in 0..1. A scale of 1 leaves the CBV unchanged.
    // Call after update() / disableThisFrame().
    void scaleStrength(float s);

    // Debug overlay / F7: when false, skip capture and pack strength 0.
    void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    bool debugEnabled() const { return m_debugEnabled; }

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu() const { return m_srvCpu; }
    int debugSliceOffset() const { return m_frame * m_cascadeCount; }

    static constexpr int kBufferedFrames = 2;

private:
    bool createResources(ID3D12Device* device);
    UINT cbBytes() const;
    void closeProfileCascade(ID3D12GraphicsCommandList* cmd);

    ShadowSettings m_settings;
    ShadowPipeline m_pipeline;
    CascadeData    m_cascades[kMaxShadowCascades];
    ShadowConstants m_cpuConstants{};
    int            m_cascadeCount = kMaxShadowCascades;
    int            m_frame        = 0;
    bool           m_enabled         = false;
    bool           m_debugEnabled    = true;
    bool           m_profileShadows  = false;
    bool           m_profileCascade  = false;

    ComPtr<ID3D12Resource>       m_resource;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12Resource>       m_cbUpload;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvCpu[kBufferedFrames][kMaxShadowCascades]{};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
    UINT                         m_dsvIncr  = 0;
    UINT8*                       m_cbMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
};

} // namespace Dark
