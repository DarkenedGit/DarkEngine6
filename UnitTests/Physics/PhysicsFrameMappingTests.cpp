#include <gtest/gtest.h>

#include "Physics/PhysicsMath.h"
#include "Physics/PhysicsWorld.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

using namespace Dark::Math;
using namespace Dark::Physics;

namespace
{
    bool NearVec(const Vector3f& a, const Vector3f& b, float eps = 1.0e-5f)
    {
        return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
    }

    bool NearQuat(const Quaternion& a, const Quaternion& b, float eps = 1.0e-5f)
    {
        const bool same = NearEqual(a.w, b.w, eps) && NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
        const bool flipped = NearEqual(a.w, -b.w, eps) && NearEqual(a.x, -b.x, eps) && NearEqual(a.y, -b.y, eps) && NearEqual(a.z, -b.z, eps);
        return same || flipped;
    }
} // namespace

TEST(PhysicsFrameMapping, RotateQuarterTurnY)
{
    const Quaternion q = Quaternion::FromAxisAngle(Vector3f(Vector3f::Y_AXIS), HalfPi);
    const Vector3f engine = q.Rotate(Vector3f(Vector3f::X_AXIS));
    EXPECT_TRUE(NearVec(engine, Vector3f(0.0f, 0.0f, -1.0f)));

    const Vector3f wrapped = rotate(q, Vector3f(Vector3f::X_AXIS));
    EXPECT_TRUE(NearVec(wrapped, Vector3f(0.0f, 0.0f, -1.0f)));
    EXPECT_TRUE(NearVec(wrapped, engine));
}

TEST(PhysicsFrameMapping, RoundTripQuat)
{
    const Quaternion q = Quaternion::FromAxisAngle(Vector3f(Vector3f::Y_AXIS), HalfPi);
    const Quaternion back = roundTripQuat(q);
    EXPECT_TRUE(NearQuat(back, q));

    const Quaternion neg(-q.w, -q.x, -q.y, -q.z);
    EXPECT_TRUE(NearQuat(roundTripQuat(neg), neg));
    EXPECT_TRUE(NearVec(rotate(q, Vector3f(Vector3f::X_AXIS)), rotate(neg, Vector3f(Vector3f::X_AXIS))));
}

TEST(PhysicsFrameMapping, ScaleBake)
{
    const Vector3f hx = bakeBoxHalfExtents(Vector3f(1.0f, 2.0f, 3.0f), Vector3f(2.0f, 0.5f, 4.0f));
    EXPECT_TRUE(NearVec(hx, Vector3f(2.0f, 1.0f, 12.0f)));

    const Vector3f mirrored = bakeBoxHalfExtents(Vector3f(1.0f, 1.0f, 1.0f), Vector3f(-2.0f, 1.0f, 1.0f));
    EXPECT_TRUE(NearVec(mirrored, Vector3f(2.0f, 1.0f, 1.0f)));

    EXPECT_NEAR(bakeSphereRadius(0.5f, Vector3f(-2.0f, 1.0f, 3.0f)), 1.5f, 1.0e-5f);
    EXPECT_NEAR(bakeCapsuleRadius(0.45f, Vector3f(2.0f, 10.0f, 3.0f)), 1.35f, 1.0e-5f);
}

TEST(PhysicsFrameMapping, GravitySign)
{
    PhysicsWorld world;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    ASSERT_TRUE(world.create(desc));

    const Vector3f g = world.gravity();
    EXPECT_NEAR(g.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(g.y, -24.0f, 1.0e-5f);
    EXPECT_NEAR(g.z, 0.0f, 1.0e-5f);
}

TEST(PhysicsFrameMapping, IdentityBodyPose)
{
    PhysicsWorld world;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(world.create(desc));

    PhysicsBoxDesc box;
    box.position    = Vector3f(3.0f, 4.0f, 5.0f);
    box.rotation    = Quaternion::IDENTITY;
    box.halfExtents = Vector3f(0.5f, 0.5f, 0.5f);
    const PhysicsBodyId id = world.createBox(box);
    ASSERT_NE(id, kNullPhysicsBody);

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(world.getBodyPose(id, pos, rot));
    EXPECT_TRUE(NearVec(pos, Vector3f(3.0f, 4.0f, 5.0f)));
    EXPECT_TRUE(NearQuat(rot, Quaternion::IDENTITY));
}
