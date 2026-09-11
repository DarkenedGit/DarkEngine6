#include <gtest/gtest.h>

#include "Animation/AnimPlayer.h"
#include "Animation/AnimSampler.h"
#include "Math/MathHelper.h"

#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{
struct Fire
{
	std::string name;
	float       time = 0.0f;
	int32_t     payload = 0;
	uint32_t    clip = 0;
};

std::vector<Fire> g_fires;

void recordNotify(void* user, const AnimNotify& n)
{
	auto* out = static_cast<std::vector<Fire>*>(user);
	out->push_back(Fire{ n.name ? n.name : "", n.time, n.intPayload, n.clipIndex });
}

Skeleton oneBone()
{
	Skeleton sk;
	Joint j;
	j.name = "root";
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

AnimationClip makeClip(const char* name, float duration, std::vector<AnimMarker> markers, bool loop = true)
{
	AnimationClip clip;
	clip.name = name;
	clip.duration = duration;
	clip.loopDefault = loop;
	clip.markers = std::move(markers);
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, duration };
	ch.values = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	clip.channels.push_back(ch);
	return clip;
}

AnimationSet makeSet(std::vector<AnimationClip> clips)
{
	AnimationSet set;
	set.setClips(std::move(clips));
	return set;
}
} // namespace

TEST(AnimNotify, CrossesHalfSecondOnce)
{
	Skeleton sk = oneBone();
	AnimationSet set = makeSet({ makeClip("Walk", 1.0f, { { "footstep", 0.5f } }) });
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	ASSERT_TRUE(p.play("Walk", 0.0f));

	AnimNotifyQueue q;
	p.update(0.4f, q);
	EXPECT_TRUE(fires.empty());
	p.update(0.2f, q);
	ASSERT_EQ(fires.size(), 1u);
	EXPECT_EQ(fires[0].name, "footstep");
	EXPECT_NEAR(fires[0].time, 0.5f, 1.0e-4f);
	p.update(0.2f, q);
	EXPECT_EQ(fires.size(), 1u);
}

TEST(AnimNotify, OpenClosedWindow)
{
	Skeleton sk = oneBone();
	AnimationSet set = makeSet({ makeClip("Walk", 1.0f, { { "hit", 0.25f } }) });
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("Walk", 0.0f);
	AnimNotifyQueue q;
	p.update(0.25f, q);
	ASSERT_EQ(fires.size(), 1u);
}

TEST(AnimNotify, LoopWrapFiresEndThenZero)
{
	Skeleton sk = oneBone();
	AnimMarker endM{ "end", 1.0f };
	AnimMarker zeroM{ "zero", 0.0f };
	AnimationSet set = makeSet({ makeClip("Walk", 1.0f, { zeroM, endM }) });
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("Walk", 0.0f);
	AnimNotifyQueue q;
	p.update(0.9f, q);
	fires.clear();
	p.update(0.2f, q);
	ASSERT_EQ(fires.size(), 2u);
	EXPECT_EQ(fires[0].name, "end");
	EXPECT_EQ(fires[1].name, "zero");
}

TEST(AnimNotify, ZeroDoesNotFireOnPlay)
{
	Skeleton sk = oneBone();
	AnimationSet set = makeSet({ makeClip("Idle", 1.0f, { { "spawn", 0.0f } }) });
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("Idle", 0.0f);
	AnimNotifyQueue q;
	p.update(0.1f, q);
	EXPECT_TRUE(fires.empty());
}

TEST(AnimNotify, BlendSuppressesOutgoing)
{
	Skeleton sk = oneBone();
	AnimationClip a = makeClip("A", 1.0f, { { "a", 0.3f }, { "a2", 0.6f } });
	AnimationClip b = makeClip("B", 1.0f, { { "b", 0.2f } });
	AnimationSet set = makeSet({ a, b });
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("A", 0.0f);
	AnimNotifyQueue q;
	p.update(0.35f, q);
	ASSERT_EQ(fires.size(), 1u);
	EXPECT_EQ(fires[0].name, "a");
	fires.clear();
	p.play("B", 0.5f);
	p.update(0.3f, q);
	for (const Fire& f : fires)
		EXPECT_NE(f.name, "a2");
	bool sawB = false;
	for (const Fire& f : fires)
	{
		if (f.name == "b")
			sawB = true;
	}
	EXPECT_TRUE(sawB);
}

TEST(AnimNotify, OverlayWinsSameNameTime)
{
	Skeleton sk = oneBone();
	AnimMarker clipM{ "footstep", 0.32f, 1, 0.0f };
	AnimationSet set = makeSet({ makeClip("Walk", 1.0f, { clipM }) });
	AnimMarker overlay{ "footstep", 0.32f, 7, 0.0f };
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("Walk", 0.0f);
	AnimNotifyQueue q;
	p.update(0.4f, q, &overlay, 1);
	ASSERT_EQ(fires.size(), 1u);
	EXPECT_EQ(fires[0].name, "footstep");
	EXPECT_EQ(fires[0].payload, 7);
}

TEST(AnimNotify, OverlayAppendsDistinct)
{
	Skeleton sk = oneBone();
	AnimationSet set = makeSet({ makeClip("Walk", 1.0f, { { "footstep", 0.2f } }) });
	AnimMarker overlay{ "whoosh", 0.2f };
	AnimPlayer p;
	p.bind(&sk, &set);
	std::vector<Fire> fires;
	p.addListener(recordNotify, &fires);
	p.play("Walk", 0.0f);
	AnimNotifyQueue q;
	p.update(0.3f, q, &overlay, 1);
	ASSERT_EQ(fires.size(), 2u);
	EXPECT_EQ(fires[0].name, "footstep");
	EXPECT_EQ(fires[1].name, "whoosh");
}
