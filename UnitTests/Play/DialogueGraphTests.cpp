#include "Assets/AssetManager.h"
#include "Play/DialogueGraphJson.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using namespace Dark;

namespace
{
    const char* kGraph = R"({
        "version": 1,
        "id": "gate",
        "pauseWorld": false,
        "talkRange": 2.5,
        "leash": 4,
        "speaker": "Guard",
        "entry": "hello",
        "refusal": [ { "id": "no", "text": "Not now.", "once": false } ],
        "resume": [ { "id": "back", "text": "As I was saying.", "once": false } ],
        "nodes": [
            {
                "id": "hello",
                "x": 12,
                "y": 24,
                "lines": [ { "id": "hi", "text": "Hold there.", "once": false } ],
                "choices": [
                    {
                        "id": "ask",
                        "text": "What is this gate?",
                        "next": "hello",
                        "once": true,
                        "conditions": [ { "op": "fact", "key": "met", "cmp": "==", "value": 0 } ],
                        "effects": [ { "op": "setFact", "key": "met", "value": 1 } ]
                    }
                ]
            }
        ]
    })";
}

TEST(DialogueGraph, ParsesAndRoundTrips)
{
    DialogueGraphDef graph;
    ASSERT_TRUE(parseDialogueGraph(kGraph, graph));
    EXPECT_EQ(graph.id, "gate");
    EXPECT_FALSE(graph.pauseWorld);
    EXPECT_EQ(graph.entry, "hello");
    ASSERT_EQ(graph.nodes.size(), 1u);
    EXPECT_FLOAT_EQ(graph.nodes[0].x, 12.0f);
    EXPECT_FLOAT_EQ(graph.nodes[0].y, 24.0f);
    ASSERT_EQ(graph.nodes[0].choices.size(), 1u);
    EXPECT_EQ(graph.nodes[0].choices[0].conditions[0].op, DialogueConditionOp::Fact);
    EXPECT_EQ(graph.nodes[0].choices[0].conditions[0].cmp, DialogueCmp::Equal);
    EXPECT_EQ(graph.nodes[0].choices[0].conditions[0].key, "met");
    EXPECT_EQ(graph.nodes[0].choices[0].effects[0].op, DialogueEffectOp::SetFact);
    EXPECT_EQ(graph.nodes[0].choices[0].effects[0].value, 1);

    std::string text;
    ASSERT_TRUE(writeDialogueGraphJson(graph, text));
    DialogueGraphDef again;
    ASSERT_TRUE(parseDialogueGraph(text, again));
    EXPECT_EQ(again.nodes[0].choices[0].id, "ask");
    EXPECT_FLOAT_EQ(again.nodes[0].x, 12.0f);
    EXPECT_EQ(again.resume[0].text, "As I was saying.");
    EXPECT_EQ(again.nodes[0].choices[0].conditions[0].op, DialogueConditionOp::Fact);
    EXPECT_EQ(again.nodes[0].choices[0].conditions[0].cmp, DialogueCmp::Equal);
}

namespace
{
    bool loadChoice(const char* conditions, const char* effects, DialogueGraphDef* loaded = nullptr)
    {
        const std::string text = std::string(R"({"version":1,"id":"g","entry":"n","nodes":[{"id":"n","lines":[{"id":"l","text":"Hi."}],"choices":[{"id":"c","text":"Go","conditions":)")
                                 + conditions + ",\"effects\":" + effects + "}]}]}";
        DialogueGraphDef graph;
        graph.id = "keep";
        std::string error;
        const bool ok = parseDialogueGraph(text, graph, &error);
        if (!ok)
        {
            EXPECT_EQ(graph.id, "keep");
            EXPECT_FALSE(error.empty());
        }
        if (ok && loaded)
            *loaded = std::move(graph);
        return ok;
    }
}

TEST(DialogueGraph, UnknownOpOrCmpLeavesTheGraphUnchanged)
{
    EXPECT_FALSE(loadChoice(R"([{"op":"dice"}])", "[]"));
    EXPECT_FALSE(loadChoice(R"([{"op":"fact","key":"met","cmp":"eq","value":1}])", "[]"));
    EXPECT_FALSE(loadChoice(R"([{"op":"fact","key":"met","value":1}])", "[]"));
    EXPECT_FALSE(loadChoice(R"([{"op":"questActive","key":"gate","cmp":"=="}])", "[]"));
    EXPECT_FALSE(loadChoice(R"([{"op":"alertness","name":"Calm","cmp":">"}])", "[]"));
    EXPECT_FALSE(loadChoice(R"([{"op":"health","who":"npc"}])", "[]"));
    EXPECT_FALSE(loadChoice("[]", R"([{"op":"grant"}])"));

    DialogueGraphDef health;
    ASSERT_TRUE(loadChoice(R"([{"op":"health","who":"player","alive":true}])", "[]", &health));
    EXPECT_EQ(health.nodes[0].choices[0].conditions[0].op, DialogueConditionOp::Health);
    EXPECT_EQ(health.nodes[0].choices[0].conditions[0].cmp, DialogueCmp::None);
    EXPECT_TRUE(health.nodes[0].choices[0].conditions[0].hasAlive);
}

TEST(DialogueGraph, ItemIdLandsInKeyAndLineSpeakerRoundTrips)
{
    const char* text = R"({
        "version": 1,
        "id": "items",
        "entry": "hello",
        "nodes": [ {
            "id": "hello",
            "lines": [ { "id": "hi", "text": "Here.", "speaker": "Player" } ],
            "choices": [ {
                "id": "give",
                "text": "Take it",
                "conditions": [ { "op": "item", "id": "research_file", "cmp": ">=", "value": 1 } ],
                "effects": [
                    { "op": "takeItem", "id": "research_file", "count": 1 },
                    { "op": "giveItem", "name": "medkit", "count": 1 },
                    { "op": "questEvent", "name": "gate_file", "count": 1 }
                ]
            } ]
        } ]
    })";
    DialogueGraphDef graph;
    ASSERT_TRUE(parseDialogueGraph(text, graph));
    const DialogueChoice& choice = graph.nodes[0].choices[0];
    EXPECT_EQ(choice.conditions[0].op, DialogueConditionOp::Item);
    EXPECT_EQ(choice.conditions[0].key, "research_file");
    EXPECT_EQ(choice.conditions[0].cmp, DialogueCmp::GreaterEqual);
    EXPECT_TRUE(choice.conditions[0].name.empty());
    EXPECT_EQ(choice.effects[0].op, DialogueEffectOp::TakeItem);
    EXPECT_EQ(choice.effects[0].key, "research_file");
    EXPECT_TRUE(choice.effects[0].name.empty());
    EXPECT_EQ(choice.effects[1].op, DialogueEffectOp::GiveItem);
    EXPECT_EQ(choice.effects[1].key, "medkit");
    EXPECT_TRUE(choice.effects[1].name.empty());
    EXPECT_EQ(choice.effects[2].op, DialogueEffectOp::QuestEvent);
    EXPECT_EQ(choice.effects[2].name, "gate_file");
    EXPECT_TRUE(choice.effects[2].key.empty());
    EXPECT_EQ(graph.nodes[0].lines[0].speaker, "Player");

    std::string written;
    ASSERT_TRUE(writeDialogueGraphJson(graph, written));
    EXPECT_EQ(written.find("\"id\": \"research_file\""), std::string::npos);
    EXPECT_NE(written.find("\"key\": \"research_file\""), std::string::npos);
    EXPECT_NE(written.find("\"speaker\": \"Player\""), std::string::npos);

    DialogueGraphDef again;
    ASSERT_TRUE(parseDialogueGraph(written, again));
    EXPECT_EQ(again.nodes[0].choices[0].effects[0].key, "research_file");
    EXPECT_EQ(again.nodes[0].choices[0].effects[1].key, "medkit");
    EXPECT_EQ(again.nodes[0].choices[0].effects[2].name, "gate_file");
    EXPECT_EQ(again.nodes[0].lines[0].speaker, "Player");
}

TEST(DialogueGraph, WrongVersionLeavesTheGraphUnchanged)
{
    DialogueGraphDef graph;
    graph.id = "keep";
    std::string error;
    EXPECT_FALSE(parseDialogueGraph(R"({ "version": 2, "id": "nope" })", graph, &error));
    EXPECT_EQ(graph.id, "keep");
    EXPECT_FALSE(error.empty());
}

TEST(DialogueGraph, MissingPathIsAnEmptyRef)
{
    AssetManager assets;
    EXPECT_FALSE(assets.tryLoadDialogueGraph("dialogue/missing.dlg.json"));
}

TEST(DialogueGraph, BadFileIsAnEmptyRef)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "dark-dialogue-bad.dlg.json";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << "{ \"version\": 1, \"id\": \"bad\" }";
    }
    AssetManager assets;
    EXPECT_FALSE(assets.loadDialogueGraphFile(path));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
