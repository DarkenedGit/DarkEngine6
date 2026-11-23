#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // View-projection only; billboards expanded on CPU.
    struct ParticleFrameConstants
    {
        float viewProj[16];
    };

    static_assert(sizeof(ParticleFrameConstants) == 16 * sizeof(float), "particle constants");

    // Additive (or alpha) textured billboard PSO for particle quads.
    class ParticlePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        ParticlePipeline() = default;

        bool create(ID3D12Device* device, bool additive, int depthBias = 0, float slopeScaledDepthBias = 0.0f,
                    DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
        void bind(ID3D12GraphicsCommandList* cmd) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const ParticleFrameConstants& c) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
