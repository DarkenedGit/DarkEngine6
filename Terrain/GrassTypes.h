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
        // Edge-on cards disappear from above. These turn a blade toward the camera only while it is sideways.
        float    faceEdge         = 0.35f;
        float    faceYaw          = 0.65f;
        float    faceLean         = 0.30f;
        float    faceSteep        = 0.55f;
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
        params.windTemporalFreq = Math::Clamp(params.windTemporalFreq, 0.0f, 1.0f);
        params.faceEdge         = Math::Clamp(params.faceEdge, 0.0f, 1.0f);
        params.faceYaw          = Math::Clamp(params.faceYaw, 0.0f, 1.0f);
        params.faceLean         = Math::Clamp(params.faceLean, 0.0f, 1.0f);
        params.faceSteep        = Math::Clamp(params.faceSteep, 0.0f, 0.95f);

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
    // 592 bytes is not a CBV size; the aligned stride is kGrassFrameCbStride.
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
        float           cameraPos[4]{};
        float           cameraRight[4]{};
        float           cameraLook[4]{};
        float           faceEdge  = 0.0f;
        float           faceYaw   = 0.0f;
        float           faceLean  = 0.0f;
        float           faceSteep = 0.0f;
    };

    constexpr uint32_t kGrassFrameCbBytes  = 592u;
    constexpr uint32_t kGrassFrameCbStride = 768u;

    static_assert(sizeof(GrassFrameConstants) == kGrassFrameCbBytes, "grass frame CB");
    static_assert(kGrassFrameCbStride % 256u == 0, "grass cbv alignment");
    static_assert(kGrassFrameCbStride >= kGrassFrameCbBytes, "grass cb stride");
    static_assert(offsetof(GrassFrameConstants, heightMetres) == 128, "grass cb height");
    static_assert(offsetof(GrassFrameConstants, interactor) == 144, "grass cb interactor");
    static_assert(offsetof(GrassFrameConstants, prevInteractor) == 208, "grass cb prev interactor");
    static_assert(offsetof(GrassFrameConstants, footprint) == 272, "grass cb footprint");
    static_assert(offsetof(GrassFrameConstants, prevFootprint) == 400, "grass cb prev footprint");
    static_assert(offsetof(GrassFrameConstants, cameraPos) == 528, "grass cb camera");
    static_assert(offsetof(GrassFrameConstants, cameraRight) == 544, "grass cb camera right");
    static_assert(offsetof(GrassFrameConstants, cameraLook) == 560, "grass cb camera look");
    static_assert(offsetof(GrassFrameConstants, faceEdge) == 576, "grass cb face");

    // One instanced draw. begin/count are a contiguous span in that LOD's blade buffer.
    struct GrassTileDraw
    {
        int      lod   = 0;
        uint32_t begin = 0;
        uint32_t count = 0;
    };

    constexpr int      kGrassLodCount   = 4;
    constexpr uint32_t kGrassFrameSlots = 2u;

    // Blades on one full 8 m tile. How many tiles each band can hold is
    // grassRegionBudget(), counted from the radii in content/terrain/lod.json.
    constexpr uint32_t kGrassLod0PerTile = 4096u;
    constexpr uint32_t kGrassLod1PerTile = 2048u;
    constexpr uint32_t kGrassLod2PerTile = 1024u;
    constexpr uint32_t kGrassLod3PerTile = 640u;

    constexpr float kGrassTileMetres = 8.0f;
    // LOD band distances are content/terrain/lod.json, read through grassLod().

} // namespace Dark::Terrain
