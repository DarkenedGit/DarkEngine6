#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Renderer;

using Microsoft::WRL::ComPtr;

// One chirp's view. Sandbox owns the clock; the pipeline only stores the snapshot.
struct EchoSnapshotFrame
{
    float invViewProj[16]{};
    float cameraPos[3]{};
};

// Composite reads the frozen snapshot. strength eases the whole picture, including the black field.
struct EchoCompositeFrame
{
    float currInvViewProj[16]{};
    float chirpViewProj[16]{};
    float chirpCamera[3]{};
    float radius   = 0.0f;
    float strength = 0.0f;
    float maxRange = 0.0f;
};

// Deferred echolocation. A low-resolution G-buffer snapshot once per chirp, then a
// fullscreen composite that replaces HDR. Not a material, and it does not write RT0.a.
class EchoPipeline
{
public:
    static constexpr UINT kRootConstants = 0;
    static constexpr UINT kRootSrv       = 1;

    // Built-in chirp, not the speed of sound. Sandbox loads content/render/echolocation.json
    // over the period, the speed, and the range. Dev Tools can change the live copies. At the
    // built-in speed the shell crosses kRangeMeters in kExpandSeconds, then holds until the next
    // pulse. A shorter period or a faster shell can start the next chirp while the shell is still
    // moving. kEdgeMeters is wide enough to show for about one 60 Hz frame at the built-in speed.
    static constexpr float    kPeriodSeconds     = 1.25f;
    static constexpr float    kRangeMeters       = 40.0f;
    static constexpr float    kSpeedMetersPerSec = 50.0f;
    static constexpr float    kExpandSeconds     = kRangeMeters / kSpeedMetersPerSec;
    static constexpr float    kEdgeMeters        = 8.0f;
    static constexpr float    kRangeTolerance    = 0.75f;
    static constexpr uint32_t kSnapshotDivisor   = 8;

    static_assert(kSnapshotDivisor >= 1u, "snapshot scale");

    EchoPipeline() = default;

    bool create(ID3D12Device* device);
    bool capture(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoSnapshotFrame& frame);
    void composite(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const EchoCompositeFrame& frame);

    bool isValid() const { return m_snapPso != nullptr && m_compositePso != nullptr; }
    bool hasSnapshot() const { return m_haveSnapshot && m_snap != nullptr; }

private:
    static constexpr UINT kBufferedFrames     = 2;
    static constexpr UINT kCaptureSrvCount    = 2; // attrib + depth
    static constexpr UINT kCompositeSrvCount  = 3; // snapshot + depth + beauty copy
    static constexpr UINT kSrvStride          = kCaptureSrvCount + kCompositeSrvCount;
    static constexpr UINT kSnapConstantCount  = 24;
    static constexpr UINT kViewConstantCount  = 44;
    static constexpr UINT kCpuSrvCount        = 2; // snapshot, beauty copy

    bool ensureSnapshot(Renderer& renderer);
    bool ensureCopy(Renderer& renderer);
    void transitionSnapshot(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
    void transitionCopy(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

    ComPtr<ID3D12RootSignature>  m_snapRoot;
    ComPtr<ID3D12RootSignature>  m_compositeRoot;
    ComPtr<ID3D12PipelineState>  m_snapPso;
    ComPtr<ID3D12PipelineState>  m_compositePso;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_cpuSrvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12Resource>       m_snap;
    ComPtr<ID3D12Resource>       m_copy;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuSrv{};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_gpuCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
    UINT                         m_srvIncr = 0;
    D3D12_RESOURCE_STATES        m_snapState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES        m_copyState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t                     m_snapW     = 0;
    uint32_t                     m_snapH     = 0;
    uint32_t                     m_copyW     = 0;
    uint32_t                     m_copyH     = 0;
    bool                         m_haveSnapshot = false;
};

} // namespace Dark
