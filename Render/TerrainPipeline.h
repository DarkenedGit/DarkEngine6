#pragma once

#include "Render/DebugRenderState.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    enum class TerrainPass : uint8_t
    {
        ForwardUnorm = 0,
        ForwardHdr,
    };

    inline DXGI_FORMAT terrainPassColorFormat(TerrainPass pass)
    {
        return pass == TerrainPass::ForwardHdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // Root constants for Terrain.hlsl. Must match the HLSL cbuffer packing
    // (float3+float share a float4). 57 dwords, lighting at byte 224 = cb0[14].x.
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
        float lighting = 1.0f; // 1 = Lambert+shadow+fog, 0 = albedo only
    };

    static_assert(sizeof(TerrainFrameConstants) == 57 * sizeof(float), "terrain root constant size");
    static_assert(offsetof(TerrainFrameConstants, lightDirWS) == 144, "lightDirWS pack");
    static_assert(offsetof(TerrainFrameConstants, lighting) == 224, "lighting pack");

    // PSO for height-map terrain: pos/normal/uv, 4 albedo layers + 1 splat map.
    class TerrainPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootShadowCbv = 2;
        static constexpr UINT kSrvCount      = 6; // layers 0-3 + splat + shadow

        TerrainPipeline() = default;

        bool create(ID3D12Device* device, TerrainPass pass = TerrainPass::ForwardUnorm);

        void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const TerrainFrameConstants& constants) const;

        bool isValid() const
        {
            return m_psoSolid != nullptr && m_psoWire != nullptr && m_psoPoint != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_psoSolid;
        ComPtr<ID3D12PipelineState> m_psoWire;
        ComPtr<ID3D12PipelineState> m_psoPoint;
    };

} // namespace Dark
