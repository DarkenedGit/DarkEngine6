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

using namespace Dark;

namespace
{
std::filesystem::path findContentFile(const char* relativeUnderContent)
{
    for (const auto& root : contentRootCandidates())
    {
        std::error_code ec;
        const auto p = root / relativeUnderContent;
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
} // namespace

TEST(WiggleFixture, ParsesSkinAndClips)
{
    const auto gltf = findContentFile("models/wiggle.gltf");
    ASSERT_FALSE(gltf.empty()) << "content/models/wiggle.gltf not found";

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    ASSERT_EQ(cpu.skeleton.joints.size(), 2u);
    ASSERT_FALSE(cpu.primitives.empty());
    EXPECT_TRUE(cpu.primitives[0].skinned);
    EXPECT_EQ(cpu.primitives[0].mesh.positions.size(), cpu.primitives[0].mesh.jointPacked.size());
    EXPECT_EQ(cpu.primitives[0].mesh.positions.size(), cpu.primitives[0].mesh.weights.size());

    bool idle = false;
    bool walk = false;
    for (const AnimationClip& c : cpu.clips)
    {
        if (c.name == "Idle")
            idle = true;
        if (c.name == "Walk")
            walk = true;
    }
    EXPECT_TRUE(idle);
    EXPECT_TRUE(walk);
}

TEST(WiggleFixture, SidecarGraphBindsIdleWalk)
{
    const auto gltf = findContentFile("models/wiggle.gltf");
    const auto jsonPath = findContentFile("models/wiggle.anim.json");
    ASSERT_FALSE(gltf.empty());
    ASSERT_FALSE(jsonPath.empty()) << "content/models/wiggle.anim.json not found";

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    AnimationSet set;
    set.setFromParsed(cpu);

    const std::string text = readText(jsonPath);
    ASSERT_FALSE(text.empty());
    std::string modelPath;
    ASSERT_TRUE(peekAnimGraphModelPath(text.c_str(), modelPath));
    EXPECT_EQ(modelPath, "models/wiggle.gltf");

    AnimGraphDef def;
    ASSERT_TRUE(parseAnimGraphJson(text.c_str(), set, def));
    EXPECT_EQ(def.states.size(), 2u);
    ASSERT_LT(def.defaultState, def.states.size());
    EXPECT_EQ(def.states[def.defaultState].name, "Idle");
    EXPECT_GE(set.findClipIndex("Idle"), 0);
    EXPECT_GE(set.findClipIndex("Walk"), 0);
    EXPECT_EQ(def.overlayMarkers.size(), 2u);
}
