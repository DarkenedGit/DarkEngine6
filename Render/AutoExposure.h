#pragma once

#include "Render/GraphTargets.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Renderer;

using Microsoft::WRL::ComPtr;

enum class ExposureMode : uint8_t
{
    Manual,
    Auto
};

// Correction on top of Environment::exposure(). Manual applies 0.
struct AutoExposureSettings
{
    ExposureMode mode        = ExposureMode::Auto;
    float        targetGrey  = 0.18f;
    float        evBias      = 0.0f;
    float        maxEv       = 1.5f; // clamp of the correction, not of the artistic exposure
    float        adaptBright = 6.0f; // 1/s, view got brighter, exposure falls
    float        adaptDark   = 1.0f; // 1/s, view got darker, exposure rises
};

struct AutoExposureResult
{
    float measuredLuma  = 0.0f; // 0 = no sample this frame
    float evCorrection  = 0.0f;
    float finalExposure = 1.0f; // envExposure * exp2(evCorrection), or envExposure in Manual
};

struct AutoExposureState
{
    float ev = 0.0f;
};

// One weighted luminance. weight <= 0 is ignored, matching a sky pixel in the pyramid.
struct LumaSample
{
    float weight = 0.0f;
    float luma   = 0.0f;
};

// 1×1 pair stored by AutoExposure.hlsl. weight <= 1e-6 → 0 so an all-sky frame is a missing sample.
float measuredLumaFromSums(float logSum, float weight);

// Geometric mean of the samples. A zero-weight sample does not move the result.
float logAverageLuma(const LumaSample* samples, int count);

// Host calls this when Auto is turned on and when the view resizes.
inline void resetAutoExposure(AutoExposureState& state)
{
    state.ev = 0.0f;
}

// measured <= 0 keeps state.ev. Manual applies correction 0 and does not adapt.
// Auto with a missing sample still applies the held ev (0 until the first good sample).
AutoExposureResult adaptExposure(AutoExposureState& state, float measuredLuma, float envExposure, float dt, const AutoExposureSettings& settings);

// Quarter-res log/weight pyramid plus a double-buffered 1×1 readback.
// Read the frame-index slot before meter(); that slot was written the last time this
// frame index ran, and the per-frame fence has already waited for it.
class AutoExposurePipeline
{
public:
    static constexpr UINT kFrameCount = 2;
    static constexpr UINT kMaxLevels  = 16;

    AutoExposurePipeline() = default;
    ~AutoExposurePipeline();

    AutoExposurePipeline(const AutoExposurePipeline&)            = delete;
    AutoExposurePipeline& operator=(const AutoExposurePipeline&) = delete;

    // Graph targets (render-graph plan Phase 5): the level chain is registered output-relative and
    // Renderer::resize recreates it (generation() changes). The readbacks are D3D12MA buffers whose
    // release `renderer` defers behind the frame fence (null: released at once, GPU idle; tests).
    bool create(GraphTargets& targets, Renderer* renderer);
    uint64_t generation() const; // changes when a resize recreated the pyramid
    // Records the pyramid and copies the 1×1 into this frame's readback slot.
    bool meter(ID3D12GraphicsCommandList* cmd, Renderer& renderer);
    // 0 when this slot has not completed a copy, or the weight was 0.
    float readMeasuredLuma(uint32_t frameIndex) const;
    // Next reads return 0 until a later meter() completes. Does not change GPU memory.
    void invalidateReadback();
    bool isValid() const;
    void release();

private:
    static constexpr UINT kRootConstants = 0;
    static constexpr UINT kRootSrv       = 1;
    static constexpr UINT kConstantCount = 4;
    static constexpr UINT kFrameSrv      = kFrameCount * 2; // HDR + depth, per frame

    bool createPsos(ID3D12Device* device);
    bool createTargets(ID3D12Device* device);
    void resetTargets();
    void refreshViews() const;
    uint32_t levelCount() const;
    uint32_t levelWidth(UINT index) const;
    uint32_t levelHeight(UINT index) const;
    void transitionLevel(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT index) const;
    void drawLevel(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* root, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE table,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH) const;

    GraphTargets*                m_targets  = nullptr;
    Renderer*                    m_renderer = nullptr; // deferRelease for the readbacks
    ComPtr<ID3D12RootSignature>  m_rootReduce;
    ComPtr<ID3D12RootSignature>  m_rootDownsample;
    ComPtr<ID3D12PipelineState>  m_psoReduce;
    ComPtr<ID3D12PipelineState>  m_psoDownsample;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
    UINT                         m_srvIncr = 0;
    mutable GraphTarget          m_levels[kMaxLevels]{};
    mutable bool                 m_viewsWritten = false;
    GpuResource                  m_readback[kFrameCount]{};
    void*                        m_readbackMapped[kFrameCount]{};
    bool                         m_slotWritten[kFrameCount]{};
    mutable bool                 m_loggedSkip = false;
};

} // namespace Dark
