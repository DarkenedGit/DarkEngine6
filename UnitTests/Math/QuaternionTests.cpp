#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

using namespace Dark::Math;

namespace
{
    bool NearVec(const Vector3f& a, const Vector3f& b, float eps = 1.0e-4f)
    {
        return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
    }
} // namespace

TEST(Quaternion, FromLookRotationIdentityMapsZToZ)
{
    const Quaternion q = Quaternion::FromLookRotation(Vector3f::Z_AXIS, Vector3f::Y_AXIS);
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::Z_AXIS), Vector3f(Vector3f::Z_AXIS)));
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::X_AXIS), Vector3f(Vector3f::X_AXIS)));
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::Y_AXIS), Vector3f(Vector3f::Y_AXIS)));
}

TEST(Quaternion, FromLookRotationXMapsZToX)
{
    const Quaternion q = Quaternion::FromLookRotation(Vector3f::X_AXIS, Vector3f::Y_AXIS);
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::Z_AXIS), Vector3f(Vector3f::X_AXIS)));
}

TEST(Quaternion, FromLookRotationParallelUpFallsBack)
{
    const Quaternion q = Quaternion::FromLookRotation(Vector3f::Y_AXIS, Vector3f::Y_AXIS);
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::Z_AXIS), Vector3f(Vector3f::Y_AXIS)));
    EXPECT_NEAR(q.Length(), 1.0f, 1.0e-4f);
}

TEST(Quaternion, FromLookRotationZeroForwardFallsBackToZ)
{
    const Quaternion q = Quaternion::FromLookRotation(Vector3f(0.0f, 0.0f, 0.0f), Vector3f::Y_AXIS);
    EXPECT_TRUE(NearVec(q.Rotate(Vector3f::Z_AXIS), Vector3f(Vector3f::Z_AXIS)));
}
