#pragma once

#include "Render/SsrSettings.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <string>
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
        float ssrEnabled;       // register 11.z so float3 does not straddle
        float padPbr1;          // occupy register 11.w
        float pbrLightColor[3]; // π-scaled; PbrDirectional only. Starts at float 48 = register 12.xyz
        float iblIntensity;     // register 12.w
        float iblRotationRadY;
        float iblMaxRoughnessMip;
        float iblEnabled;
        float iblDebug;
    };

    static_assert(sizeof(LightingConstants) == 56 * sizeof(float), "lighting root constants");
    static_assert(offsetof(LightingConstants, ssrEnabled) == 46 * sizeof(float), "ssrEnabled occupies register 11.z");
    static_assert(offsetof(LightingConstants, pbrLightColor) == 48 * sizeof(float), "float3 must start on a 16-byte boundary");
    static_assert(offsetof(LightingConstants, iblIntensity) == 51 * sizeof(float), "iblIntensity shares register 12.w");
    static_assert(56 + 1 + 2 + 1 + 1 + 1 + 1 <= 64, "deferred lighting RS DWORD budget");

    inline constexpr char kDefaultIblVirtualPath[] = "env/studio_gradient.hdr";

    // Host knobs. Not on Sky::Environment. enabled is false until bake OK.
    struct IblSettings
    {
        std::string virtualPath  = kDefaultIblVirtualPath;
        float       intensity    = 1.0f;
        float       rotationRadY = 0.0f;
        bool        enabled      = false;
    };

    inline void fillIblLightingConstants(LightingConstants& lc, const IblSettings& ibl, bool debugEnabled, int debugView, bool iblReady)
    {
        lc.iblIntensity       = ibl.intensity;
        lc.iblRotationRadY    = ibl.rotationRadY;
        lc.iblMaxRoughnessMip = 4.0f;
        lc.iblEnabled         = (ibl.enabled && debugEnabled && iblReady) ? 1.0f : 0.0f;
        const int d           = debugView < 0 ? 0 : (debugView > 3 ? 3 : debugView);
        lc.iblDebug           = static_cast<float>(d);
    }

    inline void fillSsrLightingConstants(LightingConstants& lc, const SsrSettings& settings, bool debugEnabled, bool pipelineValid)
    {
        lc.ssrEnabled = (settings.enabled && debugEnabled && pipelineValid) ? 1.0f : 0.0f;
    }

    class DeferredLightingPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootShadowCbv = 2;
        static constexpr UINT kRootHeightSrv = 3;
        static constexpr UINT kRootAoSrv     = 4;
        static constexpr UINT kRootIblSrv    = 5;
        static constexpr UINT kRootSsrSrv    = 6;

        DeferredLightingPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const ShadowSystem& shadows, const LightingConstants& constants) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
