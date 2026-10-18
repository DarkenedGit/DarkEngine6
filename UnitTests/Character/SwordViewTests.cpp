#include <gtest/gtest.h>

#include "Character/SwordView.h"
#include "ECS/Components.h"
#include "ECS/World.h"

using Dark::Entity;
using Dark::ModelComponent;
using Dark::SwordComponent;
using Dark::TransformComponent;
using Dark::World;
using Dark::Math::Quaternion;
using Dark::Math::Vector3f;

namespace
{
    Entity addPart(World& world, const Vector3f& scale)
    {
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, Vector3f{ 0.0f, 0.0f, 0.0f }, Quaternion::IDENTITY, scale);
        ModelComponent mc{};
        mc.visible = true;
        world.emplace<ModelComponent>(e, mc);
        return e;
    }
}

TEST(SwordView, HideKeepsScaleAndSkipsRender)
{
    World world;
    const Entity owner = world.createEntity();
    world.emplace<TransformComponent>(owner);
    const Vector3f bladeSize{ 0.035f, 0.08f, 0.9f };
    const Vector3f guardSize{ 0.24f, 0.04f, 0.04f };
    const Entity blade = addPart(world, bladeSize);
    const Entity guard = addPart(world, guardSize);

    SwordComponent sword{};
    sword.blade = blade;
    sword.guard = guard;
    sword.shown = false;
    world.emplace<SwordComponent>(owner, sword);

    Dark::syncSwordVisuals(world);

    const TransformComponent* bladeXf = world.get<TransformComponent>(blade);
    const TransformComponent* guardXf = world.get<TransformComponent>(guard);
    const ModelComponent*     bladeMc = world.get<ModelComponent>(blade);
    const ModelComponent*     guardMc = world.get<ModelComponent>(guard);
    ASSERT_NE(bladeXf, nullptr);
    ASSERT_NE(guardXf, nullptr);
    ASSERT_NE(bladeMc, nullptr);
    ASSERT_NE(guardMc, nullptr);
    EXPECT_FLOAT_EQ(bladeXf->scale.x, bladeSize.x);
    EXPECT_FLOAT_EQ(bladeXf->scale.y, bladeSize.y);
    EXPECT_FLOAT_EQ(bladeXf->scale.z, bladeSize.z);
    EXPECT_FLOAT_EQ(guardXf->scale.x, guardSize.x);
    EXPECT_FLOAT_EQ(guardXf->scale.y, guardSize.y);
    EXPECT_FLOAT_EQ(guardXf->scale.z, guardSize.z);
    EXPECT_FALSE(bladeMc->visible);
    EXPECT_FALSE(guardMc->visible);
}

TEST(SwordView, ShownWithoutAPoseStillSkipsRender)
{
    World world;
    const Entity owner = world.createEntity();
    world.emplace<TransformComponent>(owner);
    const Vector3f bladeSize{ 0.035f, 0.08f, 0.9f };
    const Entity blade = addPart(world, bladeSize);
    const Entity guard = addPart(world, Vector3f{ 0.24f, 0.04f, 0.04f });

    SwordComponent sword{};
    sword.blade = blade;
    sword.guard = guard;
    sword.shown = true;
    world.emplace<SwordComponent>(owner, sword);

    Dark::syncSwordVisuals(world);

    const TransformComponent* bladeXf = world.get<TransformComponent>(blade);
    const ModelComponent*     bladeMc = world.get<ModelComponent>(blade);
    ASSERT_NE(bladeXf, nullptr);
    ASSERT_NE(bladeMc, nullptr);
    EXPECT_FLOAT_EQ(bladeXf->scale.z, bladeSize.z);
    EXPECT_FALSE(bladeMc->visible);
    EXPECT_FALSE(world.get<ModelComponent>(guard)->visible);
}
