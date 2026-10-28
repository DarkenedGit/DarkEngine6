#pragma once

#include <cstddef>
#include <cstdint>

namespace Dark::Terrain
{
    // Mirror of the HLSL cbuffer FoliageView : register(b1)
    // One per (frameSlot, view)
    struct FoliageViewConstants
    {
        float viewProj[16];     // row-major, GBuffer only
        float prevViewProj[16]; // row-major, GBuffer only
        float windCur[4];       // x=sin(heading), y=cos(heading), z=strength*gust(t), w=time
        float windPrev[4];      // same at prevTime
        float field[4];         // spatialFreq, temporalFreq, deflection, asfloat(seed)
        float kindA[4][4];      // stiffness, mainBendMetres*influence, branchAmp*influence, edgeAmp*influence
        float kindB[4][4];      // flutterFreq, detailFadeStart, detailFadeEnd, bendFadeStart
        float kindC[4][4];      // bendFadeEnd, kindHeight (bounds max.y), influence, pad
        float eye[4];           // camera position xyz, enabled(1/0)
    };

    static_assert(sizeof(FoliageViewConstants) == 384, "FoliageViewConstants size");
    static_assert(offsetof(FoliageViewConstants, viewProj) == 0, "viewProj offset");
    static_assert(offsetof(FoliageViewConstants, prevViewProj) == 64, "prevViewProj offset");
    static_assert(offsetof(FoliageViewConstants, windCur) == 128, "windCur offset");
    static_assert(offsetof(FoliageViewConstants, windPrev) == 144, "windPrev offset");
    static_assert(offsetof(FoliageViewConstants, field) == 160, "field offset");
    static_assert(offsetof(FoliageViewConstants, kindA) == 176, "kindA offset");
    static_assert(offsetof(FoliageViewConstants, kindB) == 240, "kindB offset");
    static_assert(offsetof(FoliageViewConstants, kindC) == 304, "kindC offset");
    static_assert(offsetof(FoliageViewConstants, eye) == 368, "eye offset");

    constexpr uint32_t kFoliageViewCbStride = 512; // %256==0

} // namespace Dark::Terrain
