#include <gtest/gtest.h>

#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Core/ContentRoots.h"

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
    EXPECT_EQ(def.states.size(), 11u);
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
