#include "Play/DialogueMemory.h"

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
