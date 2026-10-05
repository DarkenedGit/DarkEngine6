#pragma once

#include <cstdint>

namespace Dark
{

    enum class DebugFill : uint8_t
    {
        Solid = 0,
        Wireframe,
        Points,
    };

    struct DebugRenderState
    {
        DebugFill fill        = DebugFill::Solid;
        bool      lighting    = true;
        bool      localLights = true;
        bool      clouds      = false; // Fullscreen march of a volume that only covers part of the sky.
        bool      bloom       = true;
        bool      shadows     = true;
        bool      aces        = false; // Narkowicz display curve on HDR tonemap
        bool      motionBlur  = true;
        bool      taa         = true;
        bool      legacyUnormAlbedo = false; // pack albedo as raw UNORM (skip sRGB decode)
        bool      showAlbedoLinear  = false; // lighting-off: linear albedo in HDR, then saturate IEC
        bool      showAlbedoRaw     = false; // lighting heap slot 0 = UNORM (no sRGB decode)
        bool      iblEnabled        = true; // AND with IblSettings.enabled and GpuIbl::isReady()
        int       iblDebug          = 0;    // 0 off, 1 irradiance, 2 prefiltered lod0, 3 LUT
        bool      ssaoEnabled       = false; // AND with GtaoSettings.enabled
        int       ssaoDebug         = 0;    // 0 off, 1 AoFull overlay tile
        bool      ssrEnabled        = true; // AND with SsrSettings.enabled
        int       ssrDebug          = 0;    // 0 off, 1 radiance, 2 confidence
        int       localShadowDebug  = 0;    // 0 off, 1 shadow factor, 2 face id (volume pass only)
        bool      shadowMapTiles    = false;
        bool      decalsEnabled     = false; // AND with HybridDeferred before any UAV. Struct stays false; hosts set the checkbox.
        int       decalsDebug       = 0;     // 0 off, 1 wire volumes

        bool lightingActive() const { return lighting && !showAlbedoLinear; }

        void cycleFill();
    };

    const char* toString(DebugFill fill);

} // namespace Dark
