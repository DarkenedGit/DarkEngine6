#pragma once

#include "Math/MathHelper.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Dark::Terrain
{
    struct GrassParams
    {
        bool     enabled          = false;
        float    heightMetres     = 0.55f;
        float    flexibility      = 0.65f;
        float    densityScale     = 1.0f;
        float    windBaseYaw      = 1.37f;
        float    windDeflection   = 0.55f;
        float    windTipMetres    = 0.40f;
        float    windSpatialFreq  = 0.004f;
        float    windTemporalFreq = 0.015f;
        uint32_t seed             = 1337u;
    };

    inline void clampGrassParams(GrassParams& params)
    {
        params.heightMetres     = Math::Clamp(params.heightMetres, 0.05f, 1.50f);
        params.flexibility      = Math::Clamp(params.flexibility, 0.0f, 1.0f);
        params.densityScale     = Math::Clamp(params.densityScale, 0.0f, 1.5f);
        params.windDeflection   = Math::Clamp(params.windDeflection, 0.0f, 1.20f);
        params.windTipMetres    = Math::Clamp(params.windTipMetres, 0.0f, 1.50f);
        params.windSpatialFreq  = Math::Clamp(params.windSpatialFreq, 0.0005f, 0.02f);
        params.windTemporalFreq = Math::Clamp(params.windTemporalFreq, 0.0f, 0.10f);

        float yaw = std::fmod(params.windBaseYaw, Math::TwoPi);
        if (yaw <= -Math::Pi)
            yaw += Math::TwoPi;
        else if (yaw > Math::Pi)
            yaw -= Math::TwoPi;
        params.windBaseYaw = yaw;
    }

    struct GrassBlade
    {
        float    x         = 0.0f;
        float    y         = 0.0f;
        float    z         = 0.0f;
        float    yaw       = 0.0f;
        float    heightMul = 1.0f;
        float    flexMul   = 1.0f;
        float    phase     = 0.0f;
        uint32_t tileSlot  = 0;
    };

    static_assert(sizeof(GrassBlade) == 32, "grass blade");
    static_assert(offsetof(GrassBlade, yaw) == 12, "grass blade yaw");
    static_assert(offsetof(GrassBlade, tileSlot) == 28, "grass blade tile");

    struct GrassTileWind
    {
        float windX = 0.0f;
        float windZ = 0.0f;
        float fade  = 1.0f;
        float pad   = 0.0f;
    };

    static_assert(sizeof(GrassTileWind) == 16, "grass tile wind");
    static_assert(offsetof(GrassTileWind, fade) == 8, "grass tile fade");

    struct GrassInteractor
    {
        float x        = 0.0f;
        float z        = 0.0f;
        float strength = 0.0f;
        float radius   = 0.0f;
    };

    static_assert(sizeof(GrassInteractor) == 16, "grass interactor");

    // Row-major view matrices, translation in the last row (Matrix4f::m_afEntry).
    // 528 bytes is not a CBV size; the aligned stride is kGrassFrameCbStride.
    struct GrassFrameConstants
    {
        float           viewProj[16]{};
        float           prevViewProj[16]{};
        float           heightMetres = 0.0f;
        float           flexibility  = 0.0f;
        float           pushMetres   = 0.0f;
        float           pad0         = 0.0f;
        GrassInteractor interactor[4]{};
        GrassInteractor prevInteractor[4]{};
        GrassInteractor footprint[8]{};
        GrassInteractor prevFootprint[8]{};
    };

    constexpr uint32_t kGrassFrameCbBytes  = 528u;
    constexpr uint32_t kGrassFrameCbStride = 768u;

    static_assert(sizeof(GrassFrameConstants) == kGrassFrameCbBytes, "grass frame CB");
    static_assert(kGrassFrameCbStride % 256u == 0, "grass cbv alignment");
    static_assert(kGrassFrameCbStride >= kGrassFrameCbBytes, "grass cb stride");
    static_assert(offsetof(GrassFrameConstants, heightMetres) == 128, "grass cb height");
    static_assert(offsetof(GrassFrameConstants, interactor) == 144, "grass cb interactor");
    static_assert(offsetof(GrassFrameConstants, prevInteractor) == 208, "grass cb prev interactor");
    static_assert(offsetof(GrassFrameConstants, footprint) == 272, "grass cb footprint");
    static_assert(offsetof(GrassFrameConstants, prevFootprint) == 400, "grass cb prev footprint");

    constexpr int      kGrassLodCount  = 4;
    constexpr uint32_t kGrassBladeCap  = 1048576u;
    constexpr uint32_t kGrassWindSlots = 2048u;

    constexpr uint32_t kGrassLod0Tiles   = 21u;
    constexpr uint32_t kGrassLod1Tiles   = 96u;
    constexpr uint32_t kGrassLod2Tiles   = 323u;
    constexpr uint32_t kGrassLod3Tiles   = 680u;
    constexpr uint32_t kGrassLod0PerTile = 4096u;
    constexpr uint32_t kGrassLod1PerTile = 2048u;
    constexpr uint32_t kGrassLod2PerTile = 1024u;
    constexpr uint32_t kGrassLod3PerTile = 640u;

    constexpr uint32_t kGrassLod0Slots = kGrassLod0Tiles * kGrassLod0PerTile;
    constexpr uint32_t kGrassLod1Slots = kGrassLod1Tiles * kGrassLod1PerTile;
    constexpr uint32_t kGrassLod2Slots = kGrassLod2Tiles * kGrassLod2PerTile;
    constexpr uint32_t kGrassLod3Slots = kGrassLod3Tiles * kGrassLod3PerTile;

    static_assert(kGrassLod0Slots == 86016u, "lod0 slots");
    static_assert(kGrassLod1Slots == 196608u, "lod1 slots");
    static_assert(kGrassLod2Slots == 330752u, "lod2 slots");
    static_assert(kGrassLod3Slots == 435200u, "lod3 slots");
    static_assert(kGrassLod0Slots + kGrassLod1Slots + kGrassLod2Slots + kGrassLod3Slots == kGrassBladeCap, "grass cap");

    constexpr float kGrassTileMetres         = 8.0f;
    constexpr float kGrassLod0NominalMetres  = 16.0f;
    constexpr float kGrassLod1NominalMetres  = 40.0f;
    constexpr float kGrassLod2NominalMetres  = 80.0f;
    constexpr float kGrassLod3NominalMetres  = 128.0f;
    constexpr float kGrassCreateRadiusMetres = 128.0f;
    constexpr float kGrassEvictRadiusMetres  = 147.2f;
    constexpr float kGrassFadeStartMetres    = 112.0f;
    constexpr float kGrassFadeEndMetres      = 128.0f;
    constexpr float kGrassPromoteTo0Metres   = 13.6f;
    constexpr float kGrassDemoteFrom0Metres  = 18.4f;
    constexpr float kGrassPromoteTo1Metres   = 34.0f;
    constexpr float kGrassDemoteFrom1Metres  = 46.0f;
    constexpr float kGrassPromoteTo2Metres   = 68.0f;
    constexpr float kGrassDemoteFrom2Metres  = 92.0f;

} // namespace Dark::Terrain
