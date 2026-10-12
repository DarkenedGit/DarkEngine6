#include <gtest/gtest.h>

#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimSampler.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Core/ContentRoots.h"
#include "Math/Vector4f.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Dark;

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

bool hasClip(const std::vector<AnimationClip>& clips, const char* name)
{
    for (const AnimationClip& c : clips)
    {
        if (c.name == name)
            return true;
    }
    return false;
}
} // namespace

TEST(HumanoidGltf, HumanHasLocomotionAndCombatClips)
{
    const auto gltf = findContentFile("models/human.gltf");
    ASSERT_FALSE(gltf.empty()) << "content/models/human.gltf not found";

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    ASSERT_EQ(cpu.skeleton.joints.size(), 17u);
    ASSERT_FALSE(cpu.primitives.empty());
    EXPECT_TRUE(cpu.primitives[0].skinned);
    EXPECT_EQ(cpu.primitives[0].mesh.positions.size(), cpu.primitives[0].mesh.jointPacked.size());
    EXPECT_TRUE(hasClip(cpu.clips, "Idle"));
    EXPECT_TRUE(hasClip(cpu.clips, "Walk"));
    EXPECT_TRUE(hasClip(cpu.clips, "Run"));
    EXPECT_TRUE(hasClip(cpu.clips, "WalkBack"));
    EXPECT_TRUE(hasClip(cpu.clips, "RunBack"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeLeft"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRight"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRunLeft"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRunRight"));
    EXPECT_TRUE(hasClip(cpu.clips, "Shoot"));
    EXPECT_TRUE(hasClip(cpu.clips, "SwingSword"));
    EXPECT_TRUE(hasClip(cpu.clips, "Crouch"));
    EXPECT_TRUE(hasClip(cpu.clips, "CrouchWalk"));
    EXPECT_FALSE(hasClip(cpu.clips, "Die"));
}

TEST(HumanoidGltf, SkeletonHasDieClip)
{
    const auto gltf = findContentFile("models/skeleton.gltf");
    ASSERT_FALSE(gltf.empty()) << "content/models/skeleton.gltf not found";

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    ASSERT_EQ(cpu.skeleton.joints.size(), 17u);
    EXPECT_TRUE(hasClip(cpu.clips, "Idle"));
    EXPECT_TRUE(hasClip(cpu.clips, "Walk"));
    EXPECT_TRUE(hasClip(cpu.clips, "Run"));
    EXPECT_TRUE(hasClip(cpu.clips, "WalkBack"));
    EXPECT_TRUE(hasClip(cpu.clips, "RunBack"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeLeft"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRight"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRunLeft"));
    EXPECT_TRUE(hasClip(cpu.clips, "StrafeRunRight"));
    EXPECT_TRUE(hasClip(cpu.clips, "Shoot"));
    EXPECT_TRUE(hasClip(cpu.clips, "SwingSword"));
    EXPECT_TRUE(hasClip(cpu.clips, "Die"));
}

void skinnedVerticalSpan(const GltfCpuModel& cpu, const AnimationClip& clip, float time, float& outMinY, float& outMaxY, bool& ok)
{
    using namespace Dark::Math;
    outMinY = 0.0f;
    outMaxY = 0.0f;
    ok      = false;
    const uint32_t n = static_cast<uint32_t>(cpu.skeleton.joints.size());
    if (n == 0 || n > AnimPose::kMaxBones || cpu.primitives.empty())
        return;
    const MeshData& mesh = cpu.primitives[0].mesh;
    if (mesh.positions.size() != mesh.jointPacked.size() || mesh.positions.size() != mesh.weights.size())
        return;

    Vector3f   T[AnimPose::kMaxBones];
    Quaternion R[AnimPose::kMaxBones];
    Vector3f   S[AnimPose::kMaxBones];
    if (!sampleClipLocal(clip, cpu.skeleton, time, T, R, S))
        return;
    AnimPose pose;
    localToPalette(cpu.skeleton, T, R, S, pose);

    float minY = 1.0e9f;
    float maxY = -1.0e9f;
    for (size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const uint32_t packed = mesh.jointPacked[i];
        const Vector4f w     = mesh.weights[i];
        const float    wk[4] = { w.x, w.y, w.z, w.w };
        const Vector4f hp(mesh.positions[i], 1.0f);
        Vector3f       skinned = Vector3f::ZERO;
        for (int k = 0; k < 4; ++k)
        {
            if (wk[k] <= 0.0f)
                continue;
            const uint32_t j = (packed >> (8 * k)) & 255u;
            if (j >= n)
                return;
            const Vector4f s = pose.palette[j] * hp;
            skinned.x += wk[k] * s.x;
            skinned.y += wk[k] * s.y;
            skinned.z += wk[k] * s.z;
        }
        minY = std::min(minY, skinned.y);
        maxY = std::max(maxY, skinned.y);
    }
    outMinY = minY;
    outMaxY = maxY;
    ok      = true;
}

TEST(HumanoidGltf, SkeletonDieLiesOnTheGround)
{
    // Hunter origin is the hips, 1 m above the terrain, so model y = -1 is the ground.
    const auto gltf = findContentFile("models/skeleton.gltf");
    ASSERT_FALSE(gltf.empty());

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));

    const AnimationClip* die = nullptr;
    for (const AnimationClip& clip : cpu.clips)
    {
        if (clip.name == "Die")
            die = &clip;
    }
    ASSERT_NE(die, nullptr);

    float standMin = 0.0f;
    float standMax = 0.0f;
    bool  standOk  = false;
    skinnedVerticalSpan(cpu, *die, 0.0f, standMin, standMax, standOk);
    ASSERT_TRUE(standOk);
    EXPECT_GT(standMax, 0.7f);
    EXPECT_GT(standMin, -0.7f);

    float layMin = 0.0f;
    float layMax = 0.0f;
    bool  layOk  = false;
    skinnedVerticalSpan(cpu, *die, die->duration, layMin, layMax, layOk);
    ASSERT_TRUE(layOk);
    EXPECT_NEAR(layMin, -1.0f, 0.08f);
    EXPECT_LT(layMax, -0.40f);
    EXPECT_LT(layMax - layMin, 0.55f);

    const uint32_t n = static_cast<uint32_t>(cpu.skeleton.joints.size());
    ASSERT_GT(n, 0u);
    ASSERT_LE(n, AnimPose::kMaxBones);
    Math::Vector3f   T[AnimPose::kMaxBones];
    Math::Quaternion R[AnimPose::kMaxBones];
    Math::Vector3f   S[AnimPose::kMaxBones];
    ASSERT_TRUE(sampleClipLocal(*die, cpu.skeleton, die->duration, T, R, S));
    Math::Matrix4f worlds[AnimPose::kMaxBones];
    localToJointWorlds(cpu.skeleton, T, R, S, worlds);

    const Math::Matrix4f* hips = nullptr;
    const Math::Matrix4f* head = nullptr;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (cpu.skeleton.joints[i].name == "Hips")
            hips = &worlds[i];
        if (cpu.skeleton.joints[i].name == "Head")
            head = &worlds[i];
    }
    ASSERT_NE(hips, nullptr);
    ASSERT_NE(head, nullptr);
    const Math::Vector3f hipPos  = hips->GetTranslation();
    const Math::Vector3f headPos = head->GetTranslation();
    EXPECT_LT(std::fabs(headPos.y - hipPos.y), 0.2f);
    EXPECT_GT(std::fabs(headPos.z - hipPos.z), 0.45f);
    EXPECT_LT(hipPos.y, -0.5f);
}

void expectArmsHang(const char* relativePath)
{
    const auto gltf = findContentFile(relativePath);
    ASSERT_FALSE(gltf.empty()) << relativePath;

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu)) << relativePath;

    auto joint = [&](const char* name) -> const Joint* {
        for (const Joint& j : cpu.skeleton.joints)
        {
            if (j.name == name)
                return &j;
        }
        return nullptr;
    };

    const Joint* lElbow = joint("L_LowerArm");
    const Joint* rElbow = joint("R_LowerArm");
    const Joint* lHand  = joint("L_Hand");
    const Joint* rHand  = joint("R_Hand");
    ASSERT_NE(lElbow, nullptr);
    ASSERT_NE(rElbow, nullptr);
    ASSERT_NE(lHand, nullptr);
    ASSERT_NE(rHand, nullptr);

    // Upper-arm bones: down, a little out, slightly forward. Not a T-pose along X.
    EXPECT_LT(lElbow->restT.y, -0.15f);
    EXPECT_GT(lElbow->restT.z, 0.03f);
    EXPECT_GT(lElbow->restT.x, 0.02f);
    EXPECT_GT(-lElbow->restT.y, lElbow->restT.x);
    EXPECT_LT(rElbow->restT.y, -0.15f);
    EXPECT_GT(rElbow->restT.z, 0.03f);
    EXPECT_LT(rElbow->restT.x, -0.02f);

    // Forearms keep hanging and bend a little farther forward.
    EXPECT_GT(lHand->restT.z, lElbow->restT.z);
    EXPECT_GT(rHand->restT.z, rElbow->restT.z);
    EXPECT_LT(lHand->restT.y, -0.1f);
    EXPECT_LT(rHand->restT.y, -0.1f);
}

TEST(HumanoidGltf, RestArmsHangAtTheSides)
{
    expectArmsHang("models/human.gltf");
    expectArmsHang("models/skeleton.gltf");
}

TEST(HumanoidGltf, HumanGraphBindsClips)
{
    const auto gltf     = findContentFile("models/human.gltf");
    const auto jsonPath = findContentFile("models/human.anim.json");
    ASSERT_FALSE(gltf.empty());
    ASSERT_FALSE(jsonPath.empty());

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    AnimationSet set;
    set.setFromParsed(cpu);

    const std::string text = readText(jsonPath);
    ASSERT_FALSE(text.empty());
    AnimGraphDef def;
    ASSERT_TRUE(parseAnimGraphJson(text.c_str(), set, def));
    EXPECT_EQ(def.states.size(), 15u);
    EXPECT_EQ(def.states[def.defaultState].name, "Idle");
    EXPECT_GE(set.findClipIndex("StrafeLeft"), 0);
    EXPECT_GE(set.findClipIndex("StrafeRight"), 0);
}

TEST(HumanoidGltf, SkeletonGraphBindsDie)
{
    const auto gltf     = findContentFile("models/skeleton.gltf");
    const auto jsonPath = findContentFile("models/skeleton.anim.json");
    ASSERT_FALSE(gltf.empty());
    ASSERT_FALSE(jsonPath.empty());

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    AnimationSet set;
    set.setFromParsed(cpu);

    const std::string text = readText(jsonPath);
    AnimGraphDef def;
    ASSERT_TRUE(parseAnimGraphJson(text.c_str(), set, def));
    bool hasDie = false;
    for (const AnimStateDef& st : def.states)
    {
        if (st.name == "Die")
            hasDie = true;
    }
    EXPECT_TRUE(hasDie);
}
