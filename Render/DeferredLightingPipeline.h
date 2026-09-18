#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class ShadowSystem;

    using Microsoft::WRL::ComPtr;

    struct LightingConstants
    {
        float invViewProj[16];
        float cameraPos[3];
        float fogDensity;
        float lightDirWS[3];
        float lighting;
        float lightColor[3];
        float emissiveGain; // default 4
        float ambientColor[3];
        float heightFogDensity;
        float fogColor[3];
        float heightFogFalloff;
        float heightFogHeight;
        float volumetricFogDensity;
        float waterLevel;
        float volumetricHeight;
        float heightOriginX;
        float heightOriginZ;
        float heightCellSize;
        float heightWorldSizeX;
        float heightWorldSizeZ;
        float padFog;
        float padPbr0;          // occupy register 11.z so float3 does not straddle
        float padPbr1;          // occupy register 11.w
        float pbrLightColor[3]; // π-scaled; PbrDirectional only. Starts at float 48 = register 12.xyz
    };

    static_assert(sizeof(LightingConstants) == 51 * sizeof(float), "lighting root constants");
    static_assert(offsetof(LightingConstants, pbrLightColor) == 48 * sizeof(float), "float3 must start on a 16-byte boundary");

    class DeferredLightingPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootShadowCbv = 2;
        static constexpr UINT kRootHeightSrv = 3;
        static constexpr UINT kRootAoSrv     = 4;

        DeferredLightingPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const ShadowSystem& shadows, const LightingConstants& constants) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
