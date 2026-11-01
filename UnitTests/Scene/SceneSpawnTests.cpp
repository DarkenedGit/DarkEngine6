#include <gtest/gtest.h>

#include "AI/AiComponents.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/PowerComponents.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueMemory.h"
#include "Scene/SceneSpawn.h"
#include "Scene/SceneTypes.h"

using namespace Dark;

namespace
{

    float height3(void*, float, float)
    {
        return 3.0f;
    }

    Entity makeActor(World& world, const Math::Vector3f& pos)
    {
        const Entity e = world.createEntity();
        world.emplace<TransformComponent>(e, pos, Math::Quaternion::IDENTITY, Math::Vector3f{ 1.0f, 1.0f, 1.0f });
        world.emplace<AiAgentComponent>(e);
        return e;
    }

} // namespace

TEST(SceneSpawn, ApplyActorSharesCircuitDialoguePatrolAndSquad)
{
    World world;
    const Entity light = world.createEntity();
    SceneObjectData lamp{};
    lamp.type    = SceneObjectType::PointLight;
    lamp.circuit = "yard";
    applySceneActor(world, light, lamp, nullptr, nullptr);
    const CircuitMemberComponent* member = world.get<CircuitMemberComponent>(light);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->circuit, "yard");

    const Entity cube = makeActor(world, Math::Vector3f{ 0.0f, 9.0f, 0.0f });
    SceneObjectData box{};
    box.type    = SceneObjectType::Cube;
    box.circuit = "yard";
    applySceneActor(world, cube, box, &height3, nullptr);
    EXPECT_EQ(world.get<CircuitMemberComponent>(cube), nullptr);
    const TransformComponent* cubeXf = world.get<TransformComponent>(cube);
    ASSERT_NE(cubeXf, nullptr);
    EXPECT_FLOAT_EQ(cubeXf->position.y, 9.0f);

    const Entity hunter = makeActor(world, Math::Vector3f{ 4.0f, 0.0f, 6.0f });
    SceneObjectData npc{};
    npc.type     = SceneObjectType::Hunter;
    npc.dialogue = "dialogue/hunter_gate.dlg.json";
    npc.squad    = "gate";
    npc.patrol.push_back(Math::Vector2f{ 1.0f, 2.0f });
    npc.patrol.push_back(Math::Vector2f{ 8.0f, 2.0f });
    applySceneActor(world, hunter, npc, &height3, nullptr);

    const TransformComponent* hunterXf = world.get<TransformComponent>(hunter);
    ASSERT_NE(hunterXf, nullptr);
    EXPECT_FLOAT_EQ(hunterXf->position.y, 3.5f);
    const DialogueBindingComponent* talk = world.get<DialogueBindingComponent>(hunter);
    ASSERT_NE(talk, nullptr);
    EXPECT_EQ(talk->graph, "dialogue/hunter_gate.dlg.json");
    EXPECT_NE(world.get<DialogueMemoryComponent>(hunter), nullptr);
    const PatrolComponent* patrol = world.get<PatrolComponent>(hunter);
    ASSERT_NE(patrol, nullptr);
    ASSERT_EQ(patrol->points.size(), 2u);
    EXPECT_FLOAT_EQ(patrol->points[0].x, 1.0f);
    EXPECT_FLOAT_EQ(patrol->points[0].y, 3.0f);
    EXPECT_FLOAT_EQ(patrol->points[0].z, 2.0f);
    EXPECT_FLOAT_EQ(patrol->points[1].x, 8.0f);
    EXPECT_FLOAT_EQ(patrol->points[1].z, 2.0f);
    const AiAgentComponent* agent = world.get<AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    const uint32_t squad = agent->squad;
    EXPECT_NE(squad, 0u);

    const Entity same = makeActor(world, Math::Vector3f{ 0.0f, 1.0f, 0.0f });
    applySceneActor(world, same, npc, nullptr, nullptr);
    const AiAgentComponent* sameAgent = world.get<AiAgentComponent>(same);
    ASSERT_NE(sameAgent, nullptr);
    EXPECT_EQ(sameAgent->squad, squad);
    const PatrolComponent* flat = world.get<PatrolComponent>(same);
    ASSERT_NE(flat, nullptr);
    ASSERT_FALSE(flat->points.empty());
    EXPECT_FLOAT_EQ(flat->points[0].y, 0.0f);

    npc.squad.clear();
    npc.patrol.clear();
    npc.dialogue.clear();
    applySceneActor(world, hunter, npc, &height3, nullptr);
    EXPECT_EQ(world.get<AiAgentComponent>(hunter)->squad, 0u);
    EXPECT_EQ(world.get<PatrolComponent>(hunter), nullptr);
    EXPECT_EQ(world.get<DialogueBindingComponent>(hunter), nullptr);

    const Entity wolf = makeActor(world, Math::Vector3f{ 2.0f, 8.0f, 3.0f });
    SceneObjectData beast{};
    beast.type     = SceneObjectType::Wolf;
    beast.dialogue = "dialogue/hunter_gate.dlg.json";
    beast.squad    = "gate";
    beast.patrol.push_back(Math::Vector2f{ 1.0f, 1.0f });
    applySceneActor(world, wolf, beast, &height3, nullptr);
    const TransformComponent* wolfXf = world.get<TransformComponent>(wolf);
    ASSERT_NE(wolfXf, nullptr);
    EXPECT_FLOAT_EQ(wolfXf->position.y, 3.5f);
    EXPECT_EQ(world.get<DialogueBindingComponent>(wolf), nullptr);
    EXPECT_EQ(world.get<PatrolComponent>(wolf), nullptr);
    EXPECT_EQ(world.get<AiAgentComponent>(wolf)->squad, 0u);
}
