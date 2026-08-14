#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Root constants for Terrain.hlsl. 44 dwords (world/view/proj + lighting + 4 tilings).
    struct TerrainFrameConstants
    {
        float worldViewProj[16];
        float world[16];
        float color[4];
        float lightDirWS[3];
        float fogDensity;
        float layerTiling[4];
        float lightColor[3];
        float cameraPosX;
        float ambientColor[3];
        float cameraPosY;
        float fogColor[3];
        float cameraPosZ;
    };

    static_assert(sizeof(TerrainFrameConstants) == 56 * sizeof(float), "terrain root constant size");

    // PSO for height-map terrain: pos/normal/uv, 4 albedo layers + 1 splat map.
    class TerrainPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kSrvCount      = 5; // layers 0-3 + splat

        TerrainPipeline() = default;

        bool create(ID3D12Device* device);

        void bind(ID3D12GraphicsCommandList* cmd) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const TerrainFrameConstants& constants) const;

        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
