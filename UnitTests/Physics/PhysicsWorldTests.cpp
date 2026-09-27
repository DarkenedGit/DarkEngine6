#include <gtest/gtest.h>

#include "Physics/PhysicsWorld.h"
#include "Math/MathHelper.h"

using namespace Dark::Math;
using namespace Dark::Physics;

namespace
{
    bool NearVec(const Vector3f& a, const Vector3f& b, float eps)
    {
        return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
    }
} // namespace

TEST(PhysicsWorld, CreateDestroy)
{
    PhysicsWorld world;
    EXPECT_FALSE(world.valid());
    EXPECT_FALSE(world.enabled());

    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(world.create(desc));
    EXPECT_TRUE(world.valid());
    EXPECT_TRUE(world.enabled());

    world.destroy();
    EXPECT_FALSE(world.valid());
    world.destroy();
    EXPECT_FALSE(world.valid());
}

TEST(PhysicsWorld, EnabledFalseIsNoOpStep)
{
    PhysicsWorld world;
    PhysicsWorldDesc desc;
    desc.enabled = false;
    ASSERT_TRUE(world.create(desc));
    EXPECT_TRUE(world.valid());
    EXPECT_FALSE(world.enabled());

    PhysicsBoxDesc ground;
    ground.position    = Vector3f(0.0f, -10.0f, 0.0f);
    ground.halfExtents = Vector3f(50.0f, 10.0f, 50.0f);
    const PhysicsBodyId groundId = world.createBox(ground);
    ASSERT_NE(groundId, kNullPhysicsBody);

    PhysicsBoxDesc box;
    box.position     = Vector3f(0.0f, 4.0f, 0.0f);
    box.halfExtents  = Vector3f(1.0f, 1.0f, 1.0f);
    box.dynamic      = true;
    const PhysicsBodyId boxId = world.createBox(box);
    ASSERT_NE(boxId, kNullPhysicsBody);

    for (int i = 0; i < 90; ++i)
        ASSERT_TRUE(world.step(1.0f / 60.0f));

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(world.getBodyPose(boxId, pos, rot));
    EXPECT_TRUE(NearVec(pos, Vector3f(0.0f, 4.0f, 0.0f), 1.0e-4f));
}

TEST(PhysicsWorld, HelloWorldStaticAndDynamicRest)
{
    PhysicsWorld world;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(world.create(desc));

    PhysicsBoxDesc ground;
    ground.position    = Vector3f(0.0f, -10.0f, 0.0f);
    ground.halfExtents = Vector3f(50.0f, 10.0f, 50.0f);
    ASSERT_NE(world.createBox(ground), kNullPhysicsBody);

    PhysicsBoxDesc box;
    box.position    = Vector3f(0.0f, 4.0f, 0.0f);
    box.halfExtents = Vector3f(1.0f, 1.0f, 1.0f);
    box.dynamic     = true;
    box.density     = 1.0f;
    box.friction    = 0.3f;
    const PhysicsBodyId boxId = world.createBox(box);
    ASSERT_NE(boxId, kNullPhysicsBody);
    EXPECT_TRUE(world.bodyValid(boxId));

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(world.getBodyPose(boxId, pos, rot));
    EXPECT_NEAR(pos.y, 4.0f, 1.0e-4f);

    for (int i = 0; i < 90; ++i)
        ASSERT_TRUE(world.step(1.0f / 60.0f));

    ASSERT_TRUE(world.getBodyPose(boxId, pos, rot));
    EXPECT_NEAR(pos.x, 0.0f, 0.05f);
    EXPECT_NEAR(pos.y, 1.0f, 0.05f);
    EXPECT_NEAR(pos.z, 0.0f, 0.05f);
}
