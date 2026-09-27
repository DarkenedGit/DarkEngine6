#include <gtest/gtest.h>

#include "Assets/MeshData.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Physics/PhysicsWorld.h"
#include "Math/Vector3f.h"

using Dark::LineMeshData;
using Dark::Math::Vector3f;
using Dark::Physics::PhysicsBoxDesc;
using Dark::Physics::PhysicsWorld;
using Dark::Physics::PhysicsWorldDesc;
using Dark::Physics::appendPhysicsDebugLine;
using Dark::Physics::kMaxPhysicsDebugLines;
using Dark::Physics::kNullPhysicsBody;

TEST(PhysicsDebugDraw, InvalidWorldClearsOutput)
{
    PhysicsWorld world;
    LineMeshData lines;
    lines.positions.push_back(Vector3f(1.0f, 2.0f, 3.0f));
    lines.indices.push_back(0);
    world.debugDraw(lines);
    EXPECT_TRUE(lines.positions.empty());
    EXPECT_TRUE(lines.indices.empty());
}

TEST(PhysicsDebugDraw, BoxWorldEmitsWireframeLines)
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
    ASSERT_NE(world.createBox(box), kNullPhysicsBody);

    LineMeshData lines;
    world.debugDraw(lines);
    ASSERT_EQ(lines.indices.size() % 2, 0u);
    EXPECT_GE(lines.indices.size() / 2, 12u);
    EXPECT_EQ(lines.positions.size(), lines.indices.size());
    EXPECT_LE(lines.indices.size() / 2, kMaxPhysicsDebugLines);
}

TEST(PhysicsDebugDraw, LineCap)
{
    LineMeshData out;
    const Vector3f a(0.0f, 0.0f, 0.0f);
    const Vector3f b(1.0f, 0.0f, 0.0f);
    for (uint32_t i = 0; i < kMaxPhysicsDebugLines; ++i)
        ASSERT_TRUE(appendPhysicsDebugLine(out, a, b));
    EXPECT_FALSE(appendPhysicsDebugLine(out, a, b));
    EXPECT_EQ(out.indices.size() / 2, kMaxPhysicsDebugLines);
    EXPECT_EQ(out.positions.size(), kMaxPhysicsDebugLines * 2u);
}
