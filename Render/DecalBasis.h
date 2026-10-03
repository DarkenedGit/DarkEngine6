#pragma once

#include "Render/DecalTypes.h"

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{

    struct DecalDef
    {
        DecalDefId        id          = DecalDefId::Footmark;
        DecalKind         kind        = DecalKind::Footmark;
        uint32_t          channelMask = 0;
        Math::Vector3f    tint{ 1.0f, 1.0f, 1.0f };
        float             albedoWeight    = 0.0f;
        float             normalScale     = 1.0f;
        float             normalWeight    = 0.0f;
        float             roughnessTarget = 0.0f;
        float             roughnessWeight = 0.0f;
        float             metallicTarget  = 0.0f;
        float             metallicWeight  = 0.0f;
        float             emissiveTarget  = 0.0f;
        float             emissiveWeight  = 0.0f;
        Math::Vector3f    halfExtents{ 1.0f, 1.0f, 1.0f };
        float             clipLocalYMin = -1.0f;
        float             clipLocalYMax = 1.0f;
        float             lifetime      = 1.0f;
        DecalFade         fade          = DecalFade::Linear;
        float             fadeHold      = 0.0f;
        Math::Vector3f    burnHot{ 0.85f, 0.22f, 0.04f };
        Math::Vector3f    burnCold{ 0.03f, 0.02f, 0.015f };
        float             burnAlbedoHold   = 0.55f;
        float             burnEmissiveHold = 0.25f;
        DecalNormalRecipe normalRecipe     = DecalNormalRecipe::Flat;
    };

    struct DecalBurnBake
    {
        Math::Vector3f tint{ 0.0f, 0.0f, 0.0f };
        float          albedoWeight    = 0.0f;
        float          emissiveWeight  = 0.0f;
        float          roughnessWeight = 0.0f;
        float          emissiveTarget  = 1.0f;
        float          normalWeight    = 0.0f;
    };

    struct DecalBakedNormal
    {
        Math::Vector3f n{ 0.0f, 0.0f, 1.0f };
        uint8_t        r = 128;
        uint8_t        g = 128;
        uint8_t        b = 255;
    };

    const DecalDef& decalDef(DecalDefId id);
    DecalDefId      decalSelectDef(const DecalSpawnDesc& desc);
    Math::Vector3f  decalResolveHalfExtents(const DecalDef& def, const Math::Vector3f& requested);

    // False when axisY has no direction. axisX is projected off Y; a parallel hint gets a fallback.
    bool decalBuildAxes(const Math::Vector3f& axisYIn, const Math::Vector3f& axisXIn, Math::Vector3f& outX, Math::Vector3f& outY, Math::Vector3f& outZ);

    // S * R * T. Rows of R are the unit axes. Scale is the half-extents.
    Math::Matrix4f decalWorldMatrix(const Math::Vector3f& position, const Math::Vector3f& axisX, const Math::Vector3f& axisY, const Math::Vector3f& axisZ, const Math::Vector3f& halfExtents);

    bool decalClipLocal(float x, float y, float z, float yMin, float yMax);
    bool decalClipWorld(const Math::Vector3f& worldPos, const Math::Matrix4f& decalFromWorld, float yMin, float yMax);

    float decalFadeHoldThenLinear(float u, float hold);
    float decalFade(DecalFade fade, float u, float hold);

    // False rejects (nd < start). outFade is saturate((nd - start) / range).
    bool decalAngleFade(float nd, float start, float range, float& outFade);

    void           decalTangentBasis(const Math::Vector3f& geomN, const Math::Vector3f& axisX, const Math::Vector3f& axisZ, Math::Vector3f& outT, Math::Vector3f& outB, Math::Vector3f& outN);
    Math::Vector3f decalMapNormal(const Math::Vector3f& geomN, const Math::Vector3f& axisX, const Math::Vector3f& axisZ, const Math::Vector3f& tangentSpace, float normalScale);

    uint32_t decalPackBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void     decalUnpackBytes(uint32_t raw, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a);
    uint8_t  decalUnorm8(float c);
    uint32_t decalPackUnorm(float r, float g, float b, float a);

    // Weight 0 copies that channel's bytes. A missing mask bit forces the weight to 0.
    uint32_t decalBlendAlbedo(uint32_t raw, uint32_t channelMask, float albedoWeight, float emissiveWeight, const Math::Vector3f& srcLin, float emissiveTarget);
    uint32_t decalBlendAttrib(uint32_t raw, uint32_t channelMask, float normalWeight, const Math::Vector3f& decalNormal, float roughnessWeight, float roughnessTarget, float metallicWeight,
                              float metallicTarget);

    DecalBurnBake decalBakeBurn(float u);

    void decalWeightZeroNormalBytes(uint8_t& r, uint8_t& g, uint8_t& b);

    DecalBakedNormal decalBakeNormalUv(DecalNormalRecipe recipe, float u, float v);
    DecalBakedNormal decalBakeNormalTexel(DecalNormalRecipe recipe, int texelX, int texelY);

    uint8_t decalImpactAlpha(DecalDefId id, float u, float v);

    Math::Vector3f decalShellBiasedPosition(const Math::Vector3f& hitPoint, const Math::Vector3f& hitNormal);

    bool decalCueIsGroundStep(const char* cue, const char* groundCue);
    Math::Vector3f decalDeathAxisY(const Math::Vector3f& hitNormal, const Math::Vector3f& terrainNormal);
    Math::Vector3f decalImpactAxisX(const Math::Vector3f& hitNormal, const Math::Vector3f& hitDirection);

} // namespace Dark
