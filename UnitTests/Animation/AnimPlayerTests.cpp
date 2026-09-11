#include <gtest/gtest.h>

#include "Animation/AnimPlayer.h"
#include "Animation/AnimSampler.h"
#include "Math/MathHelper.h"

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

AnimationClip transClip(const char* name, float endX, bool loop = true)
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
} // namespace

TEST(AnimPlayer, SameClipWithoutRestartKeepsTime)
{
	Skeleton sk = oneBone();
	AnimationSet set;
	set.setClips({ transClip("Idle", 0.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.play("Idle", 0.0f);
	AnimNotifyQueue q;
	p.update(0.4f, q);
	EXPECT_NEAR(p.time(), 0.4f, 1.0e-4f);
	EXPECT_TRUE(p.play("Idle", 0.0f, false));
	EXPECT_NEAR(p.time(), 0.4f, 1.0e-4f);
	EXPECT_TRUE(p.play("Idle", 0.0f, true));
	EXPECT_NEAR(p.time(), 0.0f, 1.0e-4f);
}

TEST(AnimPlayer, UnknownClipKeepsPose)
{
	Skeleton sk = oneBone();
	AnimationSet set;
	set.setClips({ transClip("Idle", 0.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.play("Idle", 0.0f);
	EXPECT_FALSE(p.play("Nope", 0.0f));
	EXPECT_STREQ(p.clipName(), "Idle");
}

TEST(AnimPlayer, OneshotFinishes)
{
	Skeleton sk = oneBone();
	AnimationSet set;
	set.setClips({ transClip("Attack", 0.0f, false) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.play("Attack", 0.0f);
	AnimNotifyQueue q;
	p.update(1.5f, q);
	EXPECT_TRUE(p.finished());
	EXPECT_NEAR(p.time(), 1.0f, 1.0e-4f);
}

TEST(AnimPlayer, CrossfadeAlpha)
{
	Skeleton sk = oneBone();
	AnimationSet set;
	set.setClips({ transClip("A", 0.0f), transClip("B", 2.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.play("A", 0.0f);
	AnimNotifyQueue q;
	p.update(0.1f, q);
	p.play("B", 0.2f);
	EXPECT_EQ(p.outgoingClip(), 0u);
	EXPECT_NEAR(p.blendAlpha(), 0.0f, 1.0e-4f);
	p.update(0.1f, q);
	EXPECT_NEAR(p.blendAlpha(), 0.5f, 5.0e-2f);
	p.update(0.2f, q);
	EXPECT_EQ(p.outgoingClip(), AnimPlayer::kInvalidClip);
	EXPECT_NEAR(p.blendAlpha(), 1.0f, 1.0e-4f);
}

TEST(AnimPlayer, NegativeSpeedClamped)
{
	Skeleton sk = oneBone();
	AnimationSet set;
	set.setClips({ transClip("Idle", 0.0f) });
	AnimPlayer p;
	p.bind(&sk, &set);
	p.setSpeed(-2.0f);
	p.play("Idle", 0.0f);
	AnimNotifyQueue q;
	p.update(0.5f, q);
	EXPECT_NEAR(p.time(), 0.0f, 1.0e-5f);
}
