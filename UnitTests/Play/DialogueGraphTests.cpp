#include "Assets/AssetManager.h"
#include "Play/DialogueGraphJson.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
    EXPECT_EQ(graph.nodes[0].choices[0].conditions[0].key, "met");
    EXPECT_EQ(graph.nodes[0].choices[0].effects[0].value, 1);

    std::string text;
    ASSERT_TRUE(writeDialogueGraphJson(graph, text));
    DialogueGraphDef again;
    ASSERT_TRUE(parseDialogueGraph(text, again));
    EXPECT_EQ(again.nodes[0].choices[0].id, "ask");
    EXPECT_FLOAT_EQ(again.nodes[0].x, 12.0f);
    EXPECT_EQ(again.resume[0].text, "As I was saying.");
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
