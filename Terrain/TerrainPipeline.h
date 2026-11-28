#pragma once

#include "Render/DebugRenderState.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

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

    // PSO for height-map terrain: pos/normal/uv, 14-slot metal-rough heap (shadow last).
    class TerrainPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kMapSrvCount   = 13; // G-buffer table (heap prefix, no shadow)
        static constexpr UINT kSrvCount      = 14; // material heap size (shadow slot last, unused by the G-buffer PSO)

        TerrainPipeline() = default;

        // G-buffer only (D2): the forward terrain PSO and Terrain.hlsl are gone.
        bool create(ID3D12Device* device);

        void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
        void setWriteSurfaceTag(bool on) { m_writeSurfaceTag = on; }
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
        ComPtr<ID3D12PipelineState> m_psoSurfaceTag;
        bool                        m_writeSurfaceTag = false;
    };

} // namespace Dark
