#include <gtest/gtest.h>

#include "Play/QuestBanner.h"

using namespace Dark;

namespace
{

    QuestNoteEvent note(QuestNote kind, const char* text)
    {
        QuestNoteEvent event;
        event.kind = kind;
        event.text = text;
        return event;
    }

    uint32_t pack(uint32_t r, uint32_t g, uint32_t b)
    {
        return 0xFF000000u | (b << 16) | (g << 8) | r;
    }

} // namespace

TEST(QuestBanner, ColoursAndLabelsFollowTheNote)
{
    const QuestBanner started = makeQuestBanner(note(QuestNote::Started, "Find the gate"));
    EXPECT_EQ(started.label, "NEW QUEST");
    EXPECT_EQ(started.text, "Find the gate");
    EXPECT_EQ(started.color, pack(240, 205, 110));
    EXPECT_FLOAT_EQ(started.timeLeft, 3.5f);

    const QuestBanner objective = makeQuestBanner(note(QuestNote::NewObjective, "Cross"));
    EXPECT_EQ(objective.label, "NEW OBJECTIVE");
    EXPECT_EQ(objective.color, pack(235, 235, 235));

    const QuestBanner done = makeQuestBanner(note(QuestNote::ObjectiveDone, "Cross"));
    EXPECT_EQ(done.label, "OBJECTIVE COMPLETE");
    EXPECT_EQ(done.color, pack(120, 230, 140));

    const QuestBanner quest = makeQuestBanner(note(QuestNote::QuestDone, "Leave"));
    EXPECT_EQ(quest.label, "QUEST COMPLETE");
    EXPECT_EQ(quest.color, pack(240, 205, 110));
}
