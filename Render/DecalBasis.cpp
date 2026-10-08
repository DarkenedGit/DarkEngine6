#include "Render/DecalBasis.h"

#include "Core/Log.h"
#include "Math/Color.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"
#include "Math/Vector4f.h"
#include "Render/Octahedral.h"

#include <cmath>
#include <cstring>

namespace Dark
{
    using namespace Math;

    namespace
    {

        constexpr float kBulletPitRadius   = 0.45f;
        constexpr float kBulletRimRadius   = 0.75f;
        constexpr float kBulletRimCenter   = 0.60f;
        constexpr float kBulletRimBand     = 0.15f;
        constexpr float kBulletAlphaRadius = 0.70f;
        constexpr float kSlashRadiusX      = 0.92f;
        constexpr float kSlashRadiusY      = 0.22f;
        constexpr float kFootRadiusX       = 0.85f;
        constexpr float kFootRadiusY       = 0.45f;

        DecalDef makeFoot()
        {
            DecalDef d{};
            d.id            = DecalDefId::Footmark;
            d.kind          = DecalKind::Footmark;
            d.channelMask   = DecalChannel_Albedo | DecalChannel_Normal;
            d.tint          = Vector3f(0.25f, 0.22f, 0.18f);
            d.albedoWeight  = 0.45f;
            d.normalScale   = 0.65f;
            d.normalWeight  = 0.85f;
            d.halfExtents   = Vector3f(0.14f, 0.08f, 0.26f);
            d.clipLocalYMin = -1.0f;
            // 2 cm of air over the 8 cm half-extent. A centered slab would include the sole.
            d.clipLocalYMax = kDecalFootAbovePlaneMeters / d.halfExtents.y;
            d.lifetime      = 8.0f;
            d.fade          = DecalFade::Smoothstep;
            d.normalRecipe  = DecalNormalRecipe::FootDent;
            return d;
        }

        DecalDef makeBloodHit()
        {
            DecalDef d{};
            d.id            = DecalDefId::BloodHit;
            d.kind          = DecalKind::Blood;
            d.channelMask   = DecalChannel_Albedo | DecalChannel_Normal;
            d.tint          = Vector3f(1.0f, 1.0f, 1.0f);
            d.albedoWeight  = 0.85f;
            d.normalScale   = 0.4f;
            d.normalWeight  = 0.35f;
            d.halfExtents   = Vector3f(0.10f, 0.10f, 0.10f);
            d.clipLocalYMin = -1.0f;
            d.clipLocalYMax = 1.0f;
            d.lifetime      = 12.0f;
            d.fade          = DecalFade::Smoothstep;
            d.normalRecipe  = DecalNormalRecipe::BloodBump;
            return d;
        }

        DecalDef makeBloodDeath()
        {
            DecalDef d{};
            d.id            = DecalDefId::BloodDeath;
            d.kind          = DecalKind::Blood;
            d.channelMask   = DecalChannel_Albedo;
            d.tint          = Vector3f(1.0f, 1.0f, 1.0f);
            d.albedoWeight  = 1.0f;
            d.normalScale   = 1.0f;
            d.normalWeight  = 0.0f;
            d.halfExtents   = Vector3f(1.8f, 0.12f, 1.8f);
            d.clipLocalYMin = -1.0f;
            d.clipLocalYMax = 1.0f;
            d.lifetime      = 20.0f;
            d.fade          = DecalFade::HoldThenLinear;
            d.fadeHold      = 0.70f;
            d.normalRecipe  = DecalNormalRecipe::Flat;
            return d;
        }

        DecalDef makeBullet()
        {
            DecalDef d{};
            d.id              = DecalDefId::ImpactBullet;
            d.kind            = DecalKind::Impact;
            d.channelMask     = DecalChannel_Albedo | DecalChannel_Normal | DecalChannel_Roughness;
            d.tint            = Vector3f(0.04f, 0.035f, 0.03f);
            d.albedoWeight    = 0.9f;
            d.normalScale     = 1.0f;
            d.normalWeight    = 1.0f;
            d.roughnessTarget = 0.85f;
            d.roughnessWeight = 0.5f;
            d.halfExtents     = Vector3f(0.06f, 0.04f, 0.06f);
            d.clipLocalYMin   = -1.0f;
            d.clipLocalYMax   = 1.0f;
            d.lifetime        = 45.0f;
            d.fade            = DecalFade::HoldThenLinear;
            d.fadeHold        = 0.85f;
            d.normalRecipe    = DecalNormalRecipe::ImpactBullet;
            return d;
        }

        DecalDef makeSlash()
        {
            DecalDef d{};
            d.id              = DecalDefId::ImpactSlash;
            d.kind            = DecalKind::Impact;
            d.channelMask     = DecalChannel_Albedo | DecalChannel_Normal | DecalChannel_Roughness;
            d.tint            = Vector3f(0.09f, 0.07f, 0.05f);
            d.albedoWeight    = 0.9f;
            d.normalScale     = 1.0f;
            d.normalWeight    = 1.0f;
            d.roughnessTarget = 0.72f;
            d.roughnessWeight = 0.45f;
            d.halfExtents     = Vector3f(0.22f, 0.05f, 0.07f);
            d.clipLocalYMin   = -1.0f;
            d.clipLocalYMax   = 1.0f;
            d.lifetime        = 30.0f;
            d.fade            = DecalFade::HoldThenLinear;
            d.fadeHold        = 0.75f;
            d.normalRecipe    = DecalNormalRecipe::ImpactSlash;
            return d;
        }

        DecalDef makeBurn()
        {
            DecalDef d{};
            d.id               = DecalDefId::Burn;
            d.kind             = DecalKind::Burn;
            d.channelMask      = DecalChannel_Albedo | DecalChannel_Roughness | DecalChannel_Emissive;
            d.tint             = Vector3f(1.0f, 1.0f, 1.0f);
            d.albedoWeight     = 0.85f;
            d.normalScale      = 1.0f;
            d.normalWeight     = 0.0f;
            d.roughnessTarget  = 0.95f;
            d.roughnessWeight  = 0.8f;
            d.emissiveTarget   = 1.0f;
            d.emissiveWeight   = 1.0f;
            d.halfExtents      = Vector3f(0.35f, 0.08f, 0.35f);
            d.clipLocalYMin    = -1.0f;
            d.clipLocalYMax    = 1.0f;
            d.lifetime         = 15.0f;
            d.fade             = DecalFade::HoldThenLinear;
            d.fadeHold         = 0.55f;
            d.burnHot          = Vector3f(0.85f, 0.22f, 0.04f);
            d.burnCold         = Vector3f(0.03f, 0.02f, 0.015f);
            d.burnAlbedoHold   = 0.55f;
            d.burnEmissiveHold = 0.25f;
            d.normalRecipe     = DecalNormalRecipe::Flat;
            return d;
        }

        const DecalDef* defTable()
        {
            static const DecalDef kDefs[] = {
                makeFoot(), makeBloodHit(), makeBloodDeath(), makeBullet(), makeSlash(), makeBurn(),
            };
            static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == static_cast<size_t>(DecalDefId::Count), "def table");
            return kDefs;
        }

        float saturate(float u)
        {
            return Clamp(u, 0.0f, 1.0f);
        }

        float recipeHeight(DecalNormalRecipe recipe, float u, float v)
        {
            switch (recipe)
            {
            case DecalNormalRecipe::Flat:
                return 0.0f;
            case DecalNormalRecipe::BloodBump:
            {
                const float r = Vector2f(u, v).Magnitude();
                if (r >= 1.0f)
                    return 0.0f;
                const float e = 1.0f - r;
                return e * e * 0.35f;
            }
            case DecalNormalRecipe::FootDent:
            {
                const float rx = u / kFootRadiusX;
                const float ry = v / kFootRadiusY;
                const float r  = Vector2f(rx, ry).Magnitude();
                if (r >= 1.0f)
                    return 0.0f;
                const float e = 1.0f - r * r;
                return -(e * e) * 0.55f;
            }
            case DecalNormalRecipe::ImpactBullet:
            {
                const float r = Vector2f(u, v).Magnitude();
                if (r < kBulletPitRadius)
                {
                    const float e = 1.0f - r / kBulletPitRadius;
                    return -0.9f * e * e;
                }
                if (r < kBulletRimRadius)
                    return 0.25f * (1.0f - std::fabs(r - kBulletRimCenter) / kBulletRimBand);
                return 0.0f;
            }
            case DecalNormalRecipe::ImpactSlash:
            {
                const float rx = u / kSlashRadiusX;
                const float ry = v / kSlashRadiusY;
                const float r  = Vector2f(rx, ry).Magnitude();
                if (r >= 1.0f)
                    return 0.0f;
                const float e = 1.0f - r * r;
                return -0.55f * e * e;
            }
            }
            return 0.0f;
        }

        uint8_t quantizeNormalChannel(float c)
        {
            return static_cast<uint8_t>((c * 0.5f + 0.5f) * 255.0f + 0.5f);
        }

    } // namespace

    const DecalDef& decalDef(DecalDefId id)
    {
        const auto index = static_cast<uint32_t>(id);
        if (index >= static_cast<uint32_t>(DecalDefId::Count))
        {
            DE_ASSERT(false, "decal def id");
            return defTable()[0];
        }
        return defTable()[index];
    }

    DecalDefId decalSelectDef(const DecalSpawnDesc& desc)
    {
        switch (desc.kind)
        {
        case DecalKind::Footmark:
            return DecalDefId::Footmark;
        case DecalKind::Blood:
            return desc.space == DecalSpace::World ? DecalDefId::BloodDeath : DecalDefId::BloodHit;
        case DecalKind::Impact:
            return desc.weapon == WeaponKind::Projectile ? DecalDefId::ImpactBullet : DecalDefId::ImpactSlash;
        case DecalKind::Burn:
            return DecalDefId::Burn;
        case DecalKind::Count:
            break;
        }
        return DecalDefId::Count;
    }

    Vector3f decalResolveHalfExtents(const DecalDef& def, const Vector3f& requested)
    {
        const bool any = requested.x > 0.0f || requested.y > 0.0f || requested.z > 0.0f;
        if (!any)
            return def.halfExtents;
        Vector3f h = requested;
        if (h.x <= 0.0f)
            h.x = def.halfExtents.x;
        if (h.y <= 0.0f)
            h.y = def.halfExtents.y;
        if (h.z <= 0.0f)
            h.z = def.halfExtents.z;
        return h;
    }

    bool decalBuildAxes(const Vector3f& axisYIn, const Vector3f& axisXIn, Vector3f& outX, Vector3f& outY, Vector3f& outZ)
    {
        outY = axisYIn;
        if (outY.MagnitudeSqrd() <= 1.0e-8f)
            return false;
        outY.Normalize();

        outX = axisXIn - outY * outY.Dot(axisXIn);
        if (outX.MagnitudeSqrd() <= 1.0e-8f)
        {
            const Vector3f hint = (std::fabs(outY.y) < 0.9f) ? Vector3f(0.0f, 1.0f, 0.0f) : Vector3f(1.0f, 0.0f, 0.0f);
            outX                = hint - outY * outY.Dot(hint);
        }
        if (outX.MagnitudeSqrd() <= 1.0e-8f)
            return false;
        outX.Normalize();

        outZ = outX.Cross(outY);
        if (outZ.MagnitudeSqrd() <= 1.0e-8f)
            return false;
        outZ.Normalize();
        return true;
    }

    Matrix4f decalWorldMatrix(const Vector3f& position, const Vector3f& axisX, const Vector3f& axisY, const Vector3f& axisZ, const Vector3f& halfExtents)
    {
        Matrix4f R = Matrix4f::IDENTITY;
        R.SetRow(0, axisX);
        R.SetRow(1, axisY);
        R.SetRow(2, axisZ);
        const Matrix4f S = Matrix4f::ScaleMatrixXYZ(halfExtents.x, halfExtents.y, halfExtents.z);
        const Matrix4f T = Matrix4f::TranslationMatrix(position.x, position.y, position.z);
        return S * R * T;
    }

    bool decalClipLocal(float x, float y, float z, float yMin, float yMax)
    {
        if (std::fabs(x) > 1.0f || std::fabs(z) > 1.0f)
            return false;
        if (y < yMin || y > yMax)
            return false;
        return true;
    }

    bool decalClipWorld(const Vector3f& worldPos, const Matrix4f& decalFromWorld, float yMin, float yMax)
    {
        const Vector4f local = decalFromWorld * Vector4f(worldPos, 1.0f);
        return decalClipLocal(local.x, local.y, local.z, yMin, yMax);
    }

    float decalFadeHoldThenLinear(float u, float hold)
    {
        u = saturate(u);
        if (hold >= 1.0f)
            return 1.0f;
        if (u < hold)
            return 1.0f;
        return 1.0f - (u - hold) / (1.0f - hold);
    }

    float decalFade(DecalFade fade, float u, float hold)
    {
        u = saturate(u);
        switch (fade)
        {
        case DecalFade::Smoothstep:
            return 1.0f - SmoothStep(0.0f, 1.0f, u);
        case DecalFade::HoldThenLinear:
            return decalFadeHoldThenLinear(u, hold);
        case DecalFade::Linear:
            break;
        }
        return 1.0f - u;
    }

    bool decalAngleFade(float nd, float start, float range, float& outFade)
    {
        if (nd < start)
            return false;
        if (range <= 1.0e-6f)
        {
            outFade = 1.0f;
            return true;
        }
        outFade = saturate((nd - start) / range);
        return true;
    }

    void decalTangentBasis(const Vector3f& geomN, const Vector3f& axisX, const Vector3f& axisZ, Vector3f& outT, Vector3f& outB, Vector3f& outN)
    {
        outN = geomN;
        outN.Normalize();
        outT = axisX - outN * outN.Dot(axisX);
        if (outT.Dot(outT) < 1.0e-6f)
            outT = axisZ - outN * outN.Dot(axisZ);
        outT.Normalize();
        outB = outN.Cross(outT);
        if (outB.Dot(axisZ) < 0.0f)
            outB = -outB;
    }

    Vector3f decalMapNormal(const Vector3f& geomN, const Vector3f& axisX, const Vector3f& axisZ, const Vector3f& tangentSpace, float normalScale)
    {
        Vector3f T;
        Vector3f B;
        Vector3f N;
        decalTangentBasis(geomN, axisX, axisZ, T, B, N);
        Vector3f n = T * (tangentSpace.x * normalScale) + B * (tangentSpace.y * normalScale) + N * tangentSpace.z;
        n.Normalize();
        return n;
    }

    uint32_t decalPackBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }

    void decalUnpackBytes(uint32_t raw, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a)
    {
        r = static_cast<uint8_t>(raw & 0xFFu);
        g = static_cast<uint8_t>((raw >> 8) & 0xFFu);
        b = static_cast<uint8_t>((raw >> 16) & 0xFFu);
        a = static_cast<uint8_t>((raw >> 24) & 0xFFu);
    }

    uint8_t decalUnorm8(float c)
    {
        c = saturate(c);
        return static_cast<uint8_t>(c * 255.0f + 0.5f);
    }

    uint32_t decalPackUnorm(float r, float g, float b, float a)
    {
        return decalPackBytes(decalUnorm8(r), decalUnorm8(g), decalUnorm8(b), decalUnorm8(a));
    }

    uint32_t decalBlendAlbedo(uint32_t raw, uint32_t channelMask, float albedoWeight, float emissiveWeight, const Vector3f& srcLin, float emissiveTarget)
    {
        float wA = (channelMask & DecalChannel_Albedo) ? saturate(albedoWeight) : 0.0f;
        float wE = (channelMask & DecalChannel_Emissive) ? saturate(emissiveWeight) : 0.0f;
        if (wA <= 0.0f && wE <= 0.0f)
            return raw;

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        decalUnpackBytes(raw, r, g, b, a);

        const float dstR = ::Dark::Color::srgb8ToLinear(r);
        const float dstG = ::Dark::Color::srgb8ToLinear(g);
        const float dstB = ::Dark::Color::srgb8ToLinear(b);
        const float dstE = static_cast<float>(a) / 255.0f;
        const float outR = dstR + (srcLin.x - dstR) * wA;
        const float outG = dstG + (srcLin.y - dstG) * wA;
        const float outB = dstB + (srcLin.z - dstB) * wA;
        const float outE = dstE + (emissiveTarget - dstE) * wE;

        const uint8_t pr = wA > 0.0f ? ::Dark::Color::linearToSrgb8(outR) : r;
        const uint8_t pg = wA > 0.0f ? ::Dark::Color::linearToSrgb8(outG) : g;
        const uint8_t pb = wA > 0.0f ? ::Dark::Color::linearToSrgb8(outB) : b;
        const uint8_t pa = wE > 0.0f ? decalUnorm8(outE) : a;
        return decalPackBytes(pr, pg, pb, pa);
    }

    uint32_t decalBlendAttrib(uint32_t raw, uint32_t channelMask, float normalWeight, const Vector3f& decalNormal, float roughnessWeight, float roughnessTarget, float metallicWeight,
                              float metallicTarget)
    {
        const float wN = (channelMask & DecalChannel_Normal) ? saturate(normalWeight) : 0.0f;
        const float wR = (channelMask & DecalChannel_Roughness) ? saturate(roughnessWeight) : 0.0f;
        const float wM = (channelMask & DecalChannel_Metallic) ? saturate(metallicWeight) : 0.0f;
        if (wN <= 0.0f && wR <= 0.0f && wM <= 0.0f)
            return raw;

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        decalUnpackBytes(raw, r, g, b, a);

        uint8_t pr = r;
        uint8_t pg = g;
        if (wN > 0.0f)
        {
            const Vector2f oct = encodeOct(decalNormal);
            pr                 = decalUnorm8(oct.x);
            pg                 = decalUnorm8(oct.y);
        }

        uint8_t pb = b;
        if (wR > 0.0f)
        {
            const float dst = static_cast<float>(b) / 255.0f;
            pb              = decalUnorm8(dst + (roughnessTarget - dst) * wR);
        }

        uint8_t pa = a;
        if (wM > 0.0f)
        {
            const float dst = static_cast<float>(a) / 255.0f;
            pa              = decalUnorm8(dst + (metallicTarget - dst) * wM);
        }
        return decalPackBytes(pr, pg, pb, pa);
    }

    DecalBurnBake decalBakeBurn(float u)
    {
        const DecalDef& def = decalDef(DecalDefId::Burn);
        u                   = saturate(u);
        const float eFade   = decalFadeHoldThenLinear(u, def.burnEmissiveHold);
        const float aFade   = decalFadeHoldThenLinear(u, def.burnAlbedoHold);

        DecalBurnBake bake{};
        bake.tint            = Lerp(def.burnCold, def.burnHot, eFade);
        bake.albedoWeight    = def.albedoWeight * aFade;
        bake.emissiveWeight  = def.emissiveWeight * eFade;
        bake.roughnessWeight = def.roughnessWeight * aFade;
        bake.emissiveTarget  = def.emissiveTarget;
        bake.normalWeight    = 0.0f;
        return bake;
    }

    void decalWeightZeroNormalBytes(uint8_t& r, uint8_t& g, uint8_t& b)
    {
        r = 128;
        g = 128;
        b = 255;
    }

    DecalBakedNormal decalBakeNormalUv(DecalNormalRecipe recipe, float u, float v)
    {
        const float du   = 2.0f / static_cast<float>(kDecalNormalMapSize);
        const float hL   = recipeHeight(recipe, u - du, v);
        const float hR   = recipeHeight(recipe, u + du, v);
        const float hD   = recipeHeight(recipe, u, v - du);
        const float hU   = recipeHeight(recipe, u, v + du);
        const float dhdu = (hR - hL) / (2.0f * du);
        const float dhdv = (hU - hD) / (2.0f * du);

        Vector3f n(-dhdu, -dhdv, 1.0f);
        n.Normalize();

        DecalBakedNormal out{};
        out.n = n;
        out.r = quantizeNormalChannel(n.x);
        out.g = quantizeNormalChannel(n.y);
        out.b = quantizeNormalChannel(n.z);
        return out;
    }

    DecalBakedNormal decalBakeNormalTexel(DecalNormalRecipe recipe, int texelX, int texelY)
    {
        const float res = static_cast<float>(kDecalNormalMapSize);
        const float u   = (static_cast<float>(texelX) + 0.5f) / res * 2.0f - 1.0f;
        const float v   = (static_cast<float>(texelY) + 0.5f) / res * 2.0f - 1.0f;
        return decalBakeNormalUv(recipe, u, v);
    }

    uint8_t decalImpactAlpha(DecalDefId id, float u, float v)
    {
        if (id == DecalDefId::ImpactBullet)
            return Vector2f(u, v).Magnitude() < kBulletAlphaRadius ? static_cast<uint8_t>(255) : static_cast<uint8_t>(0);
        if (id == DecalDefId::ImpactSlash)
        {
            const float r = Vector2f(u / kSlashRadiusX, v / kSlashRadiusY).Magnitude();
            return r < 1.0f ? static_cast<uint8_t>(255) : static_cast<uint8_t>(0);
        }
        return 255;
    }

    Vector3f decalShellBiasedPosition(const Vector3f& hitPoint, const Vector3f& hitNormal)
    {
        Vector3f n = hitNormal;
        if (n.MagnitudeSqrd() <= 1.0e-8f)
            return hitPoint;
        n.Normalize();
        return hitPoint - n * kDecalShellBiasMeters;
    }

    bool decalCueIsGroundStep(const char* cue, const char* groundCue)
    {
        if (!cue || cue[0] == '\0')
            return false;
        if (std::strcmp(cue, "step") == 0)
            return true;
        if (!groundCue || groundCue[0] == '\0')
            return false;
        return std::strcmp(cue, groundCue) == 0;
    }

    Vector3f decalDeathAxisY(const Vector3f& hitNormal, const Vector3f& terrainNormal)
    {
        if (hitNormal.y > 0.4f && hitNormal.MagnitudeSqrd() > 1.0e-8f)
            return hitNormal;
        if (terrainNormal.MagnitudeSqrd() > 1.0e-8f)
            return terrainNormal;
        return Vector3f(0.0f, 1.0f, 0.0f);
    }

    Vector3f decalImpactAxisX(const Vector3f& hitNormal, const Vector3f& hitDirection)
    {
        Vector3f axis = hitNormal.Cross(Vector3f(0.0f, 1.0f, 0.0f));
        if (axis.MagnitudeSqrd() <= 1.0e-8f)
            axis = hitNormal.Cross(hitDirection);
        if (axis.MagnitudeSqrd() <= 1.0e-8f)
            return Vector3f(1.0f, 0.0f, 0.0f);
        axis.Normalize();
        return axis;
    }

} // namespace Dark
