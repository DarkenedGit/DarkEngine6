#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Root constants for Sprite.hlsl (worldViewProj + tint + UV).
    struct SpriteConstants
    {
        float worldViewProj[16];
        float color[4];
        float uvScale[2];
        float uvOffset[2];
    };

    static_assert(sizeof(SpriteConstants) == 24 * sizeof(float), "sprite root constant size");

    // Unlit textured quad PSO for Camera2D / side-view sprites.
    class SpritePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootAlbedoSrv = 1;

        SpritePipeline() = default;

        bool create(ID3D12Device* device);

        void bind(ID3D12GraphicsCommandList* cmd) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const SpriteConstants& constants) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
