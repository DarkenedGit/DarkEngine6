#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Render/PbrLighting.h"

#include <cmath>

using Dark::applySourceRadius;
using Dark::pbrDirectional;
using Dark::spotAngleAttenuation;
using Dark::spotCosTheta;
using Dark::windowedDistanceAttenuation;
using Dark::Math::DegToRad;
using Dark::Math::Vector3f;

namespace
{
    Vector3f normalized(float x, float y, float z)
    {
        Vector3f v(x, y, z);
        v.Normalize();
        return v;
    }
} // namespace

TEST(PbrLighting, WindowedAttenuationZeroAtRange)
{
    const float range      = 8.0f;
    const float invRange2  = 1.0f / (range * range);
    EXPECT_NEAR(windowedDistanceAttenuation(range * range, invRange2), 0.0f, 1.0e-6f);
    EXPECT_NEAR(windowedDistanceAttenuation((range + 1.0f) * (range + 1.0f), invRange2), 0.0f, 1.0e-6f);
}

TEST(PbrLighting, WindowedAttenuationFiniteAtOrigin)
{
    const float range     = 8.0f;
    const float invRange2 = 1.0f / (range * range);
    const float a0        = windowedDistanceAttenuation(0.0f, invRange2);
    EXPECT_TRUE(std::isfinite(a0));
    EXPECT_GT(a0, 0.0f);

    const float half = range * 0.5f;
    const float aMid = windowedDistanceAttenuation(half * half, invRange2);
    EXPECT_GT(aMid, 0.0f);
    EXPECT_LT(aMid, a0);
}

TEST(PbrLighting, SpotCosThetaOnAxisIsOne)
{
    const Vector3f lightPos(0.0f, 0.0f, 0.0f);
    const Vector3f worldPos(0.0f, 0.0f, 4.0f);
    const Vector3f dir(0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(spotCosTheta(lightPos, worldPos, dir), 1.0f, 1.0e-5f);
    EXPECT_NEAR(spotCosTheta(lightPos, Vector3f(0.0f, 0.0f, -4.0f), dir), -1.0f, 1.0e-5f);
}

TEST(PbrLighting, SpotAngleOnAxisIsOne)
{
    const float innerCos = std::cos(12.0f * DegToRad);
    const float outerCos = std::cos(25.0f * DegToRad);
    EXPECT_NEAR(spotAngleAttenuation(1.0f, innerCos, outerCos), 1.0f, 1.0e-5f);
    EXPECT_NEAR(spotAngleAttenuation(innerCos, innerCos, outerCos), 1.0f, 1.0e-5f);
    EXPECT_NEAR(spotAngleAttenuation(outerCos, innerCos, outerCos), 0.0f, 1.0e-5f);
    EXPECT_NEAR(spotAngleAttenuation(0.0f, innerCos, outerCos), 0.0f, 1.0e-5f);
}

TEST(PbrLighting, RoughDielectricMatchesLambertWithin15Percent)
{
    const Vector3f albedo(0.62f, 0.55f, 0.48f);
    const Vector3f lightColor(1.0f, 0.96f, 0.88f);
    const float    roughness = 1.0f;
    const float    metallic  = 0.0f;

    auto expectClose = [&](const Vector3f& n, const Vector3f& v, const Vector3f& l) {
        const float    ndotl   = Dark::pbrSaturate(n.Dot(l));
        const Vector3f lambert = (albedo * lightColor) * ndotl;
        const Vector3f pbr     = pbrDirectional(n, v, albedo, roughness, metallic, l, lightColor);
        const float    denom   = std::fmax(lambert.Magnitude(), 1.0e-4f);
        EXPECT_LT((pbr - lambert).Magnitude() / denom, 0.15f);
        EXPECT_NEAR(pbr.x, lambert.x, 0.15f * std::fmax(std::fabs(lambert.x), 1.0e-3f));
        EXPECT_NEAR(pbr.y, lambert.y, 0.15f * std::fmax(std::fabs(lambert.y), 1.0e-3f));
        EXPECT_NEAR(pbr.z, lambert.z, 0.15f * std::fmax(std::fabs(lambert.z), 1.0e-3f));
    };

    const Vector3f up(0.0f, 1.0f, 0.0f);
    expectClose(up, up, up);
    expectClose(up, normalized(0.2f, 1.0f, 0.1f), normalized(0.4f, 1.0f, -0.2f));
    expectClose(up, normalized(-0.3f, 1.0f, 0.5f), normalized(0.6f, 0.8f, 0.0f));
}

TEST(PbrLighting, SourceRadiusZeroIsIdentity)
{
    const Vector3f L(1.0f, 2.0f, 3.0f);
    const Vector3f n(0.0f, 1.0f, 0.0f);
    const Vector3f v(0.0f, 1.0f, 0.0f);
    const Vector3f out = applySourceRadius(L, n, v, 0.0f);
    EXPECT_NEAR(out.x, L.x, 1.0e-6f);
    EXPECT_NEAR(out.y, L.y, 1.0e-6f);
    EXPECT_NEAR(out.z, L.z, 1.0e-6f);
}
