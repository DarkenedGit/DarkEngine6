#include <gtest/gtest.h>

#include "Animation/AnimSampler.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"

#include <cmath>

using namespace Dark;
using namespace Dark::Math;

namespace
{
Vector3f xformPoint(const Matrix4f& m, const Vector3f& p)
{
	const Vector4f o = m * Vector4f(p, 1.0f);
	return Vector3f(o.x, o.y, o.z);
}

bool Near(const Vector3f& a, const Vector3f& b, float eps = 1.0e-3f)
{
	return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
}

Skeleton makeTwoJointArmature()
{
	Skeleton sk;
	sk.meshWorld = Matrix4f::TranslationMatrix(3.0f, 0.0f, 0.0f);

	Joint hips;
	hips.name = "hips";
	hips.parent = -1;
	hips.restT = Vector3f::ZERO;
	hips.restR = Quaternion::IDENTITY;
	hips.restS = Vector3f(1.0f, 1.0f, 1.0f);
	hips.ancestorBindWorld = Matrix4f::TranslationMatrix(0.0f, 4.0f, 0.0f);
	hips.inverseBind = Matrix4f::TranslationMatrix(0.0f, -4.0f, 0.0f) * sk.meshWorld;

	Joint spine;
	spine.name = "spine";
	spine.parent = 0;
	spine.restT = Vector3f(0.0f, 1.0f, 0.0f);
	spine.restR = Quaternion::IDENTITY;
	spine.restS = Vector3f(1.0f, 1.0f, 1.0f);
	spine.ancestorBindWorld = Matrix4f();
	spine.inverseBind = Matrix4f::TranslationMatrix(0.0f, -5.0f, 0.0f) * sk.meshWorld;

	sk.joints.push_back(hips);
	sk.joints.push_back(spine);
	sk.fkOrder = { 0, 1 };
	sk.rootMotionJoint = 0;

	Vector3f T[2]{ hips.restT, spine.restT };
	Quaternion R[2]{ hips.restR, spine.restR };
	Vector3f S[2]{ hips.restS, spine.restS };
	localToPalette(sk, T, R, S, sk.restPose);
	return sk;
}
} // namespace

TEST(AnimSampler, ComposeLocalMatchesMakeWorldMatrix)
{
	const Vector3f t(1.0f, 2.0f, 3.0f);
	const Quaternion r = Quaternion::IDENTITY;
	const Vector3f s(2.0f, 2.0f, 2.0f);
	const Matrix4f m = composeLocal(t, r, s);
	const Vector3f p = xformPoint(m, Vector3f(1.0f, 0.0f, 0.0f));
	EXPECT_TRUE(Near(p, Vector3f(3.0f, 2.0f, 3.0f)));
}

TEST(AnimSampler, LinearTranslationMidpoint)
{
	Skeleton sk;
	Joint j;
	j.restT = Vector3f::ZERO;
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };

	AnimationClip clip;
	clip.name = "move";
	clip.duration = 1.0f;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, 1.0f };
	ch.values = { 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };
	clip.channels.push_back(ch);

	Vector3f T[1];
	Quaternion R[1];
	Vector3f S[1];
	ASSERT_TRUE(sampleClipLocal(clip, sk, 0.5f, T, R, S));
	EXPECT_TRUE(Near(T[0], Vector3f(1.0f, 0.0f, 0.0f)));
	ASSERT_TRUE(sampleClipLocal(clip, sk, 0.0f, T, R, S));
	EXPECT_TRUE(Near(T[0], Vector3f(0.0f, 0.0f, 0.0f)));
	ASSERT_TRUE(sampleClipLocal(clip, sk, 1.0f, T, R, S));
	EXPECT_TRUE(Near(T[0], Vector3f(2.0f, 0.0f, 0.0f)));
}

TEST(AnimSampler, StepHoldsLeftKey)
{
	Skeleton sk;
	Joint j;
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };

	AnimationClip clip;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Step;
	ch.times = { 0.0f, 1.0f };
	ch.values = { 0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f };
	clip.channels.push_back(ch);

	Vector3f T[1];
	Quaternion R[1];
	Vector3f S[1];
	ASSERT_TRUE(sampleClipLocal(clip, sk, 0.9f, T, R, S));
	EXPECT_TRUE(Near(T[0], Vector3f(0.0f, 0.0f, 0.0f)));
}

TEST(AnimSampler, RotationUsesSlerp)
{
	Skeleton sk;
	Joint j;
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };

	const Quaternion q0 = Quaternion::IDENTITY;
	const Quaternion q1 = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, 0.5f * Pi);

	AnimationClip clip;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Rotation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, 1.0f };
	ch.values = { q0.w, q0.x, q0.y, q0.z, q1.w, q1.x, q1.y, q1.z };
	clip.channels.push_back(ch);

	Vector3f T[1];
	Quaternion R[1];
	Vector3f S[1];
	ASSERT_TRUE(sampleClipLocal(clip, sk, 0.5f, T, R, S));
	const Quaternion expect = Quaternion::Slerp(q0, q1, 0.5f);
	EXPECT_NEAR(R[0].w, expect.w, 1.0e-4f);
	EXPECT_NEAR(R[0].y, expect.y, 1.0e-4f);
}

TEST(AnimSampler, EmptyClipFails)
{
	Skeleton sk;
	sk.joints.push_back(Joint{});
	AnimationClip clip;
	Vector3f T[1];
	Quaternion R[1];
	Vector3f S[1];
	EXPECT_FALSE(sampleClipLocal(clip, sk, 0.0f, T, R, S));
}

TEST(AnimSampler, BindVsStaticTranslatedMeshNode)
{
	Skeleton sk = makeTwoJointArmature();
	const Vector3f pos(1.0f, 0.0f, 0.0f);
	const Vector3f skinned = xformPoint(sk.restPose.palette[0], pos);
	const Vector3f drawn = xformPoint(sk.meshWorld, skinned);
	const Vector3f baked = xformPoint(sk.meshWorld, pos);
	EXPECT_TRUE(Near(drawn, baked));
}

TEST(AnimSampler, RestPoseStoresJointWorld)
{
	Skeleton sk = makeTwoJointArmature();
	EXPECT_EQ(sk.restPose.boneCount, 2u);
	EXPECT_TRUE(Near(sk.restPose.jointWorld[0].GetTranslation(), Vector3f(0.0f, 4.0f, 0.0f)));
	EXPECT_TRUE(Near(sk.restPose.jointWorld[1].GetTranslation(), Vector3f(0.0f, 5.0f, 0.0f)));
}

TEST(AnimSampler, AncestorSurvivesClipOnSkinRoot)
{
	Skeleton sk = makeTwoJointArmature();

	AnimationClip clip;
	clip.duration = 1.0f;
	AnimChannel ch;
	ch.joint = 0;
	ch.path = AnimPath::Translation;
	ch.interp = AnimInterp::Linear;
	ch.times = { 0.0f, 1.0f };
	ch.values = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
	clip.channels.push_back(ch);

	Vector3f T[2];
	Quaternion R[2];
	Vector3f S[2];
	ASSERT_TRUE(sampleClipLocal(clip, sk, 1.0f, T, R, S));
	EXPECT_TRUE(Near(T[0], Vector3f(1.0f, 0.0f, 0.0f)));

	AnimPose pose{};
	localToPalette(sk, T, R, S, pose);
	const Matrix4f jointWorldMesh = sk.joints[0].inverseBind.Inverse() * pose.palette[0];
	const Vector3f hipsWorld = xformPoint(sk.meshWorld * jointWorldMesh, Vector3f::ZERO);
	EXPECT_NEAR(hipsWorld.y, 4.0f, 1.0e-3f);
	EXPECT_NEAR(hipsWorld.x, 1.0f, 1.0e-3f);
}

TEST(AnimSampler, ChildFirstFkOrder)
{
	Skeleton sk;
	sk.meshWorld = Matrix4f();

	Joint child;
	child.name = "child";
	child.parent = 1;
	child.restS = Vector3f(1.0f, 1.0f, 1.0f);
	child.inverseBind = Matrix4f();

	Joint parent;
	parent.name = "parent";
	parent.parent = -1;
	parent.restS = Vector3f(1.0f, 1.0f, 1.0f);
	parent.inverseBind = Matrix4f();

	sk.joints.push_back(child); // index 0 is child
	sk.joints.push_back(parent);
	sk.fkOrder = { 1, 0 };

	Vector3f T[2]{ child.restT, parent.restT };
	Quaternion R[2]{ child.restR, parent.restR };
	Vector3f S[2]{ child.restS, parent.restS };
	AnimPose pose{};
	localToPalette(sk, T, R, S, pose);
	EXPECT_EQ(pose.boneCount, 2u);
	EXPECT_EQ(sk.fkOrder[0], 1u);
	EXPECT_EQ(sk.fkOrder[1], 0u);
}

TEST(AnimSampler, TranslateJointMultiplyOrder)
{
	Skeleton sk;
	sk.meshWorld = Matrix4f();
	Joint j;
	j.restT = Vector3f(2.0f, 0.0f, 0.0f);
	j.restS = Vector3f(1.0f, 1.0f, 1.0f);
	j.inverseBind = Matrix4f::TranslationMatrix(-2.0f, 0.0f, 0.0f);
	sk.joints.push_back(j);
	sk.fkOrder = { 0 };

	Vector3f T[1]{ j.restT };
	Quaternion R[1]{ j.restR };
	Vector3f S[1]{ j.restS };
	AnimPose pose{};
	localToPalette(sk, T, R, S, pose);

	const Vector3f p(1.0f, 0.0f, 0.0f);
	const Vector3f out = xformPoint(pose.palette[0], p);
	EXPECT_TRUE(Near(out, p));
}
