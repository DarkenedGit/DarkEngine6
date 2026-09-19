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
        GBuffer,
    };

    inline DXGI_FORMAT terrainPassColorFormat(TerrainPass pass)
    {
        (void)pass;
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    struct TerrainGBufferConstants
    {
        float worldViewProj[16];
        float world[16];
        float color[4];             // rgb unused (1); a = 0 emissive — do not steal .a
        float layerTiling[4];
        float prevWorldViewProj[16];
        float    heightBlendK;      // 56
        float    heightBlendT;      // 57
        float    triplanarSlope;    // 58
        uint32_t layerTint0;        // 59  RGBA8; RGB = round(sat(linearTint)*255), A unused
        uint32_t layerTint1;        // 60
        uint32_t layerTint2;        // 61
        uint32_t layerTint3;        // 62
    };

    static_assert(sizeof(TerrainGBufferConstants) == 63 * sizeof(float), "gbuffer terrain CB");
    static_assert(offsetof(TerrainGBufferConstants, heightBlendK) == 56 * sizeof(float), "blend params append");
    static_assert(offsetof(TerrainGBufferConstants, layerTint0) == 59 * sizeof(float), "scalar uints pack tightly");
    static_assert(offsetof(TerrainGBufferConstants, layerTint3) == 62 * sizeof(float), "layerTint3 last float");
    static_assert(63 + 1 <= 64, "terrain G-buffer root signature DWORD budget");

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
        float heightFogDensity  = 0.0f;
        float heightFogFalloff  = 0.06f;
        float heightFogHeight   = 0.0f;
    };

    static_assert(sizeof(TerrainFrameConstants) == 60 * sizeof(float), "terrain root constant size");
    static_assert(offsetof(TerrainFrameConstants, lightDirWS) == 144, "lightDirWS pack");
    static_assert(offsetof(TerrainFrameConstants, lighting) == 224, "lighting pack");
    static_assert(60 + 1 + 2 <= 64, "terrain forward root signature DWORD budget");

    // PSO for height-map terrain: pos/normal/uv, 14-slot metal-rough heap (shadow last).
    class TerrainPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootShadowCbv = 2;
        static constexpr UINT kMapSrvCount   = 13; // G-buffer table (prefix, no shadow)
        static constexpr UINT kSrvCount      = 14; // forward table (shadow last)

        TerrainPipeline() = default;

        bool create(ID3D12Device* device, TerrainPass pass = TerrainPass::ForwardUnorm);

        void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
        void setConstants(ID3D12GraphicsCommandList* cmd, const TerrainFrameConstants& constants) const;
        void setGBufferConstants(ID3D12GraphicsCommandList* cmd, const TerrainGBufferConstants& constants) const;

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
