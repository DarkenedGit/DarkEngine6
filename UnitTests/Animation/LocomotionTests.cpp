#include <gtest/gtest.h>

#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimSampler.h"
#include "Animation/Locomotion.h"
#include "Assets/GltfLoader.h"
#include "Core/ContentRoots.h"

#include <fstream>
#include <memory>
#include <sstream>

using namespace Dark;
using namespace Dark::Math;

namespace
{
std::filesystem::path findContentFile(const char* relativeUnderContent)
{
	for (const auto& root : contentRootCandidates())
	{
		std::error_code ec;
		const auto      p = root / relativeUnderContent;
		if (std::filesystem::exists(p, ec) && !ec)
			return p;
	}
	return {};
}

std::string readText(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

bool bindGraph(const char* gltfRel, const char* jsonRel, GltfCpuModel& cpu, AssetRef<AnimationSet>& set, AnimGraphDef& def, AnimGraphInstance& graph)
{
	const auto gltf = findContentFile(gltfRel);
	const auto jsonPath = findContentFile(jsonRel);
	if (gltf.empty() || jsonPath.empty())
		return false;
	if (!parseGltfFile(gltf, cpu))
		return false;
	set = std::make_shared<AnimationSet>();
	set->setFromParsed(cpu);
	const std::string text = readText(jsonPath);
	if (!parseAnimGraphJson(text.c_str(), *set, def))
		return false;
	def.animSet = set;
	if (!graph.bind(&def, &cpu.skeleton))
		return false;
	graph.evaluate();
	return true;
}
} // namespace

TEST(LocomotionSample, SidewaysBeatsForward)
{
	const Quaternion facing = Quaternion::IDENTITY;
	const LocomotionSample right = locomotionSample(Vector3f{ 6.0f, 0.0f, 0.0f }, facing);
	EXPECT_NEAR(right.speed, 6.0f, 1.0e-4f);
	EXPECT_NEAR(right.strafe, 6.0f, 1.0e-4f);

	const LocomotionSample left = locomotionSample(Vector3f{ -4.0f, 1.0f, 0.0f }, facing);
	EXPECT_NEAR(left.speed, 4.0f, 1.0e-4f);
	EXPECT_NEAR(left.strafe, -4.0f, 1.0e-4f);

	const LocomotionSample forward = locomotionSample(Vector3f{ 0.0f, 0.0f, 8.0f }, facing);
	EXPECT_NEAR(forward.speed, 8.0f, 1.0e-4f);
	EXPECT_NEAR(forward.strafe, 0.0f, 1.0e-4f);

	const LocomotionSample diagonal = locomotionSample(Vector3f{ 3.0f, 0.0f, 3.0f }, facing);
	EXPECT_NEAR(diagonal.strafe, 0.0f, 1.0e-4f);

	const LocomotionSample stopped = locomotionSample(Vector3f{ 0.0f, 2.0f, 0.0f }, facing);
	EXPECT_NEAR(stopped.speed, 0.0f, 1.0e-4f);
	EXPECT_NEAR(stopped.strafe, 0.0f, 1.0e-4f);
}

TEST(LocomotionGraph, HumanStrafeSelectsSideStep)
{
	GltfCpuModel           cpu;
	AssetRef<AnimationSet> set;
	AnimGraphDef           def;
	AnimGraphInstance      graph;
	ASSERT_TRUE(bindGraph("models/human.gltf", "models/human.anim.json", cpu, set, def, graph));
	EXPECT_STREQ(graph.currentStateName(), "Idle");

	graph.setFloat("speed", 4.0f);
	graph.setFloat("strafe", 4.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "StrafeRight");

	graph.setFloat("strafe", -4.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "StrafeLeft");

	graph.setFloat("speed", 14.0f);
	graph.setFloat("strafe", -14.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "StrafeRunLeft");

	graph.setFloat("strafe", 0.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "Run");

	graph.setFloat("speed", 0.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "Walk");
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "Idle");
}

TEST(LocomotionGraph, SkeletonDiesFromSideStep)
{
	GltfCpuModel           cpu;
	AssetRef<AnimationSet> set;
	AnimGraphDef           def;
	AnimGraphInstance      graph;
	ASSERT_TRUE(bindGraph("models/skeleton.gltf", "models/skeleton.anim.json", cpu, set, def, graph));

	graph.setFloat("speed", 12.0f);
	graph.setFloat("strafe", 12.0f);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "StrafeRight");
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "StrafeRunRight");

	graph.setBool("dead", true);
	graph.evaluate();
	EXPECT_STREQ(graph.currentStateName(), "Die");
}

TEST(LocomotionYaw, OffsetMatchesTravelRelativeToFacing)
{
	const Quaternion facing = Quaternion::IDENTITY;
	EXPECT_NEAR(locomotionYawOffset(Vector3f{ 0.0f, 0.0f, 4.0f }, facing), 0.0f, 1.0e-4f);
	EXPECT_NEAR(locomotionYawOffset(Vector3f{ 4.0f, 0.0f, 0.0f }, facing), HalfPi, 1.0e-4f);
	EXPECT_NEAR(locomotionYawOffset(Vector3f{ -4.0f, 0.0f, 0.0f }, facing), -HalfPi, 1.0e-4f);
	EXPECT_NEAR(locomotionYawOffset(Vector3f{ 0.2f, 0.0f, 0.0f }, facing), 0.0f, 1.0e-4f);
	EXPECT_NEAR(approachAngle(0.0f, HalfPi, 10.0f, 0.1f), 1.0f, 1.0e-4f);
}

TEST(LocomotionYaw, LowerBodyTurnsUpperBodyStays)
{
	Skeleton sk;
	sk.meshWorld = Matrix4f();
	auto add = [&](const char* name, int parent, Vector3f t) {
		Joint j;
		j.name = name;
		j.parent = parent;
		j.restT = t;
		j.restR = Quaternion::IDENTITY;
		j.restS = Vector3f(1.0f, 1.0f, 1.0f);
		j.inverseBind = Matrix4f();
		sk.joints.push_back(j);
	};
	add("Hips", -1, Vector3f(0.0f, 0.0f, 0.0f));
	add("Spine", 0, Vector3f(0.0f, 1.0f, 0.0f));
	add("Chest", 1, Vector3f(0.0f, 0.0f, 1.0f));
	add("Foot", 0, Vector3f(0.0f, -1.0f, 1.0f));
	sk.fkOrder = { 0, 1, 2, 3 };

	Vector3f T[4];
	Quaternion R[4];
	Vector3f S[4];
	for (int i = 0; i < 4; ++i)
	{
		T[i] = sk.joints[static_cast<size_t>(i)].restT;
		R[i] = Quaternion::IDENTITY;
		S[i] = Vector3f(1.0f, 1.0f, 1.0f);
	}
	applyLocomotionYawSplit(sk, R, 4, HalfPi);
	AnimPose pose{};
	localToPalette(sk, T, R, S, pose);

	const Vector3f foot = pose.jointWorld[3].GetTranslation();
	const Vector3f chest = pose.jointWorld[2].GetTranslation();
	EXPECT_NEAR(foot.x, 1.0f, 1.0e-3f);
	EXPECT_NEAR(foot.y, -1.0f, 1.0e-3f);
	EXPECT_NEAR(foot.z, 0.0f, 1.0e-3f);
	EXPECT_NEAR(chest.x, 0.0f, 1.0e-3f);
	EXPECT_NEAR(chest.y, 1.0f, 1.0e-3f);
	EXPECT_NEAR(chest.z, 1.0f, 1.0e-3f);
}
