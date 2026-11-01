#include "Play/DialogueMemory.h"

#include "ECS/World.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(DialogueMemory, TwoSpeakersCycleIndependently)
{
    const std::vector<DialogueLine> bank = {
        { "a", "Alpha", false, {} },
        { "b", "Beta", false, {} },
    };
    DialogueLineMemory first;
    DialogueLineMemory second;
    second.rng = 99u;

    const DialogueLine* line = pickDialogueLine(first, bank);
    ASSERT_NE(line, nullptr);
    rememberDialogueLine(first, *line);
    EXPECT_EQ(first.cycleHeard.size(), 1u);
    EXPECT_TRUE(second.cycleHeard.empty());

    std::string last;
    for (int i = 0; i < 2; ++i)
    {
        const DialogueLine* picked = pickDialogueLine(first, bank);
        ASSERT_NE(picked, nullptr);
        last = picked->id;
        rememberDialogueLine(first, *picked);
    }
    const DialogueLine* skipped = pickDialogueLine(first, bank);
    ASSERT_NE(skipped, nullptr);
    EXPECT_NE(skipped->id, last);
}

TEST(DialogueMemory, EntryRouter)
{
    EXPECT_EQ(chooseDialogueEntry(false, false, false, false, true), DialogueEntryKind::Missing);
    EXPECT_EQ(chooseDialogueEntry(true, true, false, false, true), DialogueEntryKind::Refusal);
    EXPECT_EQ(chooseDialogueEntry(true, false, true, false, true), DialogueEntryKind::Refusal);
    EXPECT_EQ(chooseDialogueEntry(true, false, false, true, true), DialogueEntryKind::Refusal);
    EXPECT_EQ(chooseDialogueEntry(true, false, false, false, true), DialogueEntryKind::Resume);
    EXPECT_EQ(chooseDialogueEntry(true, false, false, false, false), DialogueEntryKind::Greeting);
}

TEST(DialogueMemory, ClearDropsBookmarkAndOnceLines)
{
    World world;
    const Entity npc = world.createEntity();
    DialogueMemoryComponent& memory = world.emplace<DialogueMemoryComponent>(npc);
    memory.bookmark.valid   = true;
    memory.bookmark.graphId = "hunter_gate";
    memory.bookmark.nodeId  = "gate";
    memory.bookmark.pending = DialoguePending::Choice;
    memory.lines.onceHeard.push_back("why_hunters");
    memory.lines.onceHeard.push_back("offer_file");
    memory.lines.cycleHeard.push_back("gate_hold");

    clearDialogueMemory(world);

    const DialogueMemoryComponent* after = world.get<DialogueMemoryComponent>(npc);
    ASSERT_NE(after, nullptr);
    EXPECT_FALSE(after->bookmark.valid);
    EXPECT_TRUE(after->bookmark.graphId.empty());
    EXPECT_TRUE(after->bookmark.nodeId.empty());
    EXPECT_TRUE(after->lines.onceHeard.empty());
    EXPECT_TRUE(after->lines.cycleHeard.empty());
}

TEST(DialogueMemory, OnceChoiceIsRememberedApartFromRepeatableLines)
{
    DialogueLineMemory memory;
    rememberDialogueChoice(memory, "ask");
    EXPECT_TRUE(dialogueHeard(memory, "ask"));
    DialogueLine repeat{ "hi", "Hello", false, {} };
    rememberDialogueLine(memory, repeat);
    EXPECT_FALSE(dialogueHeard(memory, "hi"));
    EXPECT_EQ(memory.cycleHeard.size(), 1u);
}
