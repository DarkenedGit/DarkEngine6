#include <gtest/gtest.h>

#include "Animation/AnimGraphJson.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Core/ContentRoots.h"
#include "Scene/EntityMaster.h"
#include "Scene/SceneFile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Dark;

namespace
{

std::filesystem::path findContentFile(const char* relative)
{
    for (const auto& root : contentRootCandidates())
    {
        std::error_code ec;
        const auto      path = root / relative;
        if (std::filesystem::is_regular_file(path, ec) && !ec)
            return path;
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

struct AnimalRow
{
    const char* type;
    const char* gltf;
};

constexpr AnimalRow kAnimals[] = {
    { "alpaca", "models/AnimatedAnimals/Alpaca.gltf" },
    { "bull", "models/AnimatedAnimals/Bull.gltf" },
    { "cow", "models/AnimatedAnimals/Cow.gltf" },
    { "deer", "models/AnimatedAnimals/Deer.gltf" },
    { "donkey", "models/AnimatedAnimals/Donkey.gltf" },
    { "fox", "models/AnimatedAnimals/Fox.gltf" },
    { "horse", "models/AnimatedAnimals/Horse.gltf" },
    { "horse_white", "models/AnimatedAnimals/Horse_White.gltf" },
    { "husky", "models/AnimatedAnimals/Husky.gltf" },
    { "shibainu", "models/AnimatedAnimals/ShibaInu.gltf" },
    { "stag", "models/AnimatedAnimals/Stag.gltf" },
    { "animal_wolf", "models/AnimatedAnimals/Wolf.gltf" },
};

} // namespace

TEST(AnimatedAnimals, EntityMastersPointAtGraphs)
{
    const EntityMaster gameplayWolf = loadEntityMasterType("wolf");
    EXPECT_EQ(gameplayWolf.gltf, "models/wolf.gltf");
    EXPECT_EQ(gameplayWolf.anim, "models/wolf.anim.json");

    for (const AnimalRow& row : kAnimals)
    {
        const EntityMaster master = loadEntityMasterType(row.type);
        EXPECT_EQ(master.type, row.type) << row.type;
        EXPECT_EQ(master.gltf, row.gltf) << row.type;
        std::filesystem::path anim(row.gltf);
        anim.replace_extension(".anim.json");
        EXPECT_EQ(master.anim, anim.generic_string()) << row.type;
        EXPECT_TRUE(master.hsm.empty()) << row.type;
        EXPECT_FALSE(findContentFile(master.gltf.c_str()).empty()) << master.gltf;
        EXPECT_FALSE(findContentFile(master.anim.c_str()).empty()) << master.anim;
    }
}

TEST(AnimatedAnimals, GraphsCoverEveryClip)
{
    for (const AnimalRow& row : kAnimals)
    {
        const EntityMaster master = loadEntityMasterType(row.type);
        const auto         gltf   = findContentFile(master.gltf.c_str());
        const auto         json   = findContentFile(master.anim.c_str());
        ASSERT_FALSE(gltf.empty()) << row.type;
        ASSERT_FALSE(json.empty()) << row.type;

        GltfCpuModel cpu;
        ASSERT_TRUE(parseGltfFile(gltf, cpu)) << row.type;
        AnimationSet set;
        set.setFromParsed(cpu);
        ASSERT_GT(set.clipCount(), 0u) << row.type;

        const std::string text = readText(json);
        AnimGraphDef      def;
        ASSERT_TRUE(parseAnimGraphJson(text.c_str(), set, def)) << row.type;
        EXPECT_EQ(def.states.size(), set.clipCount()) << row.type;
        EXPECT_EQ(def.states[def.defaultState].name, "Idle") << row.type;

        std::vector<uint8_t> seen(set.clipCount(), 0);
        for (const AnimStateDef& state : def.states)
        {
            ASSERT_LT(state.clipIndex, set.clipCount()) << row.type;
            EXPECT_EQ(seen[state.clipIndex], 0) << row.type << " " << state.name;
            seen[state.clipIndex] = 1;
        }
    }
}

TEST(AnimatedAnimals, ViewScenePlacesEachModel)
{
    const auto scenePath = findContentFile("scenes/viewmodel.json");
    ASSERT_FALSE(scenePath.empty());

    SceneFileData scene;
    std::string   error;
    ASSERT_TRUE(loadSceneFromJson(scenePath, scene, &error)) << error;
    EXPECT_EQ(scene.name, "viewmodel");
    EXPECT_EQ(scene.mode, SceneMode::Scene3D);

    std::vector<std::string> placed;
    for (const SceneObjectData& object : scene.objects)
    {
        if (object.type != SceneObjectType::Model)
            continue;
        placed.push_back(object.modelPath);
    }
    ASSERT_EQ(placed.size(), std::size(kAnimals));
    for (const AnimalRow& row : kAnimals)
    {
        bool found = false;
        for (const std::string& path : placed)
        {
            if (path == row.gltf)
                found = true;
        }
        EXPECT_TRUE(found) << row.gltf;
    }
}
