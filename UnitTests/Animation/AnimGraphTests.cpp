#include <gtest/gtest.h>

#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimSampler.h"
#include "Assets/AssetManager.h"
#include "Assets/GltfTestUtil.h"

using namespace Dark;
using namespace Dark::Math;

namespace
{
Skeleton oneBone()
{
	Skeleton sk;
	Joint j;
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	j.inverseBind = Matrix4f();
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };
	Vector3f T[1]{ j.restT };
	Quaternion R[1]{ j.restR };
	Vector3f S[1]{ j.restS };
	localToPalette(sk, T, R, S, sk.restPose);
	return sk;
}

AnimationClip namedClip(const char* name, float duration, bool loop)
{
	AnimationClip c;
	c.name = name;
	c.duration = duration;
	c.loopDefault = loop;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.times = { 0.0f, duration };
	ch.values = { 0, 0, 0, 0, 0, 0 };
	c.channels.push_back(ch);
	return c;
}

bool loadHeroGraph(AnimGraphDef& def, AnimationSet& set)
{
	set.setClips({ namedClip("Idle", 1.0f, true), namedClip("Walk", 1.0f, true), namedClip("Attack", 0.4f, false) });
	constexpr const char* json = R"({
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
  ]
})";
	if (!parseAnimGraphJson(json, set, def))
		return false;
	def.animSet = std::make_shared<AnimationSet>(set);
	return true;
}
} // namespace

TEST(AnimGraph, EvaluateWithoutBindIsNoOp)
{
	AnimGraphInstance g;
	g.evaluate();
	EXPECT_FALSE(g.started());
	EXPECT_FALSE(g.bind(nullptr, nullptr));
}

TEST(AnimGraph, BindWiresPlayerToAnimSet)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
}

TEST(AnimGraph, FirstEvaluatePlaysDefaultIdle)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	EXPECT_TRUE(g.started());
	EXPECT_EQ(g.currentState(), 0u);
	EXPECT_STREQ(g.player().clipName(), "Idle");
	EXPECT_NEAR(g.player().blendAlpha(), 1.0f, 1.0e-4f);
}

TEST(AnimGraph, IdleToWalkOnSpeed)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	EXPECT_TRUE(g.setFloat("speed", 1.0f));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Walk");
}

TEST(AnimGraph, AttackTriggerFromAnyRequiresReset)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	ASSERT_TRUE(g.setTrigger("attack"));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Attack");
	AnimNotifyQueue q;
	g.player().update(1.0f, q);
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
	ASSERT_TRUE(g.setTrigger("attack"));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Attack");
}

TEST(AnimGraph, OnClipEndAttackToIdle)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	g.setTrigger("attack");
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Attack");
	AnimNotifyQueue q;
	g.player().update(1.0f, q);
	EXPECT_TRUE(g.player().finished());
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
}

TEST(AnimGraph, OnClipEndIgnoredIfLoop)
{
	AnimGraphDef def;
	AnimationSet set;
	set.setClips({ namedClip("Idle", 1.0f, true) });
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "states": [{ "name": "Idle", "clip": "Idle", "loop": true }],
  "transitions": [{ "from": "Idle", "to": "Idle", "onClipEnd": true }]
})", set, def));
	def.animSet = std::make_shared<AnimationSet>(set);
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	AnimNotifyQueue q;
	g.player().update(2.0f, q);
	EXPECT_FALSE(g.player().finished());
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
}

TEST(AnimGraph, EmptyWhenAlwaysMatches)
{
	AnimGraphDef def;
	AnimationSet set;
	set.setClips({ namedClip("Idle", 1.0f, true), namedClip("Walk", 1.0f, true) });
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true }
  ],
  "transitions": [{ "from": "Idle", "to": "Walk", "blend": 0.0 }]
})", set, def));
	def.animSet = std::make_shared<AnimationSet>(set);
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Walk");
}

TEST(AnimGraph, CanInterruptOnCandidate)
{
	AnimGraphDef def;
	AnimationSet set;
	set.setClips({ namedClip("Idle", 1.0f, true), namedClip("Walk", 1.0f, true), namedClip("Attack", 0.4f, false) });
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "parameters": [{ "name": "attack", "type": "trigger" }],
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true },
    { "name": "Attack", "clip": "Attack", "loop": false }
  ],
  "transitions": [
    { "from": "Idle", "to": "Walk", "blend": 1.0, "interrupt": true },
    { "from": "Walk", "to": "Attack", "blend": 0.0, "interrupt": false,
      "when": [{ "param": "attack", "eq": true }] }
  ]
})", set, def));
	def.animSet = std::make_shared<AnimationSet>(set);
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Walk");
	EXPECT_NE(g.player().outgoingClip(), AnimPlayer::kInvalidClip);
	g.setTrigger("attack");
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Walk");
	AnimNotifyQueue q;
	g.player().update(1.1f, q);
	EXPECT_EQ(g.player().outgoingClip(), AnimPlayer::kInvalidClip);
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Attack");
}

TEST(AnimGraph, FindPathIdleWalkRun)
{
	AnimGraphDef def;
	AnimationSet set;
	set.setClips({ namedClip("Idle", 1.0f, true), namedClip("Walk", 1.0f, true), namedClip("Run", 1.0f, true) });
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true },
    { "name": "Run", "clip": "Run", "loop": true }
  ],
  "transitions": [
    { "from": "Idle", "to": "Walk", "blend": 0.15, "interrupt": true },
    { "from": "Walk", "to": "Run", "blend": 0.12, "interrupt": true },
    { "from": "Run", "to": "Walk", "blend": 0.14, "interrupt": true },
    { "from": "Walk", "to": "Idle", "blend": 0.18, "interrupt": true }
  ]
})", set, def));
	def.animSet = std::make_shared<AnimationSet>(set);
	std::vector<uint32_t> path;
	ASSERT_TRUE(findAnimStatePath(def, 0, 2, path));
	ASSERT_EQ(path.size(), 2u);
	EXPECT_EQ(def.transitions[path[0]].to, 1u);
	EXPECT_EQ(def.transitions[path[1]].to, 2u);
	path.clear();
	EXPECT_TRUE(findAnimStatePath(def, 0, 0, path));
	EXPECT_TRUE(path.empty());
}

TEST(AnimGraph, FindPathUsesAnyState)
{
	AnimGraphDef def;
	AnimationSet set;
	ASSERT_TRUE(loadHeroGraph(def, set));
	std::vector<uint32_t> path;
	ASSERT_TRUE(findAnimStatePath(def, 0, 2, path));
	ASSERT_EQ(path.size(), 1u);
	EXPECT_EQ(def.transitions[path[0]].from, kAnyState);
	EXPECT_EQ(def.transitions[path[0]].to, 2u);
}

TEST(AnimGraph, RequestStateWalksBlends)
{
	AnimGraphDef def;
	AnimationSet set;
	set.setClips({ namedClip("Idle", 1.0f, true), namedClip("Walk", 1.0f, true), namedClip("Run", 1.0f, true) });
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "parameters": [{ "name": "speed", "type": "float", "default": 0.0 }],
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true },
    { "name": "Run", "clip": "Run", "loop": true }
  ],
  "transitions": [
    { "from": "Idle", "to": "Walk", "blend": 0.15, "interrupt": true,
      "when": [{ "param": "speed", "gt": 1.2 }] },
    { "from": "Walk", "to": "Run", "blend": 0.12, "interrupt": true,
      "when": [{ "param": "speed", "gt": 11.0 }] }
  ]
})", set, def));
	def.animSet = std::make_shared<AnimationSet>(set);
	Skeleton sk = oneBone();
	AnimGraphInstance g;
	ASSERT_TRUE(g.bind(&def, &sk));
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Idle");
	ASSERT_TRUE(g.requestStateByName("Run"));
	EXPECT_STREQ(g.player().clipName(), "Walk");
	EXPECT_NE(g.player().outgoingClip(), AnimPlayer::kInvalidClip);
	EXPECT_TRUE(g.stateLocked());
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Walk");
	AnimNotifyQueue q;
	g.player().update(0.2f, q);
	EXPECT_EQ(g.player().outgoingClip(), AnimPlayer::kInvalidClip);
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Run");
	EXPECT_TRUE(g.stateLocked());
	EXPECT_FALSE(g.pathPending());
	g.setFloat("speed", 0.0f);
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Run");
	g.setStateLocked(false);
	g.evaluate();
	EXPECT_STREQ(g.player().clipName(), "Run");
}

TEST(AnimGraph, LoadInternsSamePath)
{
	const auto gltf = GltfTest::writeSkinnedGltf("graph_model.gltf");
	const std::string json = std::string(R"({
  "version": 1,
  "model": "graph_model.gltf",
  "defaultState": "Walk",
  "states": [{ "name": "Walk", "clip": "Walk", "loop": true }]
})");
	const auto jsonPath = GltfTest::writeTempGltf("graph_model.anim.json", json);
	AssetManager mgr;
	mgr.mountDirectory(gltf.parent_path());
	auto a = mgr.loadAnimGraph(jsonPath.filename().string());
	ASSERT_TRUE(a);
	EXPECT_EQ(a->type, AssetType::AnimGraph);
	ASSERT_TRUE(a->animSet);
	EXPECT_TRUE(a->animSet->findClip("Walk"));
	auto b = mgr.loadAnimGraph(jsonPath.filename().string());
	ASSERT_TRUE(b);
	EXPECT_EQ(a.get(), b.get());
	EXPECT_FALSE(mgr.tryLoadAnimGraphForModel("missing.gltf"));
}
