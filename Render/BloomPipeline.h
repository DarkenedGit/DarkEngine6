#pragma once

#include "Render/GraphTargets.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

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
        // Renderer::resize recreates it; draw() rewrites the static mip SRVs on a new generation.
        bool create(GraphTargets& targets);
        void release();
        // exposure scales kThreshold and kKnee by 1/exposure. Exposure 1 keeps those scene-luminance defaults.
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float strength, float exposure) const;

        bool isValid() const;

    private:
        static constexpr UINT kHdrSlots      = 2;
        static constexpr UINT kConstantCount = 8;
        static constexpr UINT kSrvCount      = kHdrSlots + kMipCount;

        bool createPsos(ID3D12Device* device);
        bool createTargets(ID3D12Device* device);
        void resetTargets();
        void refreshViews() const;
        uint32_t mipWidth(UINT index) const;
        uint32_t mipHeight(UINT index) const;
        void transitionMip(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT slot) const;
        void drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srcGpu,
                            D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH, float strength, float exposure) const;

        GraphTargets*                m_targets = nullptr;
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoExtract;
        ComPtr<ID3D12PipelineState>  m_psoDownsample;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoComposite;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
        UINT                         m_srvIncr = 0;
        mutable GraphTarget          m_mips[kMipCount]{};
        mutable bool                 m_viewsWritten = false;
        mutable bool                 m_loggedSkip = false;
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
