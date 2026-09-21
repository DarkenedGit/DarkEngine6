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

bool hasState(const AnimGraphDef& def, const char* name)
{
    for (const AnimStateDef& st : def.states)
    {
        if (st.name == name)
            return true;
    }
    return false;
}
} // namespace

TEST(WolfGltf, HasLocomotionBiteAndJumpClips)
{
    const auto gltf = findContentFile("models/wolf.gltf");
    ASSERT_FALSE(gltf.empty()) << "content/models/wolf.gltf not found";

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    ASSERT_EQ(cpu.skeleton.joints.size(), 23u);
    ASSERT_FALSE(cpu.primitives.empty());
    EXPECT_TRUE(cpu.primitives[0].skinned);
    EXPECT_EQ(cpu.primitives[0].mesh.positions.size(), cpu.primitives[0].mesh.jointPacked.size());
    EXPECT_TRUE(hasClip(cpu.clips, "Idle"));
    EXPECT_TRUE(hasClip(cpu.clips, "Walk"));
    EXPECT_TRUE(hasClip(cpu.clips, "Run"));
    EXPECT_TRUE(hasClip(cpu.clips, "Bite"));
    EXPECT_TRUE(hasClip(cpu.clips, "Jump"));
    EXPECT_TRUE(hasClip(cpu.clips, "Die"));
}

TEST(WolfGltf, GraphBindsClips)
{
    const auto gltf     = findContentFile("models/wolf.gltf");
    const auto jsonPath = findContentFile("models/wolf.anim.json");
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
    EXPECT_EQ(def.states[def.defaultState].name, "Idle");
    EXPECT_TRUE(hasState(def, "Walk"));
    EXPECT_TRUE(hasState(def, "Run"));
    EXPECT_TRUE(hasState(def, "Bite"));
    EXPECT_TRUE(hasState(def, "Jump"));
    EXPECT_TRUE(hasState(def, "Die"));
}
