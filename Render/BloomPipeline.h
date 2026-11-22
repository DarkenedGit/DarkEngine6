#pragma once

#include "Render/GraphTargets.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // One draw of the chain. Each is its own graph pass (render-graph plan Phase 6 part 2): a mip is
    // a render target in one step and read in the next, so the graph places the barriers between.
    enum class BloomStep : uint8_t
    {
        Extract,    // scene HDR -> mip 0
        Downsample, // mip i -> mip i + 1
        Upsample,   // mip i -> mip i - 1, additive
        Composite,  // mip 0 -> scene HDR, additive
    };

    class BloomPipeline
    {
    public:
        static constexpr UINT  kRootConstants   = 0;
        static constexpr UINT  kRootSrv         = 1;
        static constexpr float kDefaultStrength = 0.06f;
        static constexpr float kThreshold       = 1.0f;
        static constexpr float kKnee            = 0.5f;
        static constexpr UINT  kDownCount       = 5;
        static constexpr UINT  kMipCount        = 6; // extract + down[0..4]

        BloomPipeline() = default;
        ~BloomPipeline();
        BloomPipeline(const BloomPipeline&)            = delete;
        BloomPipeline& operator=(const BloomPipeline&) = delete;

        // Graph targets (render-graph plan Phase 5): the mip chain is registered output-relative and
        // Renderer::resize recreates it.
        bool create(GraphTargets& targets);
        void release();
        bool isValid() const;

        // Once per frame before the mips are imported into the graph: picks up recreated mips.
        bool prepare() const;
        ID3D12Resource*             mipResource(UINT index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE mipSrv(UINT index) const;
        D3D12_CPU_DESCRIPTOR_HANDLE mipRtv(UINT index) const;
        uint32_t                    mipWidth(UINT index) const;
        uint32_t                    mipHeight(UINT index) const;
        static const char*          mipName(UINT index); // the registered target's name

        // Records one step into `rtv` (dstW x dstH) from the one-SRV ring table `src` (srcW x srcH).
        // exposure scales kThreshold and kKnee by 1/exposure. Exposure 1 keeps those scene-luminance
        // defaults. No barriers: the graph pass declared the mips and HDR.
        void drawStep(ID3D12GraphicsCommandList* cmd, BloomStep step, D3D12_GPU_DESCRIPTOR_HANDLE src, D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH,
                      uint32_t srcW, uint32_t srcH, float strength, float exposure) const;

    private:
        static constexpr UINT kConstantCount = 8;

        bool createPsos(ID3D12Device* device);
        bool createTargets();
        void resetTargets();

        GraphTargets*                m_targets = nullptr;
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoExtract;
        ComPtr<ID3D12PipelineState>  m_psoDownsample;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoComposite;
        mutable GraphTarget          m_mips[kMipCount]{};
    };

// A pixel that is bright after exposure is the pixel that blooms. Exposure 1 keeps 1.0 and 0.5.
inline float bloomThresholdForExposure(float exposure)
{
    const float safe = exposure > 1.0e-4f ? exposure : 1.0e-4f;
    return BloomPipeline::kThreshold / safe;
}

inline float bloomKneeForExposure(float exposure)
{
    const float safe = exposure > 1.0e-4f ? exposure : 1.0e-4f;
    return BloomPipeline::kKnee / safe;
}

} // namespace Dark
