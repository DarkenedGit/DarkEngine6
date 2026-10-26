#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Gameplay/Quest.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Dark;

namespace
{
    constexpr const char* kQuest = R"({
        "id": "test",
        "title": "Test Quest",
        "objectives": [
            { "id": "gate",  "text": "Gate",  "type": "reach",   "zone": "gate" },
            { "id": "files", "text": "Files", "type": "collect", "item": "research_file", "count": 3 },
            { "id": "power", "text": "Power", "type": "event",   "event": "power_off", "optional": true },
            { "id": "exit",  "text": "Exit",  "type": "reach",   "zone": "exit" }
        ],
        "rewards": [ { "item": "research", "count": 2 } ]
    })";

    QuestTracker started()
    {
        QuestDef def;
        EXPECT_TRUE(parseQuest(kQuest, def));
        QuestTracker t;
        t.start(def);
        return t;
    }
} // namespace

TEST(Quest, ParsesObjectivesAndRewards)
{
    QuestDef def;
    ASSERT_TRUE(parseQuest(kQuest, def));
    EXPECT_EQ(def.id, "test");
    ASSERT_EQ(def.objectives.size(), 4u);
    EXPECT_EQ(def.objectives[1].type, ObjectiveType::Collect);
    EXPECT_EQ(def.objectives[1].target, "research_file");
    EXPECT_EQ(def.objectives[1].count, 3);
    EXPECT_TRUE(def.objectives[2].optional);
    ASSERT_EQ(def.rewards.size(), 1u);
    EXPECT_EQ(def.rewards[0].count, 2);
}

TEST(Quest, RejectsBadJson)
{
    QuestDef    def;
    std::string error;
    EXPECT_FALSE(parseQuest("{ not json", def, &error));
    EXPECT_FALSE(parseQuest(R"({ "id": "x", "objectives": [] })", def, &error));
    EXPECT_FALSE(parseQuest(R"({ "id": "x", "objectives": [ { "id": "a", "type": "fly", "zone": "z" } ] })", def, &error));
    EXPECT_FALSE(error.empty());
}

namespace
{
    std::vector<std::string>* g_questLogs = nullptr;

    void captureQuestLog(LogLevel, LogCategory, const char* message)
    {
        if (g_questLogs != nullptr && message != nullptr)
            g_questLogs->push_back(message);
    }
} // namespace

TEST(Quest, WrongFieldTypeLogsAndLeavesDefUnchanged)
{
    QuestDef    def;
    def.id = "keep";
    std::string error;
    std::vector<std::string> logs;
    g_questLogs = &logs;
    Log::setCapture(&captureQuestLog);
    struct ClearCapture
    {
        ~ClearCapture()
        {
            Log::setCapture(nullptr);
            g_questLogs = nullptr;
        }
    } clearCapture;

    EXPECT_FALSE(parseQuest(R"({ "id": "x", "sequential": "yes", "objectives": [ { "id": "a", "type": "reach", "zone": "z" } ] })", def, &error));
    EXPECT_NE(error.find("sequential"), std::string::npos);
    EXPECT_NE(error.find("bool"), std::string::npos);
    EXPECT_EQ(def.id, "keep");
    ASSERT_FALSE(logs.empty());
    EXPECT_NE(logs.back().find("sequential"), std::string::npos);

    logs.clear();
    EXPECT_FALSE(parseQuest(R"({ "id": 4, "objectives": [ { "id": "a", "type": "reach", "zone": "z" } ] })", def, &error));
    EXPECT_NE(error.find("\"id\""), std::string::npos);
    EXPECT_EQ(def.id, "keep");

    EXPECT_FALSE(parseQuest(R"({ "id": "x", "objectives": [ { "id": "a", "type": "collect", "item": "rock", "count": "3" } ] })", def, &error));
    EXPECT_NE(error.find("count"), std::string::npos);
    EXPECT_EQ(def.id, "keep");

    EXPECT_FALSE(parseQuest(R"({ "id": "x", "objectives": [ { "id": "a", "type": 1, "zone": "z" } ] })", def, &error));
    EXPECT_NE(error.find("type"), std::string::npos);
}

TEST(Quest, ObjectivesOpenInOrderAndOptionalDoesNotBlock)
{
    QuestTracker t = started();
    EXPECT_TRUE(t.open(0));
    EXPECT_FALSE(t.open(1));
    EXPECT_TRUE(t.open(2));
    t.reach("exit");
    EXPECT_FALSE(t.state(3).done);
    t.reach("gate");
    EXPECT_TRUE(t.state(0).done);
    EXPECT_TRUE(t.open(1));
    t.collect("research_file", 2);
    EXPECT_EQ(t.state(1).progress, 2);
    EXPECT_FALSE(t.open(3));
    t.collect("research_file", 1);
    EXPECT_TRUE(t.state(1).done);
    t.reach("exit");
    EXPECT_TRUE(t.complete());
    EXPECT_FALSE(t.state(2).done);
}

TEST(Quest, ItemsPickedUpEarlyStillCount)
{
    QuestTracker t = started();
    t.collect("research_file", 3);
    EXPECT_FALSE(t.state(1).done);
    t.reach("gate");
    EXPECT_TRUE(t.state(1).done);
    EXPECT_EQ(t.focus(), 3);
}

TEST(Quest, NotesReportProgress)
{
    QuestTracker t     = started();
    auto         notes = t.takeNotes();
    ASSERT_GE(notes.size(), 2u);
    EXPECT_EQ(notes[0].kind, QuestNote::Started);
    t.reach("gate");
    notes = t.takeNotes();
    bool done = false, next = false;
    for (const QuestNoteEvent& n : notes)
    {
        done = done || (n.kind == QuestNote::ObjectiveDone && n.text == "Gate");
        next = next || (n.kind == QuestNote::NewObjective && n.text == "Files");
    }
    EXPECT_TRUE(done);
    EXPECT_TRUE(next);
}

TEST(Quest, SyncCollectTracksHeldStack)
{
    QuestTracker t = started();
    t.syncCollect("research_file", 3);
    EXPECT_EQ(t.state(1).progress, 0);
    EXPECT_FALSE(t.state(1).done);

    t.reach("gate");
    t.syncCollect("research_file", 1);
    EXPECT_EQ(t.state(1).progress, 1);
    EXPECT_FALSE(t.state(1).done);
    t.syncCollect("research_file", 1);
    EXPECT_EQ(t.state(1).progress, 1);

    t.syncCollect("research_file", 0);
    EXPECT_EQ(t.state(1).progress, 0);
    EXPECT_FALSE(t.state(1).done);

    t.syncCollect("research_file", 5);
    EXPECT_EQ(t.state(1).progress, 3);
    EXPECT_TRUE(t.state(1).done);
    t.syncCollect("research_file", 0);
    EXPECT_EQ(t.state(1).progress, 3);
    EXPECT_TRUE(t.state(1).done);

    t.reach("exit");
    EXPECT_TRUE(t.complete());
    t.syncCollect("research_file", 0);
    EXPECT_EQ(t.state(1).progress, 3);
}

TEST(Quest, SyncCollectRewardStackDoesNotFarm)
{
    QuestDef def;
    ASSERT_TRUE(parseQuest(kQuest, def));
    def.rewards[0].item  = "research_file";
    def.rewards[0].count = 2;
    QuestTracker t;
    t.start(std::move(def));
    t.reach("gate");
    t.syncCollect("research_file", 1);
    EXPECT_EQ(t.state(1).progress, 1);
    t.syncCollect("research_file", 3);
    EXPECT_EQ(t.state(1).progress, 3);
    EXPECT_TRUE(t.state(1).done);
    t.syncCollect("research_file", 3);
    EXPECT_EQ(t.state(1).progress, 3);
}

TEST(Quest, ShippedNorthLabQuestLoads)
{
    bool loaded = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        QuestDef    def;
        std::string error;
        if (!loadQuestFile(root / questVirtualPath("north_lab"), def, &error))
            continue;
        loaded = true;
        EXPECT_EQ(def.id, "north_lab");
        EXPECT_GE(def.objectives.size(), 3u);
        break;
    }
    EXPECT_TRUE(loaded);
}
