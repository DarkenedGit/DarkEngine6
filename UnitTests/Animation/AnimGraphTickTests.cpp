#include <gtest/gtest.h>

#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimSampler.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"

using namespace Dark;
using namespace Dark::Math;

namespace
{
Skeleton travelingBone()
{
	Skeleton sk;
	Joint j;
	j.name = "hips";
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	j.inverseBind = Matrix4f();
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };
	sk.rootMotionJoint = 0;
	Vector3f T[1]{ j.restT };
	Quaternion R[1]{ j.restR };
	Vector3f S[1]{ j.restS };
	localToPalette(sk, T, R, S, sk.restPose);
	return sk;
}

AnimationClip travelClip(const char* name, float endX, bool loop = true)
{
	AnimationClip clip;
	clip.name = name;
	clip.duration = 1.0f;
	clip.loopDefault = loop;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, 1.0f };
	ch.values = { 0.0f, 0.0f, 0.0f, endX, 0.0f, 0.0f };
	clip.channels.push_back(ch);
	return clip;
}

AssetRef<Model> makeCpuModel(const Skeleton& sk, AssetRef<AnimationSet> set)
{
	auto model = std::make_shared<Model>();
	model->setSkeleton(sk);
	model->setAnimationSet(set);
	return model;
}
} // namespace

TEST(AnimGraphTick, SkipsZeroDt)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	auto set = std::make_shared<AnimationSet>();
	set->setClips({ travelClip("Walk", 1.0f) });
	auto model = makeCpuModel(travelingBone(), set);
	AnimGraphComponent ag;
	ag.model = model;
	ag.animSet = set;
	ag.graph.player().bind(model->skeleton(), set.get());
	ag.graph.player().play("Walk", 0.0f);
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 0.0f);
	EXPECT_NEAR(world.get<AnimGraphComponent>(e)->graph.player().time(), 0.0f, 1.0e-5f);
}

TEST(AnimGraphTick, PlayerOnlyDoesNotEvaluate)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	auto set = std::make_shared<AnimationSet>();
	set->setClips({ travelClip("Walk", 1.0f) });
	auto model = makeCpuModel(travelingBone(), set);
	AnimGraphComponent ag;
	ag.model = model;
	ag.animSet = set;
	ag.graph.player().bind(model->skeleton(), set.get());
	ASSERT_TRUE(ag.graph.player().play("Walk", 0.0f));
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 0.25f);
	auto* c = world.get<AnimGraphComponent>(e);
	ASSERT_TRUE(c);
	EXPECT_FALSE(c->graph.started());
	EXPECT_NEAR(c->graph.player().time(), 0.25f, 1.0e-4f);
	EXPECT_STREQ(c->graph.player().clipName(), "Walk");
}

TEST(AnimGraphTick, GraphDefEvaluatesDefaultState)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	auto set = std::make_shared<AnimationSet>();
	set->setClips({ travelClip("Idle", 0.0f), travelClip("Walk", 1.0f) });
	auto def = std::make_shared<AnimGraphDef>();
	ASSERT_TRUE(parseAnimGraphJson(R"({
  "version": 1,
  "defaultState": "Idle",
  "states": [
    { "name": "Idle", "clip": "Idle", "loop": true },
    { "name": "Walk", "clip": "Walk", "loop": true }
  ],
  "transitions": [{ "from": "Idle", "to": "Walk", "blend": 0.0, "when": [{ "param": "speed", "gt": 0.1 }] }],
  "parameters": [{ "name": "speed", "type": "float", "default": 0.0 }]
})", *set, *def));
	def->animSet = set;
	auto model = makeCpuModel(travelingBone(), set);
	AnimGraphComponent ag;
	ag.model = model;
	ag.animSet = set;
	ag.graphDef = def;
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 0.1f);
	auto* c = world.get<AnimGraphComponent>(e);
	ASSERT_TRUE(c);
	EXPECT_TRUE(c->graph.started());
	EXPECT_STREQ(c->graph.player().clipName(), "Idle");
	c->graph.setFloat("speed", 1.0f);
	tickAnimGraphs(world, assets, 0.1f);
	EXPECT_STREQ(c->graph.player().clipName(), "Walk");
}

TEST(AnimGraphTick, RootMotionFlagOnMovesTransform)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	world.emplace<TransformComponent>(e);
	auto set = std::make_shared<AnimationSet>();
	set->setClips({ travelClip("Walk", 1.0f) });
	auto model = makeCpuModel(travelingBone(), set);
	AnimGraphComponent ag;
	ag.model = model;
	ag.animSet = set;
	ag.graph.player().bind(model->skeleton(), set.get());
	ag.graph.setApplyRootMotion(true);
	ag.graph.player().play("Walk", 0.0f);
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 1.0f);
	auto* xf = world.get<TransformComponent>(e);
	ASSERT_TRUE(xf);
	EXPECT_NEAR(xf->position.x, 1.0f, 2.0e-2f);
}

TEST(AnimGraphTick, RootMotionFlagOffLeavesTransform)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	world.emplace<TransformComponent>(e);
	auto set = std::make_shared<AnimationSet>();
	set->setClips({ travelClip("Walk", 1.0f) });
	auto model = makeCpuModel(travelingBone(), set);
	AnimGraphComponent ag;
	ag.model = model;
	ag.animSet = set;
	ag.graph.player().bind(model->skeleton(), set.get());
	ag.graph.player().play("Walk", 0.0f);
	ag.graph.setApplyRootMotion(false);
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 1.0f);
	auto* xf = world.get<TransformComponent>(e);
	ASSERT_TRUE(xf);
	EXPECT_NEAR(xf->position.x, 0.0f, 1.0e-5f);
}

TEST(AnimGraphTick, MissingSkeletonWarnsOnce)
{
	World world;
	AssetManager assets;
	Entity e = world.createEntity();
	AnimGraphComponent ag;
	ag.model = std::make_shared<Model>();
	world.emplace<AnimGraphComponent>(e, std::move(ag));
	tickAnimGraphs(world, assets, 0.1f);
	tickAnimGraphs(world, assets, 0.1f);
	EXPECT_TRUE(world.get<AnimGraphComponent>(e)->warnedMissing);
}
