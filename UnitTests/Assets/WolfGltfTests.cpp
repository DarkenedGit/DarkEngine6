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

TEST(WolfGltf, DieLiesOnTheGround)
{
    // Wolf origin is the hips, held 1 m above the terrain, so model y = -1 is the ground.
    const auto gltf = findContentFile("models/wolf.gltf");
    ASSERT_FALSE(gltf.empty());

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(gltf, cpu));
    ASSERT_FALSE(cpu.primitives.empty());
    const MeshData& mesh = cpu.primitives[0].mesh;
    ASSERT_EQ(mesh.positions.size(), mesh.jointPacked.size());
    ASSERT_EQ(mesh.positions.size(), mesh.weights.size());

    const AnimationClip* die = nullptr;
    for (const AnimationClip& clip : cpu.clips)
    {
        if (clip.name == "Die")
            die = &clip;
    }
    ASSERT_NE(die, nullptr);

    const uint32_t n = static_cast<uint32_t>(cpu.skeleton.joints.size());
    ASSERT_GT(n, 0u);
    ASSERT_LE(n, AnimPose::kMaxBones);

    auto spanAt = [&](float time, float& outMinY, float& outMaxY) -> bool {
        Math::Vector3f   T[AnimPose::kMaxBones];
        Math::Quaternion R[AnimPose::kMaxBones];
        Math::Vector3f   S[AnimPose::kMaxBones];
        if (!sampleClipLocal(*die, cpu.skeleton, time, T, R, S))
            return false;
        AnimPose pose;
        localToPalette(cpu.skeleton, T, R, S, pose);
        float minY = 1.0e9f;
        float maxY = -1.0e9f;
        for (size_t i = 0; i < mesh.positions.size(); ++i)
        {
            const uint32_t packed = mesh.jointPacked[i];
            const Math::Vector4f w = mesh.weights[i];
            const float wk[4] = { w.x, w.y, w.z, w.w };
            const Math::Vector4f hp(mesh.positions[i], 1.0f);
            float y = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                if (wk[k] <= 0.0f)
                    continue;
                const uint32_t j = (packed >> (8 * k)) & 255u;
                if (j >= n)
                    return false;
                y += wk[k] * (pose.palette[j] * hp).y;
            }
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        outMinY = minY;
        outMaxY = maxY;
        return true;
    };

    float standMin = 0.0f;
    float standMax = 0.0f;
    ASSERT_TRUE(spanAt(0.0f, standMin, standMax));
    EXPECT_GT(standMax, 0.2f);
    EXPECT_GT(standMin, -0.7f);

    float layMin = 0.0f;
    float layMax = 0.0f;
    ASSERT_TRUE(spanAt(die->duration, layMin, layMax));
    EXPECT_NEAR(layMin, -1.0f, 0.08f);
    EXPECT_LT(layMax, -0.45f);
    EXPECT_LT(layMax - layMin, 0.45f);
}
