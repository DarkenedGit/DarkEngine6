#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Physics/CollisionCook.h"
#include "Physics/CollisionShape.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsWorld.h"

using Dark::Entity;
using Dark::PhysicsBodyComponent;
using Dark::PhysicsBodyMode;
using Dark::TransformComponent;
using Dark::World;
using Dark::Math::NearEqual;
using Dark::Math::Quaternion;
using Dark::Math::Vector3f;
using Dark::Physics::CollisionCookDesc;
using Dark::Physics::CollisionCookKind;
using Dark::Physics::CollisionShape;
using Dark::Physics::PhysicsBodyDesc;
using Dark::Physics::PhysicsWorld;
using Dark::Physics::PhysicsWorldDesc;
using Dark::Physics::cookCollisionShape;
using Dark::Physics::kNullPhysicsBody;

namespace
{
    bool cookCube(CollisionShape& shape, const Vector3f& scale)
    {
        CollisionCookDesc desc;
        desc.kind  = CollisionCookKind::Cube;
        desc.scale = scale;
        return cookCollisionShape(desc, shape);
    }

    Entity spawnWithTransform(World& ecs, const Vector3f& pos, const Vector3f& scale = Vector3f(1.0f, 1.0f, 1.0f))
    {
        Entity e = ecs.createEntity();
        TransformComponent xf;
        xf.position = pos;
        xf.scale    = scale;
        ecs.emplace<TransformComponent>(e, xf);
        return e;
    }
} // namespace

TEST(PhysicsBind, CreateBodyWritesComponent)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = spawnWithTransform(ecs, Vector3f(3.0f, 4.0f, 5.0f));

    PhysicsBodyDesc bdesc;
    bdesc.mode  = PhysicsBodyMode::Static;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    EXPECT_TRUE(phys.isBound(e));
    EXPECT_EQ(phys.boundCount(), 1u);

    const PhysicsBodyComponent* c = ecs.get<PhysicsBodyComponent>(e);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->valid);
    EXPECT_EQ(c->mode, PhysicsBodyMode::Static);
    EXPECT_NE(c->body, kNullPhysicsBody);
    EXPECT_TRUE(phys.bodyValid(c->body));
    EXPECT_EQ(phys.bodyOf(e), c->body);

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(phys.getBodyPose(c->body, pos, rot));
    EXPECT_TRUE(NearEqual(pos.x, 3.0f) && NearEqual(pos.y, 4.0f) && NearEqual(pos.z, 5.0f));
}

TEST(PhysicsBind, MissingTransformFails)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = ecs.createEntity();
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    EXPECT_FALSE(phys.createBody(ecs, e, bdesc));
    EXPECT_FALSE(phys.isBound(e));
    EXPECT_FALSE(ecs.has<PhysicsBodyComponent>(e));
}

TEST(PhysicsBind, DestroyEntityUnbindsOnStep)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = spawnWithTransform(ecs, Vector3f(0.0f, 1.0f, 0.0f));
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    const auto body = phys.bodyOf(e);
    ASSERT_TRUE(phys.bodyValid(body));

    ecs.destroyEntity(e);
    EXPECT_FALSE(ecs.alive(e));
    EXPECT_TRUE(phys.isBound(e));
    EXPECT_TRUE(phys.bodyValid(body));

    ASSERT_TRUE(phys.step(1.0f / 60.0f, ecs));
    EXPECT_FALSE(phys.isBound(e));
    EXPECT_FALSE(phys.bodyValid(body));
    EXPECT_EQ(phys.boundCount(), 0u);
}

TEST(PhysicsBind, DestroyBodyClearsComponent)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = spawnWithTransform(ecs, Vector3f(0.0f, 1.0f, 0.0f));
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    const auto body = phys.bodyOf(e);

    phys.destroyBody(ecs, e);
    EXPECT_FALSE(phys.isBound(e));
    EXPECT_FALSE(phys.bodyValid(body));
    ASSERT_NE(ecs.get<PhysicsBodyComponent>(e), nullptr);
    EXPECT_FALSE(ecs.get<PhysicsBodyComponent>(e)->valid);
    EXPECT_EQ(ecs.get<PhysicsBodyComponent>(e)->body, kNullPhysicsBody);
}

TEST(PhysicsBind, ScanRunsWhenDisabled)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = false;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = spawnWithTransform(ecs, Vector3f(0.0f, 1.0f, 0.0f));
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    const auto body = phys.bodyOf(e);

    ecs.destroyEntity(e);
    ASSERT_TRUE(phys.step(1.0f / 60.0f, ecs));
    EXPECT_FALSE(phys.isBound(e));
    EXPECT_FALSE(phys.bodyValid(body));
}

TEST(PhysicsBind, RebindReplacesBody)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity e = spawnWithTransform(ecs, Vector3f(0.0f, 1.0f, 0.0f));
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    const auto first = phys.bodyOf(e);

    ecs.get<TransformComponent>(e)->position = Vector3f(2.0f, 3.0f, 4.0f);
    ASSERT_TRUE(phys.createBody(ecs, e, bdesc));
    const auto second = phys.bodyOf(e);
    EXPECT_NE(second, first);
    EXPECT_FALSE(phys.bodyValid(first));
    EXPECT_TRUE(phys.bodyValid(second));
    EXPECT_EQ(phys.boundCount(), 1u);

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(phys.getBodyPose(second, pos, rot));
    EXPECT_TRUE(NearEqual(pos.x, 2.0f) && NearEqual(pos.y, 3.0f) && NearEqual(pos.z, 4.0f));
}

TEST(PhysicsBind, SwapBackLeavesOtherBound)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape shape;
    ASSERT_TRUE(cookCube(shape, Vector3f(1.0f, 1.0f, 1.0f)));

    World ecs;
    Entity a = spawnWithTransform(ecs, Vector3f(0.0f, 1.0f, 0.0f));
    Entity b = spawnWithTransform(ecs, Vector3f(2.0f, 1.0f, 0.0f));
    PhysicsBodyDesc bdesc;
    bdesc.shape = &shape;
    ASSERT_TRUE(phys.createBody(ecs, a, bdesc));
    ASSERT_TRUE(phys.createBody(ecs, b, bdesc));
    const auto bodyB = phys.bodyOf(b);

    ecs.destroyEntity(a);
    ASSERT_TRUE(phys.step(1.0f / 60.0f, ecs));

    EXPECT_FALSE(phys.isBound(a));
    EXPECT_TRUE(phys.isBound(b));
    EXPECT_TRUE(phys.bodyValid(bodyB));
    const PhysicsBodyComponent* cb = ecs.get<PhysicsBodyComponent>(b);
    ASSERT_NE(cb, nullptr);
    EXPECT_TRUE(cb->valid);
    EXPECT_EQ(cb->body, bodyB);
    EXPECT_EQ(phys.boundCount(), 1u);
}

TEST(PhysicsBind, DynamicRestsOnStatic)
{
    PhysicsWorld phys;
    PhysicsWorldDesc wdesc;
    wdesc.enabled = true;
    ASSERT_TRUE(phys.create(wdesc));

    CollisionShape groundShape;
    CollisionShape boxShape;
    ASSERT_TRUE(cookCube(groundShape, Vector3f(100.0f, 20.0f, 100.0f)));
    ASSERT_TRUE(cookCube(boxShape, Vector3f(2.0f, 2.0f, 2.0f)));

    World ecs;
    Entity ground = spawnWithTransform(ecs, Vector3f(0.0f, -10.0f, 0.0f));
    Entity box    = spawnWithTransform(ecs, Vector3f(0.0f, 4.0f, 0.0f));

    PhysicsBodyDesc groundDesc;
    groundDesc.mode  = PhysicsBodyMode::Static;
    groundDesc.shape = &groundShape;
    ASSERT_TRUE(phys.createBody(ecs, ground, groundDesc));

    PhysicsBodyDesc boxDesc;
    boxDesc.mode     = PhysicsBodyMode::Dynamic;
    boxDesc.shape    = &boxShape;
    boxDesc.density  = 1.0f;
    boxDesc.friction = 0.3f;
    ASSERT_TRUE(phys.createBody(ecs, box, boxDesc));

    for (int i = 0; i < 90; ++i)
        ASSERT_TRUE(phys.step(1.0f / 60.0f, ecs));

    Vector3f pos{};
    Quaternion rot{};
    ASSERT_TRUE(phys.getBodyPose(phys.bodyOf(box), pos, rot));
    EXPECT_NEAR(pos.x, 0.0f, 0.05f);
    EXPECT_NEAR(pos.y, 1.0f, 0.05f);
    EXPECT_NEAR(pos.z, 0.0f, 0.05f);
    EXPECT_TRUE(phys.isBound(ground));
    EXPECT_TRUE(phys.isBound(box));
}
