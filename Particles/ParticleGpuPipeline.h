#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Compute integrate plus a camera-facing billboard PSO. No Particles/ include.
    class ParticleGpuPipeline
    {
    public:
        static constexpr UINT kSimCb        = 0;
        static constexpr UINT kSimPrev      = 1;
        static constexpr UINT kSimCurr      = 2;
        static constexpr UINT kSimStep      = 3;
        static constexpr UINT kSimTextures  = 4;
        static constexpr UINT kSimEvents    = 5;
        static constexpr UINT kSimCounter   = 6;
        static constexpr UINT kSimChildHead = 7;

        static constexpr UINT kDrawCb         = 0;
        static constexpr UINT kDrawParticles  = 1;
        static constexpr UINT kDrawSprite     = 2;
        static constexpr UINT kDrawArgs       = 3;

        ParticleGpuPipeline() = default;

        bool create(ID3D12Device* device, DXGI_FORMAT colorFormat);
        void destroy();

        bool isValid() const
        {
            return m_simCopy != nullptr && m_simInPlace != nullptr && m_simConsume != nullptr && m_drawAdditive != nullptr && m_drawAlpha != nullptr;
        }

        void bindSimulate(ID3D12GraphicsCommandList* cmd, bool inPlace) const;
        void bindConsume(ID3D12GraphicsCommandList* cmd) const;
        void bindDraw(ID3D12GraphicsCommandList* cmd, bool additive) const;

    private:
        ComPtr<ID3D12RootSignature> m_simRoot;
        ComPtr<ID3D12PipelineState> m_simCopy;
        ComPtr<ID3D12PipelineState> m_simInPlace;
        ComPtr<ID3D12PipelineState> m_simConsume;
        ComPtr<ID3D12RootSignature> m_drawRoot;
        ComPtr<ID3D12PipelineState> m_drawAdditive;
        ComPtr<ID3D12PipelineState> m_drawAlpha;
    };

} // namespace Dark
