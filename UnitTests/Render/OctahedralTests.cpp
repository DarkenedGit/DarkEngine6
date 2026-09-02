#include <gtest/gtest.h>

#include "Render/Octahedral.h"

#include <cmath>

using Dark::Math::Vector3f;
using Dark::decodeOct;
using Dark::encodeOct;

namespace
{
    void expectRoundTrip(const Vector3f& n, float maxDeg)
    {
        Vector3f unit = n;
        unit.Normalize();
        const Vector3f back = decodeOct(encodeOct(unit));
        const float    dot  = unit.Dot(back);
        const float    deg  = std::acos(std::fmin(1.0f, std::fabs(dot))) * (180.0f / 3.14159265f);
        EXPECT_LT(deg, maxDeg) << "n=(" << unit.x << "," << unit.y << "," << unit.z << ")";
    }
} // namespace

TEST(Octahedral, AxisRoundTrip)
{
    expectRoundTrip(Vector3f(0.0f, 0.0f, 1.0f), 0.5f);
    expectRoundTrip(Vector3f(0.0f, 0.0f, -1.0f), 0.5f);
    expectRoundTrip(Vector3f(1.0f, 0.0f, 0.0f), 0.5f);
    expectRoundTrip(Vector3f(0.0f, 1.0f, 0.0f), 0.5f);
}

TEST(Octahedral, DiagonalRoundTrip)
{
    expectRoundTrip(Vector3f(1.0f, 1.0f, 1.0f), 1.0f);
    expectRoundTrip(Vector3f(-0.3f, 0.8f, -0.5f), 1.0f);
}

TEST(Octahedral, PlusZEncodesToCenter)
{
    const auto uv = encodeOct(Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(uv.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(uv.y, 0.5f, 1.0e-5f);
}
