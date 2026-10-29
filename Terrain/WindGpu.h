#pragma once

#include <cstddef>
#include <cstdint>

namespace Dark::Terrain
{
    // b1 for foliage G-buffer and depth. viewProj is row-major, translation in the last row.
    // wind xyz is sin(heading), cos(heading), strength * gust. w is time in seconds.
    // field is spatialFreq, temporalFreq, deflection, and the seed bits.
    // kindA is stiffness, mainBend*influence, branchAmp*influence, edgeAmp*influence.
    // kindB is flutterFreq, detailFadeStart, detailFadeEnd, bendFadeStart.
    // kindC is bendFadeEnd, drawn height, influence, pad.
    // eye xyz is the viewer. w > 0.5 enables the bend.
    struct FoliageViewConstants
    {
        float viewProj[16];
        float prevViewProj[16];
        float windCur[4];
        float windPrev[4];
        float field[4];
        float kindA[4][4];
        float kindB[4][4];
        float kindC[4][4];
        float eye[4];
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

    constexpr uint32_t kFoliageViewCbStride = 512;

    static_assert(kFoliageViewCbStride % 256u == 0u, "foliage view cb align");
    static_assert(kFoliageViewCbStride >= sizeof(FoliageViewConstants), "foliage view cb stride");

} // namespace Dark::Terrain
