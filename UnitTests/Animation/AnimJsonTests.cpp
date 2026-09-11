#include <gtest/gtest.h>

#include "Animation/AnimGraphJson.h"
#include "Animation/AnimationSet.h"

using namespace Dark;

namespace
{
AnimationClip dummyClip(const char* name, float duration = 1.0f)
{
	AnimationClip c;
	c.name = name;
	c.duration = duration;
	c.loopDefault = true;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.times = { 0.0f, duration };
	ch.values = { 0, 0, 0, 0, 0, 0 };
	c.channels.push_back(ch);
	return c;
}

AnimationSet makeSet()
{
	AnimationSet set;
	set.setClips({ dummyClip("Idle"), dummyClip("Walk"), dummyClip("Attack", 0.5f) });
	return set;
}

constexpr const char* kGraphJson = R"({
  "version": 1,
  "model": "models/hero.gltf",
  "defaultState": "Idle",
  "parameters": [
    { "name": "speed", "type": "float", "default": 0.0 },
    { "name": "attack", "type": "trigger" }
  ],
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true },
    { "name": "Attack", "clip": "Attack", "loop": false }
  ],
  "transitions": [
    { "from": "Idle", "to": "Walk", "blend": 0.15, "interrupt": true,
      "when": [{ "param": "speed", "gt": 0.1 }] },
    { "from": "Walk", "to": "Idle", "blend": 0.20,
      "when": [{ "param": "speed", "lt": 0.1 }] },
    { "from": "*", "to": "Attack", "blend": 0.08, "interrupt": true,
      "when": [{ "param": "attack", "eq": true }] },
    { "from": "Attack", "to": "Idle", "blend": 0.10, "onClipEnd": true }
  ],
  "notifies": [
    { "clip": "Walk", "name": "footstep", "time": 0.32 },
    { "clip": "Attack", "name": "hit", "frame": 12, "fps": 30, "int": 1 }
  ]
})";
} // namespace

TEST(AnimJson, ParseSidecar)
{
	AnimationSet set = makeSet();
	AnimGraphDef def;
	ASSERT_TRUE(parseAnimGraphJson(kGraphJson, set, def));
	EXPECT_EQ(def.modelPath, "models/hero.gltf");
	EXPECT_EQ(def.defaultState, 0u);
	EXPECT_EQ(def.states.size(), 3u);
	EXPECT_EQ(def.transitions.size(), 4u);
	EXPECT_EQ(def.params.size(), 2u);
	EXPECT_EQ(def.transitions[2].from, kAnyState);
	ASSERT_EQ(def.overlayMarkers.size(), 2u);
	EXPECT_NEAR(def.overlayMarkers[0].time, 0.32f, 1.0e-4f);
	EXPECT_NEAR(def.overlayMarkers[1].time, 0.4f, 1.0e-4f);
	EXPECT_EQ(def.overlayMarkers[1].intPayload, 1);
	EXPECT_EQ(def.overlayClipIndex[1], static_cast<uint32_t>(set.findClipIndex("Attack")));
}

TEST(AnimJson, DiscardedJsonFails)
{
	AnimationSet set = makeSet();
	AnimGraphDef def;
	EXPECT_FALSE(parseAnimGraphJson("{ not json", set, def));
	EXPECT_FALSE(parseAnimGraphJson("", set, def));
}

TEST(AnimJson, VersionMismatchFails)
{
	AnimationSet set = makeSet();
	AnimGraphDef def;
	EXPECT_FALSE(parseAnimGraphJson(R"({ "version": 2, "defaultState": "Idle", "states": [{ "name": "Idle", "clip": "Idle" }] })", set, def));
}

TEST(AnimJson, UnknownStateFails)
{
	AnimationSet set = makeSet();
	AnimGraphDef def;
	EXPECT_FALSE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Nope",
  "states": [{ "name": "Idle", "clip": "Idle" }]
})", set, def));
}

TEST(AnimJson, UnknownClipFails)
{
	AnimationSet set = makeSet();
	AnimGraphDef def;
	EXPECT_FALSE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "states": [{ "name": "Idle", "clip": "Missing" }]
})", set, def));
}

TEST(AnimJson, TooManyNotifiesFails)
{
	AnimationSet set = makeSet();
	std::string json = R"({ "version": 1, "defaultState": "Idle", "states": [{ "name": "Idle", "clip": "Idle" }], "notifies": [)";
	for (int i = 0; i < 65; ++i)
	{
		if (i)
			json += ",";
		json += R"({ "clip": "Idle", "name": "x", "time": 0.1 })";
	}
	json += "] }";
	AnimGraphDef def;
	EXPECT_FALSE(parseAnimGraphJson(json.c_str(), set, def));
}

TEST(AnimJson, PeekModelPath)
{
	std::string model;
	EXPECT_TRUE(peekAnimGraphModelPath(kGraphJson, model));
	EXPECT_EQ(model, "models/hero.gltf");
	EXPECT_FALSE(peekAnimGraphModelPath("{ }", model));
}
