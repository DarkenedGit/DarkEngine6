#include <gtest/gtest.h>

#include "AI/Brain.h"
#include "AI/HsmGraph.h"
#include "AI/HsmGraphComponent.h"
#include "AI/HsmGraphJson.h"
#include "Assets/AssetManager.h"
#include "Core/ContentRoots.h"

using namespace Dark;
using namespace Dark::AI;

TEST(HsmGraphJson, HunterRoundTrip)
{
    HsmGraphDef def = makeHunterHsmGraph();
    std::string text;
    ASSERT_TRUE(writeHsmGraphJson(def, text));
    HsmGraphDef again;
    ASSERT_TRUE(parseHsmGraphJson(text.c_str(), again));
    EXPECT_EQ(again.name, "hunter");
    EXPECT_EQ(again.root, "Root");
    EXPECT_EQ(again.states.size(), def.states.size());
    EXPECT_EQ(again.transitions.size(), def.transitions.size());
    EXPECT_EQ(again.events.size(), def.events.size());
    EXPECT_EQ(again.eventId("See"), kHunterSee);
    EXPECT_NEAR(again.paramFloat("memorySec", 0.0f), 1.5f, 1.0e-4f);
}

TEST(HsmGraphJson, HunterBuildMatchesBrain)
{
    HsmGraphDef def = makeHunterHsmGraph();
    HsmGraphInstance inst;
    HsmNamedActionMap actions;
    float memory = 0.0f;
    actions["armMemory"] = [&](HsmContext&) { memory = 1.5f; };
    ASSERT_TRUE(inst.build(def, nullptr, actions));
    ASSERT_TRUE(inst.start());
    EXPECT_STREQ(inst.leafName(), "Wander");
    EXPECT_TRUE(inst.processEventNamed("See"));
    EXPECT_STREQ(inst.leafName(), "Chase");
    EXPECT_TRUE(inst.processEventNamed("Lose"));
    EXPECT_STREQ(inst.leafName(), "Memory");
    EXPECT_NEAR(memory, 1.5f, 1.0e-4f);
}

TEST(HsmGraphJson, PlayerHierarchy)
{
    HsmGraphDef def = makePlayerHsmGraph();
    HsmGraphInstance inst;
    ASSERT_TRUE(inst.build(def));
    ASSERT_TRUE(inst.start());
    EXPECT_TRUE(inst.isIn("Alive"));
    EXPECT_STREQ(inst.leafName(), "Grounded");
    EXPECT_TRUE(inst.processEventNamed("Jump"));
    EXPECT_STREQ(inst.leafName(), "Jumping");
    syncPlayerHsm(inst, 2, true);
    EXPECT_STREQ(inst.leafName(), "Falling");
    syncPlayerHsm(inst, 0, true);
    EXPECT_STREQ(inst.leafName(), "Grounded");
    syncPlayerHsm(inst, 0, false);
    EXPECT_STREQ(inst.leafName(), "Dead");
    syncPlayerHsm(inst, 0, true);
    EXPECT_TRUE(inst.isIn("Alive"));
}

TEST(HsmGraphJson, BrainLoadsGraph)
{
    Brain b;
    ASSERT_TRUE(b.start());
    EXPECT_EQ(b.leaf(), Leaf::Wander);
    HsmGraphDef def = makeHunterHsmGraph();
    ASSERT_TRUE(b.load(def));
    ASSERT_TRUE(b.start());
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}

TEST(HsmGraphJson, ParseRejectsUnknownEvent)
{
    HsmGraphDef def;
    EXPECT_FALSE(parseHsmGraphJson(R"({
  "version": 1,
  "root": "Root",
  "states": [{ "name": "Root" }],
  "transitions": [{ "from": "Root", "to": "Root", "event": "Nope" }]
})", def));
}

TEST(HsmGraphJson, ContentHunterFileIfPresent)
{
    AssetManager assets;
    for (const auto& root : contentRootCandidates())
        assets.mountDirectory(root);
    auto graph = assets.tryLoadHsmGraph("ai/hunter.hsm.json");
    if (!graph)
        GTEST_SKIP() << "content/ai/hunter.hsm.json not mounted";
    EXPECT_EQ(graph->type, AssetType::HsmGraph);
    EXPECT_EQ(graph->root, "Root");
    Brain b;
    ASSERT_TRUE(b.load(*graph));
    ASSERT_TRUE(b.start());
    EXPECT_EQ(b.leaf(), Leaf::Wander);
    b.tick(0.016f, true, false);
    EXPECT_EQ(b.leaf(), Leaf::Chase);
}
