#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/PowerSystem.h"

#include <gtest/gtest.h>

using namespace Dark;
using namespace Dark::Math;

TEST(PowerSystem, SetCircuitTogglesMembers)
{
    World world;
    const Entity box = world.createEntity();
    world.emplace<TransformComponent>(box, Vector3f{ 1.0f, 2.0f, 3.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    PowerBoxComponent power{};
    power.circuit = "lab";
    power.on      = true;
    world.emplace<PowerBoxComponent>(box, power);

    const Entity light = world.createEntity();
    LocalLightComponent lamp{};
    lamp.enabled = true;
    world.emplace<LocalLightComponent>(light, lamp);
    CircuitMemberComponent member{};
    member.circuit = "lab";
    world.emplace<CircuitMemberComponent>(light, member);

    const Entity other = world.createEntity();
    world.emplace<LocalLightComponent>(other, lamp);
    CircuitMemberComponent yard{};
    yard.circuit = "yard";
    world.emplace<CircuitMemberComponent>(other, yard);

    setPowerCircuit(world, box, false, false, nullptr, 0.0f, nullptr, nullptr, nullptr);

    const PowerBoxComponent* saved = world.get<PowerBoxComponent>(box);
    const LocalLightComponent* lab = world.get<LocalLightComponent>(light);
    const LocalLightComponent* kept = world.get<LocalLightComponent>(other);
    ASSERT_NE(saved, nullptr);
    ASSERT_NE(lab, nullptr);
    ASSERT_NE(kept, nullptr);
    EXPECT_FALSE(saved->on);
    EXPECT_FALSE(lab->enabled);
    EXPECT_TRUE(kept->enabled);
}
