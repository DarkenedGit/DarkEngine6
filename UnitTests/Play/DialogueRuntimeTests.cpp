#include "AI/AiComponents.h"
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
