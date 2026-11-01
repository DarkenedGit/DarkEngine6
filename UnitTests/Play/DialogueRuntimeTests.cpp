#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"
#include "Play/DialogueRuntime.h"

#include <gtest/gtest.h>

using namespace Dark;

namespace
{
    DialogueGraphDef gateGraph()
    {
        DialogueGraphDef graph;
        graph.id = "gate";
        graph.entry = "hello";
        graph.speaker = "Guard";
        graph.pauseWorld = true;
        graph.talkRange = 2.5f;
        graph.leash = 4.0f;
        graph.resume.push_back(DialogueLine{ "back", "As I was saying.", false, {} });
        graph.refusal.push_back(DialogueLine{ "no", "Not now.", false, {} });
        DialogueNode node;
        node.id = "hello";
        node.lines.push_back(DialogueLine{ "hi", "Hold there.", false, {} });
        DialogueChoice again;
        again.id = "again";
        again.text = "Go on";
        again.next = "hello";
        node.choices.push_back(again);
        graph.nodes.push_back(node);
        return graph;
    }

    struct Talk
    {
        World world;
        Entity player{};
        Entity npc{};
        DialogueFactsComponent* facts = nullptr;
    };

    Talk makeTalk()
    {
        Talk talk;
        talk.player = talk.world.createEntity();
        talk.npc = talk.world.createEntity();
        talk.world.emplace<TransformComponent>(talk.player);
        talk.world.emplace<TransformComponent>(talk.npc);
        talk.facts = &talk.world.emplace<DialogueFactsComponent>(talk.player);
        return talk;
    }
}

TEST(DialogueRuntime, InterruptDuringResumeKeepsTheChoiceBookmark)
{
    DialogueGraphDef graph = gateGraph();
    Talk talk = makeTalk();
    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    runtime.setFacts(talk.facts);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.requestSkip();
    runtime.tick(talk.world, 0.1f);
    EXPECT_TRUE(runtime.choiceOpen());
    runtime.interrupt(DialogueInterrupt::Menu);
    EXPECT_FALSE(runtime.active());

    DialogueMemoryComponent* memory = talk.world.get<DialogueMemoryComponent>(talk.npc);
    ASSERT_NE(memory, nullptr);
    EXPECT_TRUE(memory->bookmark.valid);
    EXPECT_EQ(memory->bookmark.pending, DialoguePending::Choice);
    EXPECT_FALSE(memory->lines.cycleHeard.empty());

    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    EXPECT_EQ(runtime.phase(), DialoguePhase::Resume);
    runtime.interrupt(DialogueInterrupt::Pause);
    EXPECT_FALSE(runtime.active());
    EXPECT_EQ(memory->bookmark.pending, DialoguePending::Choice);
    EXPECT_EQ(memory->bookmark.nodeId, "hello");
}

TEST(DialogueRuntime, SecondTalkAndAlertRefusal)
{
    DialogueGraphDef graph = gateGraph();
    Talk talk = makeTalk();
    talk.world.emplace<AiAgentComponent>(talk.npc);
    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    runtime.setFacts(talk.facts);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    EXPECT_FALSE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.interrupt(DialogueInterrupt::Cancel);

    Entity hostile = talk.world.createEntity();
    talk.world.emplace<TransformComponent>(hostile);
    talk.world.emplace<AiAgentComponent>(hostile);
    talk.world.get<AiAgentComponent>(hostile)->aware.alertness = AI::Alertness::Alert;
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, hostile, nullptr));
    EXPECT_EQ(runtime.phase(), DialoguePhase::Refusal);
    EXPECT_FALSE(runtime.locksWorld());
    DialogueMemoryComponent* memory = talk.world.get<DialogueMemoryComponent>(hostile);
    ASSERT_NE(memory, nullptr);
    EXPECT_FALSE(memory->bookmark.valid);
}

TEST(DialogueRuntime, UiBlockPausesTheLineUntilTheWindowCloses)
{
    DialogueGraphDef graph = gateGraph();
    Talk talk = makeTalk();
    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.1f);
    EXPECT_EQ(runtime.phase(), DialoguePhase::NodeLine);
    const std::string subtitle = runtime.subtitle();

    runtime.setUiBlocked(true);
    runtime.requestSkip();
    runtime.tick(talk.world, 5.0f);
    EXPECT_TRUE(runtime.active());
    EXPECT_EQ(runtime.phase(), DialoguePhase::NodeLine);
    EXPECT_EQ(runtime.subtitle(), subtitle);

    runtime.setUiBlocked(false);
    runtime.tick(talk.world, 0.0f);
    EXPECT_TRUE(runtime.choiceOpen());
}

TEST(DialogueRuntime, DeathDuringAUiBlockWaitsUntilTheWindowCloses)
{
    DialogueGraphDef graph = gateGraph();
    Talk talk = makeTalk();
    HealthComponent& health = talk.world.emplace<HealthComponent>(talk.player);
    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.1f);
    EXPECT_EQ(runtime.phase(), DialoguePhase::NodeLine);

    runtime.setUiBlocked(true);
    health.health.applyDamage(1000.0f);
    runtime.tick(talk.world, 3.0f);
    EXPECT_TRUE(runtime.active());
    EXPECT_EQ(runtime.phase(), DialoguePhase::NodeLine);

    runtime.setUiBlocked(false);
    runtime.tick(talk.world, 0.0f);
    EXPECT_FALSE(runtime.active());
}

TEST(DialogueRuntime, LineSpeakerCanBeThePlayerOrAnotherNpc)
{
    DialogueGraphDef graph = gateGraph();
    graph.speaker = "Hunter";
    graph.nodes[0].lines[0].speaker = "pLaYeR";
    Talk talk = makeTalk();
    talk.world.emplace<TagComponent>(talk.player).name = "PlayerPawn";
    talk.world.emplace<TagComponent>(talk.npc).name = "Hunter";

    Entity nearWolf = talk.world.createEntity();
    talk.world.emplace<TransformComponent>(nearWolf).position = { 3.0f, 0.0f, 0.0f };
    talk.world.emplace<TagComponent>(nearWolf).name = "Wolf";
    Entity farWolf = talk.world.createEntity();
    talk.world.emplace<TransformComponent>(farWolf).position = { 20.0f, 0.0f, 0.0f };
    talk.world.emplace<TagComponent>(farWolf).name = "Wolf";
    Entity otherHunter = talk.world.createEntity();
    talk.world.emplace<TransformComponent>(otherHunter).position = { 0.2f, 0.0f, 0.0f };
    talk.world.emplace<TagComponent>(otherHunter).name = "Hunter";

    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), talk.player.id());
    EXPECT_EQ(runtime.speakerName(), "pLaYeR");

    runtime.interrupt(DialogueInterrupt::Cancel);
    clearDialogueMemory(talk.world);
    graph.nodes[0].lines[0].speaker = "Wolf";
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), nearWolf.id());
    EXPECT_EQ(runtime.speakerName(), "Wolf");

    runtime.interrupt(DialogueInterrupt::Cancel);
    clearDialogueMemory(talk.world);
    graph.nodes[0].lines[0].speaker = "hunter";
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), talk.npc.id());
    EXPECT_EQ(runtime.speakerName(), "hunter");

    runtime.interrupt(DialogueInterrupt::Cancel);
    clearDialogueMemory(talk.world);
    graph.nodes[0].lines[0].speaker = "Ghost";
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), talk.npc.id());
    EXPECT_EQ(runtime.speakerName(), "Ghost");
}

TEST(DialogueRuntime, ResumeAndRefusalLinesUseTheirOwnSpeaker)
{
    DialogueGraphDef graph = gateGraph();
    graph.resume[0].speaker = "Wolf";
    graph.refusal[0].speaker = "Player";
    Talk talk = makeTalk();
    Entity wolf = talk.world.createEntity();
    talk.world.emplace<TransformComponent>(wolf).position = { 2.0f, 0.0f, 0.0f };
    talk.world.emplace<TagComponent>(wolf).name = "Wolf";
    AiAgentComponent& agent = talk.world.emplace<AiAgentComponent>(talk.npc);

    DialogueRuntime runtime;
    runtime.setGraph(&graph);
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    runtime.requestSkip();
    runtime.tick(talk.world, 0.1f);
    EXPECT_TRUE(runtime.choiceOpen());
    runtime.interrupt(DialogueInterrupt::Menu);

    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    EXPECT_EQ(runtime.phase(), DialoguePhase::Resume);
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), wolf.id());
    EXPECT_EQ(runtime.speakerName(), "Wolf");
    runtime.interrupt(DialogueInterrupt::Cancel);
    clearDialogueMemory(talk.world);

    agent.aware.alertness = AI::Alertness::Alert;
    ASSERT_TRUE(runtime.tryStart(talk.world, talk.player, talk.npc, nullptr));
    EXPECT_EQ(runtime.phase(), DialoguePhase::Refusal);
    agent.aware.alertness = AI::Alertness::Calm;
    runtime.tick(talk.world, 0.05f);
    EXPECT_EQ(runtime.speaker().id(), talk.player.id());
    EXPECT_EQ(runtime.speakerName(), "Player");
}
