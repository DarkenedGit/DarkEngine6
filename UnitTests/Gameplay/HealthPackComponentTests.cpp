#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/HealthPack.h"
#include "Math/Vector3f.h"

using Dark::Entity;
using Dark::HealthComponent;
using Dark::HealthPackComponent;
using Dark::MeshComponent;
using Dark::PrimitiveMesh;
using Dark::TransformComponent;
using Dark::World;
using Dark::healthPackWorldMatrix;
using Dark::tickHealthPacks;
using Dark::Math::Vector3f;

namespace
{
    Entity makePlayer(World& world, float hpLost)
    {
        Entity e = world.createEntity();
        world.emplace<TransformComponent>(e);
        HealthComponent hc{};
        if (hpLost > 0.0f)
            hc.health.applyDamage(hpLost);
        world.emplace<HealthComponent>(e, std::move(hc));
        return e;
    }

    Entity makePack(World& world, const Vector3f& pos)
    {
        Entity e = world.createEntity();
        world.emplace<TransformComponent>(e);
        MeshComponent mc{};
        mc.primitive = PrimitiveMesh::Cross;
        world.emplace<MeshComponent>(e, mc);
        HealthPackComponent p{};
        p.restPos = pos;
        p.active  = true;
        world.emplace<HealthPackComponent>(e, p);
        return e;
    }
} // namespace

TEST(HealthPackComponent, PickupHealsPlayerHealthComponent)
{
    World world;
    Entity player = makePlayer(world, 80.0f);
    Entity pack   = makePack(world, Vector3f{ 0.2f, 0.0f, 0.0f });

    EXPECT_EQ(tickHealthPacks(world, player, 0.0f), 1);
    EXPECT_NEAR(world.get<HealthComponent>(player)->health.hp(), 70.0f, 1.0e-3f);
    EXPECT_FALSE(world.get<HealthPackComponent>(pack)->active);
    EXPECT_EQ(world.get<MeshComponent>(pack)->primitive, PrimitiveMesh::None);
    EXPECT_EQ(tickHealthPacks(world, player, 0.0f), 0);

    EXPECT_EQ(tickHealthPacks(world, player, HealthPackComponent::kRespawn + 0.01f), 0);
    EXPECT_TRUE(world.get<HealthPackComponent>(pack)->active);
    EXPECT_EQ(world.get<MeshComponent>(pack)->primitive, PrimitiveMesh::Cross);
}

TEST(HealthPackComponent, NoPickupAtFullHealthOrOutOfRange)
{
    World world;
    Entity full = makePlayer(world, 0.0f);
    Entity pack = makePack(world, Vector3f{ 0.0f, 0.0f, 0.0f });
    EXPECT_EQ(tickHealthPacks(world, full, 0.0f), 0);
    EXPECT_TRUE(world.get<HealthPackComponent>(pack)->active);

    Entity hurt = makePlayer(world, 40.0f);
    if (TransformComponent* xf = world.get<TransformComponent>(hurt))
        xf->position = Vector3f{ 10.0f, 0.0f, 0.0f };
    EXPECT_EQ(tickHealthPacks(world, hurt, 0.0f), 0);
    EXPECT_TRUE(world.get<HealthPackComponent>(pack)->active);
}

TEST(HealthPack, WorldMatrixTranslates)
{
    const auto m = healthPackWorldMatrix(Vector3f{ 3.0f, 4.0f, 5.0f }, 0.0f, 0.0f);
    EXPECT_NEAR(m.m_afEntry[12], 3.0f, 1.0e-4f);
    EXPECT_NEAR(m.m_afEntry[13], 4.0f, 1.0e-4f);
    EXPECT_NEAR(m.m_afEntry[14], 5.0f, 1.0e-4f);
}
