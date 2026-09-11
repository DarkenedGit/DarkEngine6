#include <gtest/gtest.h>

#include "Animation/AnimPlayer.h"
#include "Animation/AnimSampler.h"
#include "Math/MathHelper.h"

#include <cmath>

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

AnimationClip travelClip(const char* name, float endX)
{
	AnimationClip clip;
	clip.name = name;
	clip.duration = 1.0f;
	clip.loopDefault = true;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, 1.0f };
	ch.values = { 0.0f, 0.0f, 0.0f, endX, 0.0f, 0.0f };
	clip.channels.push_back(ch);
	return clip;
}
} // namespace

TEST(AnimRootMotion, FlagOnMovesOneMeter)
{
	Skeleton sk = travelingBone();
	AnimationSet set;
	set.setClips({ travelClip("Walk", 1.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.setApplyRootMotion(true);
	ASSERT_TRUE(p.play("Walk", 0.0f));
	EXPECT_TRUE(NearEqual(p.rootMotionDelta().x, 0.0f, 1.0e-5f));

	Vector3f pos = Vector3f::ZERO;
	AnimNotifyQueue q;
	const float dt = 1.0f / 60.0f;
	for (int i = 0; i < 60; ++i)
	{
		p.update(dt, q);
		pos += p.rootMotionDelta();
	}
	EXPECT_NEAR(pos.x, 1.0f, 2.0e-2f);
}

TEST(AnimRootMotion, FlagOffStaysPut)
{
	Skeleton sk = travelingBone();
	AnimationSet set;
	set.setClips({ travelClip("Walk", 1.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.setApplyRootMotion(false);
	p.play("Walk", 0.0f);
	Vector3f pos = Vector3f::ZERO;
	AnimNotifyQueue q;
	for (int i = 0; i < 60; ++i)
	{
		p.update(1.0f / 60.0f, q);
		pos += p.rootMotionDelta();
	}
	EXPECT_NEAR(pos.x, 0.0f, 1.0e-5f);
}

TEST(AnimRootMotion, LoopWrapDoesNotRewind)
{
	Skeleton sk = travelingBone();
	AnimationSet set;
	set.setClips({ travelClip("Walk", 1.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.setApplyRootMotion(true);
	p.play("Walk", 0.0f);
	AnimNotifyQueue q;
	p.update(0.9f, q);
	const float d = p.rootMotionDelta().x;
	EXPECT_GT(d, 0.5f);
	p.update(0.2f, q);
	EXPECT_GT(p.rootMotionDelta().x, 0.0f);
	EXPECT_LT(p.rootMotionDelta().x, 0.4f);
}

TEST(AnimRootMotion, TwoPlayersIndependentFlags)
{
	Skeleton sk = travelingBone();
	AnimationSet set;
	set.setClips({ travelClip("Walk", 1.0f) });
	AnimPlayer a;
	AnimPlayer b;
	a.bind(&sk, &set);
	b.bind(&sk, &set);
	a.setApplyRootMotion(true);
	b.setApplyRootMotion(false);
	a.play("Walk", 0.0f);
	b.play("Walk", 0.0f);
	AnimNotifyQueue q;
	a.update(1.0f, q);
	b.update(1.0f, q);
	EXPECT_NEAR(a.rootMotionDelta().x, 1.0f, 2.0e-2f);
	EXPECT_NEAR(b.rootMotionDelta().x, 0.0f, 1.0e-5f);
}

TEST(AnimRootMotion, BlendDoesNotSnapToIncoming)
{
	Skeleton sk = travelingBone();
	AnimationSet set;
	set.setClips({ travelClip("A", 1.0f), travelClip("B", 1.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.setApplyRootMotion(true);
	p.play("A", 0.0f);
	AnimNotifyQueue q;
	p.update(0.5f, q);
	EXPECT_NEAR(p.rootMotionDelta().x, 0.5f, 5.0e-2f);
	p.play("B", 0.2f);
	p.update(0.05f, q);
	EXPECT_GT(p.rootMotionDelta().x, -0.2f);
	EXPECT_LT(p.rootMotionDelta().x, 0.2f);
}
