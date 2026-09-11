#include <gtest/gtest.h>

#include "Animation/AnimSampler.h"
#include "Animation/SkeletonDebug.h"
#include "Math/MathHelper.h"

using namespace Dark;
using namespace Dark::Math;

namespace
{
bool Near(const Vector3f& a, const Vector3f& b, float eps = 1.0e-3f)
{
	return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
}

Skeleton twoJoint()
{
	Skeleton sk;
	Joint hips;
	hips.parent = -1;
	hips.restS = Vector3f(1.0f, 1.0f, 1.0f);
	hips.ancestorBindWorld = Matrix4f::TranslationMatrix(0.0f, 4.0f, 0.0f);
	hips.inverseBind = Matrix4f();
	Joint spine;
	spine.parent = 0;
	spine.restT = Vector3f(0.0f, 1.0f, 0.0f);
	spine.restS = Vector3f(1.0f, 1.0f, 1.0f);
	spine.inverseBind = Matrix4f();
	sk.joints.push_back(hips);
	sk.joints.push_back(spine);
	sk.fkOrder = { 0, 1 };
	Vector3f T[2]{ hips.restT, spine.restT };
	Quaternion R[2]{ hips.restR, spine.restR };
	Vector3f S[2]{ hips.restS, spine.restS };
	localToPalette(sk, T, R, S, sk.restPose);
	return sk;
}
} // namespace

TEST(SkeletonDebug, EmptyPoseIsNoOp)
{
	Skeleton sk = twoJoint();
	AnimPose pose{};
	SkeletonDebugLines lines;
	collectSkeletonDebugLines(sk, pose, Matrix4f(), 0.25f, lines);
	EXPECT_TRUE(lines.bones.empty());
	EXPECT_TRUE(lines.axisX.empty());
}

TEST(SkeletonDebug, BoneAndAxesAtIdentityEntity)
{
	Skeleton sk = twoJoint();
	SkeletonDebugLines lines;
	collectSkeletonDebugLines(sk, sk.restPose, Matrix4f(), 0.25f, lines);
	ASSERT_EQ(lines.bones.size(), 2u);
	EXPECT_TRUE(Near(lines.bones[0], Vector3f(0.0f, 4.0f, 0.0f)));
	EXPECT_TRUE(Near(lines.bones[1], Vector3f(0.0f, 5.0f, 0.0f)));
	ASSERT_EQ(lines.axisX.size(), 4u);
	ASSERT_EQ(lines.axisY.size(), 4u);
	ASSERT_EQ(lines.axisZ.size(), 4u);
	EXPECT_TRUE(Near(lines.axisY[0], Vector3f(0.0f, 4.0f, 0.0f)));
	EXPECT_TRUE(Near(lines.axisY[1], Vector3f(0.0f, 4.25f, 0.0f)));
}

TEST(SkeletonDebug, EntityTranslationMovesOverlay)
{
	Skeleton sk = twoJoint();
	SkeletonDebugLines lines;
	const Matrix4f world = Matrix4f::TranslationMatrix(8.0f, 0.0f, 3.0f);
	collectSkeletonDebugLines(sk, sk.restPose, world, 0.25f, lines);
	ASSERT_EQ(lines.bones.size(), 2u);
	EXPECT_TRUE(Near(lines.bones[0], Vector3f(8.0f, 4.0f, 3.0f)));
	EXPECT_TRUE(Near(lines.bones[1], Vector3f(8.0f, 5.0f, 3.0f)));
}
