#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsFile.h"
#include "Physics/PhysicsWorld.h"

#include <filesystem>
#include <string>

using Dark::Entity;
using Dark::PhysicsBodyMode;
using Dark::PhysicsComponent;
using Dark::PhysicsShapeKind;
using Dark::TransformComponent;
using Dark::World;
using Dark::Math::NearEqual;
using Dark::Math::Vector3f;
using Dark::Physics::PhysicsWorld;
using Dark::Physics::PhysicsWorldDesc;
using Dark::Physics::bindPhysicsEntity;
using Dark::Physics::loadPhysicsSettingsFile;
using Dark::Physics::parsePhysicsSettings;
using Dark::Physics::physicsSidecarRelative;
using Dark::Physics::savePhysicsSettingsFile;
using Dark::Physics::writePhysicsSettings;

TEST(PhysicsSettings, SidecarRelativeToContent)
{
    const std::filesystem::path rel = physicsSidecarRelative("E:/DarkDev/DarkEngine6/content/models/human.gltf");
    EXPECT_EQ(rel.generic_string(), "models/human.physics.json");
    EXPECT_EQ(physicsSidecarRelative("human.gltf").generic_string(), "human.physics.json");
}

TEST(PhysicsSettings, ParseRoundTrip)
{
    const char* text = R"({
        "version": 1,
        "enabled": true,
        "body": "Dynamic",
        "shape": "sphere",
        "density": 2.5,
        "friction": 0.4,
        "restitution": 0.2,
        "linearDamping": 0.1,
        "angularDamping": 0.3,
        "gravityScale": 0.5,
        "sensor": false,
        "fixedRotation": true,
        "surface": "wood"
    })";
    PhysicsComponent settings;
    settings.density = 99.0f;
    ASSERT_TRUE(parsePhysicsSettings(text, settings));
    EXPECT_TRUE(settings.enabled);
    EXPECT_EQ(settings.mode, PhysicsBodyMode::Dynamic);
    EXPECT_EQ(settings.shape, PhysicsShapeKind::Sphere);
    EXPECT_TRUE(NearEqual(settings.density, 2.5f));
    EXPECT_TRUE(NearEqual(settings.friction, 0.4f));
    EXPECT_TRUE(NearEqual(settings.restitution, 0.2f));
    EXPECT_TRUE(NearEqual(settings.gravityScale, 0.5f));
    EXPECT_TRUE(settings.fixedRotation);
    EXPECT_EQ(settings.surface, "wood");

    PhysicsComponent again;
    ASSERT_TRUE(parsePhysicsSettings(writePhysicsSettings(settings), again));
    EXPECT_EQ(again.mode, settings.mode);
    EXPECT_EQ(again.shape, settings.shape);
    EXPECT_TRUE(NearEqual(again.density, settings.density));
    EXPECT_EQ(again.surface, settings.surface);
    EXPECT_EQ(again.fixedRotation, settings.fixedRotation);
}

TEST(PhysicsSettings, BadJsonLeavesComponent)
{
    PhysicsComponent settings;
    settings.density = 4.0f;
    settings.surface = "keep";
    EXPECT_FALSE(parsePhysicsSettings("{ not json", settings));
    EXPECT_TRUE(NearEqual(settings.density, 4.0f));
    EXPECT_EQ(settings.surface, "keep");
    EXPECT_FALSE(parsePhysicsSettings("{\"version\":1,\"body\":\"ragdoll\"}", settings));
    EXPECT_EQ(settings.surface, "keep");
}

TEST(PhysicsSettings, FileRoundTrip)
{
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    const std::filesystem::path path = dir / "darkengine-physics-settings.physics.json";

    PhysicsComponent settings;
    settings.enabled     = true;
    settings.mode        = PhysicsBodyMode::Kinematic;
    settings.shape       = PhysicsShapeKind::Capsule;
    settings.density     = 3.0f;
    settings.friction    = 0.2f;
    settings.surface     = "metal";
    settings.gravityScale = 0.0f;
    ASSERT_TRUE(savePhysicsSettingsFile(path, settings));

    PhysicsComponent loaded;
    ASSERT_TRUE(loadPhysicsSettingsFile(path, loaded));
    EXPECT_EQ(loaded.mode, PhysicsBodyMode::Kinematic);
    EXPECT_EQ(loaded.shape, PhysicsShapeKind::Capsule);
    EXPECT_TRUE(NearEqual(loaded.density, 3.0f));
    EXPECT_EQ(loaded.surface, "metal");
    EXPECT_TRUE(NearEqual(loaded.gravityScale, 0.0f));
    std::filesystem::remove(path, ec);
}

TEST(PhysicsSettings, DynamicBodyFalls)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    ASSERT_TRUE(phys.create(desc));

    World ecs;
    Entity e = ecs.createEntity();
    TransformComponent xf;
    xf.position = Vector3f(0.0f, 5.0f, 0.0f);
    ecs.emplace<TransformComponent>(e, xf);

    PhysicsComponent settings;
    settings.enabled = true;
    settings.mode    = PhysicsBodyMode::Dynamic;
    settings.shape   = PhysicsShapeKind::Box;
    settings.density = 1.0f;
    ecs.emplace<PhysicsComponent>(e, settings);
    ASSERT_TRUE(bindPhysicsEntity(phys, ecs, e));

    Vector3f start{};
    Dark::Math::Quaternion rot{};
    ASSERT_TRUE(phys.getBodyPose(phys.bodyOf(e), start, rot));
    for (int i = 0; i < 20; ++i)
        ASSERT_TRUE(phys.step(1.0f / 60.0f, ecs));
    phys.writeDynamicPoses(ecs);
    const TransformComponent* moved = ecs.get<TransformComponent>(e);
    ASSERT_NE(moved, nullptr);
    EXPECT_LT(moved->position.y, start.y - 0.5f);
}

TEST(PhysicsSettings, MoverStopsAtBoxAndSlidesOnGround)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    Dark::Physics::PhysicsBoxDesc ground;
    ground.position    = Vector3f(0.0f, -1.0f, 0.0f);
    ground.halfExtents = Vector3f(20.0f, 1.0f, 20.0f);
    ASSERT_NE(phys.createBox(ground), Dark::Physics::kNullPhysicsBody);

    Dark::Physics::PhysicsBoxDesc box;
    box.position    = Vector3f(3.0f, 0.5f, 0.0f);
    box.halfExtents = Vector3f(0.5f, 0.5f, 0.5f);
    ASSERT_NE(phys.createBox(box), Dark::Physics::kNullPhysicsBody);

    PhysicsWorld::MoverCast cast;
    cast.origin      = Vector3f(0.0f, 0.5f, 0.0f);
    cast.radius      = 0.45f;
    cast.bottom      = -0.45f;
    cast.top         = 1.35f;
    cast.translation = Vector3f(5.0f, 0.0f, 0.0f);
    const float blocked = phys.clipMover(cast);
    EXPECT_GT(blocked, 0.15f);
    EXPECT_LT(blocked, 0.55f);

    cast.translation = Vector3f(0.0f, 0.0f, 3.0f);
    EXPECT_GT(phys.clipMover(cast), 0.99f);
}

TEST(PhysicsSettings, MoverDoesNotBlockDynamic)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    Dark::Physics::PhysicsBoxDesc box;
    box.position    = Vector3f(1.2f, 0.5f, 0.0f);
    box.halfExtents = Vector3f(0.5f, 0.5f, 0.5f);
    box.dynamic     = true;
    ASSERT_NE(phys.createBox(box), Dark::Physics::kNullPhysicsBody);

    PhysicsWorld::MoverCast cast;
    cast.origin      = Vector3f(0.0f, 0.5f, 0.0f);
    cast.radius      = 0.45f;
    cast.bottom      = -0.45f;
    cast.top         = 1.35f;
    cast.translation = Vector3f(2.0f, 0.0f, 0.0f);
    EXPECT_GT(phys.clipMover(cast), 0.99f);
}

TEST(PhysicsSettings, KinematicSweepsDynamicBox)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    Dark::Physics::PhysicsBoxDesc floor;
    floor.position    = Vector3f(0.0f, -1.0f, 0.0f);
    floor.halfExtents = Vector3f(20.0f, 1.0f, 20.0f);
    ASSERT_NE(phys.createBox(floor), Dark::Physics::kNullPhysicsBody);

    Dark::Physics::PhysicsBoxDesc box;
    box.position    = Vector3f(1.2f, 0.5f, 0.0f);
    box.halfExtents = Vector3f(0.5f, 0.5f, 0.5f);
    box.dynamic     = true;
    box.density     = 1.0f;
    box.friction    = 0.2f;
    const auto boxId = phys.createBox(box);
    ASSERT_NE(boxId, Dark::Physics::kNullPhysicsBody);

    World ecs;
    Entity e = ecs.createEntity();
    TransformComponent xf;
    xf.position = Vector3f(0.0f, 0.5f, 0.0f);
    ecs.emplace<TransformComponent>(e, xf);
    PhysicsComponent settings;
    settings.enabled       = true;
    settings.mode          = PhysicsBodyMode::Kinematic;
    settings.shape         = PhysicsShapeKind::Capsule;
    settings.fixedRotation = true;
    ecs.emplace<PhysicsComponent>(e, settings);
    ASSERT_TRUE(bindPhysicsEntity(phys, ecs, e));
    const auto body = phys.bodyOf(e);

    const Dark::Math::Quaternion rot(1.0f, 0.0f, 0.0f, 0.0f);
    Vector3f                     start = xf.position;
    for (int i = 0; i < 20; ++i)
    {
        const Vector3f target = start + Vector3f(0.08f * static_cast<float>(i + 1), 0.0f, 0.0f);
        ASSERT_TRUE(phys.moveKinematicTo(body, target, rot, 1.0f / 60.0f));
        ASSERT_TRUE(phys.step(1.0f / 60.0f));
    }

    Vector3f boxPos;
    Dark::Math::Quaternion boxRot;
    ASSERT_TRUE(phys.getBodyPose(boxId, boxPos, boxRot));
    EXPECT_GT(boxPos.x, 1.35f);
}

TEST(PhysicsSettings, MoverIgnoresOwnBody)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    World ecs;
    Entity e = ecs.createEntity();
    TransformComponent xf;
    xf.position = Vector3f(0.0f, 0.5f, 0.0f);
    ecs.emplace<TransformComponent>(e, xf);
    PhysicsComponent settings;
    settings.enabled       = true;
    settings.mode          = PhysicsBodyMode::Kinematic;
    settings.shape         = PhysicsShapeKind::Capsule;
    settings.fixedRotation = true;
    ecs.emplace<PhysicsComponent>(e, settings);
    ASSERT_TRUE(bindPhysicsEntity(phys, ecs, e));

    PhysicsWorld::MoverCast cast;
    cast.origin      = xf.position;
    cast.radius      = 0.45f;
    cast.bottom      = -0.45f;
    cast.top         = 1.35f;
    cast.translation = Vector3f(1.0f, 0.0f, 0.0f);
    cast.ignore      = phys.bodyOf(e);
    EXPECT_GT(phys.clipMover(cast), 0.99f);
}

TEST(PhysicsSettings, DisabledBindRemovesBody)
{
    PhysicsWorld phys;
    PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    World ecs;
    Entity e = ecs.createEntity();
    TransformComponent xf;
    xf.position = Vector3f(0.0f, 2.0f, 0.0f);
    ecs.emplace<TransformComponent>(e, xf);
    PhysicsComponent settings;
    settings.enabled = true;
    settings.mode    = PhysicsBodyMode::Static;
    ecs.emplace<PhysicsComponent>(e, settings);
    ASSERT_TRUE(bindPhysicsEntity(phys, ecs, e));
    EXPECT_TRUE(phys.isBound(e));

    ecs.get<PhysicsComponent>(e)->enabled = false;
    ASSERT_TRUE(bindPhysicsEntity(phys, ecs, e));
    EXPECT_FALSE(phys.isBound(e));
}
