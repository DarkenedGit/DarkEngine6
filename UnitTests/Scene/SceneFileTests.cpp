#include <gtest/gtest.h>

#include "Content/ContentDb.h"
#include "Content/SceneObjectSpawn.h"
#include "Core/ContentRoots.h"
#include "Core/Json.h"
#include "Sky/CloudVolume.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Math/MathDefines.h"
#include "Scene/SceneFile.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/GrassTypes.h"
#include "Terrain/TrailPath.h"
#include "Terrain/LeafField.h"
#include "Terrain/WindTypes.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{

std::filesystem::path tempScenePath(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

std::filesystem::path weaklyCanonical(const std::filesystem::path& path)
{
    std::error_code       ec;
    std::filesystem::path n = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : n;
}

void expectGrassDefaults(const GrassSceneDesc& grass)
{
    const Terrain::GrassParams defaults{};
    EXPECT_EQ(grass.enabled, defaults.enabled);
    EXPECT_FALSE(grass.enabled);
    EXPECT_NEAR(grass.heightMetres, defaults.heightMetres, 1.0e-5f);
    EXPECT_NEAR(grass.flexibility, defaults.flexibility, 1.0e-5f);
    EXPECT_NEAR(grass.densityScale, defaults.densityScale, 1.0e-5f);
    EXPECT_NEAR(grass.windBaseYaw, defaults.windBaseYaw, 1.0e-5f);
    EXPECT_NEAR(grass.windDeflection, defaults.windDeflection, 1.0e-5f);
    EXPECT_NEAR(grass.windTipMetres, defaults.windTipMetres, 1.0e-5f);
    EXPECT_NEAR(grass.windSpatialFreq, defaults.windSpatialFreq, 1.0e-5f);
    EXPECT_NEAR(grass.windTemporalFreq, defaults.windTemporalFreq, 1.0e-5f);
    EXPECT_NEAR(grass.faceEdge, defaults.faceEdge, 1.0e-5f);
    EXPECT_NEAR(grass.faceYaw, defaults.faceYaw, 1.0e-5f);
    EXPECT_NEAR(grass.faceLean, defaults.faceLean, 1.0e-5f);
    EXPECT_NEAR(grass.faceSteep, defaults.faceSteep, 1.0e-5f);
    EXPECT_EQ(grass.seed, defaults.seed);
}

void expectGrassMatchesClamp(const GrassSceneDesc& grass, Terrain::GrassParams params)
{
    Terrain::clampGrassParams(params);
    EXPECT_EQ(grass.enabled, params.enabled);
    EXPECT_NEAR(grass.heightMetres, params.heightMetres, 1.0e-5f);
    EXPECT_NEAR(grass.flexibility, params.flexibility, 1.0e-5f);
    EXPECT_NEAR(grass.densityScale, params.densityScale, 1.0e-5f);
    EXPECT_NEAR(grass.windBaseYaw, params.windBaseYaw, 1.0e-5f);
    EXPECT_NEAR(grass.windDeflection, params.windDeflection, 1.0e-5f);
    EXPECT_NEAR(grass.windTipMetres, params.windTipMetres, 1.0e-5f);
    EXPECT_NEAR(grass.windSpatialFreq, params.windSpatialFreq, 1.0e-5f);
    EXPECT_NEAR(grass.windTemporalFreq, params.windTemporalFreq, 1.0e-5f);
    EXPECT_NEAR(grass.faceEdge, params.faceEdge, 1.0e-5f);
    EXPECT_NEAR(grass.faceYaw, params.faceYaw, 1.0e-5f);
    EXPECT_NEAR(grass.faceLean, params.faceLean, 1.0e-5f);
    EXPECT_NEAR(grass.faceSteep, params.faceSteep, 1.0e-5f);
    EXPECT_EQ(grass.seed, params.seed);
}

std::vector<std::string>* g_sceneLogs = nullptr;

void captureSceneLog(LogLevel, LogCategory, const char* message)
{
    if (g_sceneLogs != nullptr && message != nullptr)
        g_sceneLogs->push_back(message);
}

} // namespace

struct SceneFile : ::testing::Test
{
    void SetUp() override
    {
        if (!contentHasArchetype("core:cube"))
            ASSERT_TRUE(loadContentDatabase(nullptr));
    }
};

TEST(SceneTypes, Parse2DAnd3D)
{
    if (!contentHasArchetype("core:cube"))
        ASSERT_TRUE(loadContentDatabase(nullptr));
    EXPECT_NE(findArchetype("core:cube"), nullptr);
    EXPECT_NE(findArchetype("enemy"), nullptr);
    EXPECT_EQ(findArchetype("enemy")->name, "core:hunter");
    EXPECT_EQ(findArchetype("gltf")->name, "core:model");
    EXPECT_EQ(findArchetype("sun")->name, "core:directional_light");
    EXPECT_EQ(findArchetype("player_spawn")->name, "core:spawn");
    const Archetype* platform = findArchetype("core:platform");
    ASSERT_NE(platform, nullptr);
    EXPECT_EQ(platform->editor.modes.size(), 1u);
    EXPECT_EQ(platform->editor.modes[0], "2d");
    const Archetype* cube = findArchetype("core:cube");
    ASSERT_NE(cube, nullptr);
    EXPECT_TRUE(archetypeVisibleInMode(*cube, "3d"));
    EXPECT_FALSE(archetypeVisibleInMode(*platform, "3d"));

    SceneMode mode{};
    EXPECT_TRUE(tryParseSceneMode("2d", mode));
    EXPECT_EQ(mode, SceneMode::Scene2D);
    EXPECT_TRUE(tryParseSceneMode("3d", mode));
    EXPECT_EQ(mode, SceneMode::Scene3D);
}

TEST_F(SceneFile, RoundTrip2D)
{
    SceneFileData in{};
    in.version  = 1;
    in.name     = "ut_level2d";
    in.mode     = SceneMode::Scene2D;
    in.worldMin = Vector2f(0.0f, -1.0f);
    in.worldMax = Vector2f(40.0f, 12.0f);

    SceneObjectData plat{};
    plat.archetype     = "core:platform";
    plat.position = Vector3f(10.0f, 1.0f, 0.0f);
    plat.scale    = Vector3f(8.0f, 2.0f, 1.0f);
    plat.color[0] = 0.5f;
    plat.color[1] = 0.4f;
    plat.color[2] = 0.3f;
    plat.color[3] = 1.0f;
    in.objects.push_back(plat);

    SceneObjectData coin{};
    coin.archetype     = "core:coin";
    coin.position = Vector3f(4.0f, 3.0f, 0.0f);
    coin.scale    = Vector3f(0.5f, 0.5f, 1.0f);
    in.objects.push_back(coin);

    const auto path = tempScenePath("darkengine6_scene2d_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.mode, SceneMode::Scene2D);
    EXPECT_EQ(out.name, "ut_level2d");
    EXPECT_NEAR(out.worldMax.x, 40.0f, 1.0e-4f);
    ASSERT_EQ(out.objects.size(), 2u);
    EXPECT_EQ(out.objects[0].archetype, "core:platform");
    EXPECT_NEAR(out.objects[0].position.x, 10.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].scale.x, 8.0f, 1.0e-4f);
    EXPECT_EQ(out.objects[1].archetype, "core:coin");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, MissingModeDefaultsTo3D)
{
    const auto path = tempScenePath("darkengine6_scene3d_legacy_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({"version":1,"name":"legacy","objects":[{"archetype":"core:cube","position":[1,2,3]}]})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.mode, SceneMode::Scene3D);
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_EQ(data.objects[0].archetype, "core:cube");
    EXPECT_NEAR(data.objects[0].position.y, 2.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, DefaultScenePathUsesScenesFolder)
{
    const auto p = defaultScenePath("level2d.json");
    EXPECT_EQ(p.filename(), "level2d.json");
    EXPECT_EQ(p.parent_path().filename(), "scenes");
}

TEST_F(SceneFile, DefaultScenePathAvoidsExeContentCopy)
{
    namespace fs = std::filesystem;
    const fs::path authoring = authoringContentRoot();
    const fs::path exe       = executableDirectory();
    if (authoring.empty() || exe.empty())
        GTEST_SKIP() << "no content root or executable directory";

    bool sourceOutsideExe = false;
    for (const fs::path& root : contentRootCandidates())
    {
        std::error_code ec;
        if (!fs::is_directory(root, ec) || ec)
            continue;
        const fs::path rel = weaklyCanonical(root).lexically_relative(weaklyCanonical(exe));
        bool           inside = !rel.empty();
        for (const fs::path& part : rel)
        {
            if (part == "..")
            {
                inside = false;
                break;
            }
        }
        if (!inside)
            sourceOutsideExe = true;
    }
    if (!sourceOutsideExe)
        GTEST_SKIP() << "no source content directory outside the executable";

    const fs::path scene = defaultScenePath("level.json");
    const fs::path rel   = weaklyCanonical(scene).lexically_relative(weaklyCanonical(exe));
    bool           underExe = !rel.empty();
    for (const fs::path& part : rel)
    {
        if (part == "..")
        {
            underExe = false;
            break;
        }
    }
    EXPECT_FALSE(underExe);
    EXPECT_EQ(weaklyCanonical(scene).parent_path().parent_path(), weaklyCanonical(authoring));
}

TEST_F(SceneFile, V1RoundTripStillLoads)
{
    SceneFileData in{};
    in.version = 1;
    in.name    = "ut_v1";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData cube{};
    cube.archetype     = "core:cube";
    cube.position = Vector3f(1.0f, 2.0f, 3.0f);
    cube.color[0] = 0.2f;
    cube.color[1] = 0.3f;
    cube.color[2] = 0.4f;
    cube.color[3] = 1.0f;
    in.objects.push_back(cube);

    const auto path = tempScenePath("darkengine6_scene_v1_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 1);
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:cube");
    EXPECT_FALSE(sceneHasLight(out.objects[0]));
    EXPECT_NEAR(sceneEmissive(out.objects[0]), 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.objects[0].position.z, 3.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, V2LocalLightRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_v2_lights";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData point{};
    point.archetype = "core:point_light";
    point.position  = Vector3f(4.0f, 1.5f, -2.0f);
    point.color[0]  = 1.0f;
    point.color[1]  = 0.92f;
    point.color[2]  = 0.75f;
    point.color[3]  = 1.0f;
    SceneLightFields pointLight;
    pointLight.kind         = "point";
    pointLight.intensity    = 600.0f;
    pointLight.range        = 8.0f;
    pointLight.innerDeg     = 12.0f;
    pointLight.outerDeg     = 25.0f;
    pointLight.sourceRadius = 0.05f;
    pointLight.enabled      = true;
    sceneWriteLight(point, pointLight);
    in.objects.push_back(point);

    SceneObjectData spot{};
    spot.archetype = "core:spot_light";
    spot.position  = Vector3f(0.0f, 2.0f, 1.0f);
    spot.rotation  = Quaternion(0.70710678f, 0.0f, 0.70710678f, 0.0f);
    spot.color[0]  = 0.8f;
    spot.color[1]  = 0.9f;
    spot.color[2]  = 1.0f;
    spot.color[3]  = 1.0f;
    SceneLightFields spotLight;
    spotLight.kind         = "spot";
    spotLight.intensity    = 800.0f;
    spotLight.range        = 16.0f;
    spotLight.innerDeg     = 10.0f;
    spotLight.outerDeg     = 22.0f;
    spotLight.sourceRadius = 0.08f;
    spotLight.enabled      = false;
    sceneWriteLight(spot, spotLight);
    in.objects.push_back(spot);

    SceneObjectData glow{};
    glow.archetype = "core:sphere";
    glow.position  = Vector3f(1.0f, 0.5f, 0.0f);
    sceneWriteEmissive(glow, 1.0f);
    in.objects.push_back(glow);
    sceneSetInt(in.objects[0], "emissiveMesh", 2);

    const auto path = tempScenePath("darkengine6_scene_v2_lights_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    ASSERT_EQ(out.objects.size(), 3u);

    EXPECT_EQ(out.objects[0].archetype, "core:point_light");
    EXPECT_TRUE(sceneHasLight(out.objects[0]));
    EXPECT_NEAR(out.objects[0].position.y, 1.5f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "intensity", 0.0f), 600.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "range", 0.0f), 8.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "inner", 0.0f), 12.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "outer", 0.0f), 25.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "sourceRadius", 0.0f), 0.05f, 1.0e-4f);
    EXPECT_TRUE(sceneLightBool(out.objects[0], "enabled", false));
    EXPECT_NEAR(out.objects[0].color[1], 0.92f, 1.0e-4f);
    EXPECT_EQ(sceneEmissiveMeshIndex(out.objects[0]), 2);

    EXPECT_EQ(out.objects[1].archetype, "core:spot_light");
    EXPECT_TRUE(sceneHasLight(out.objects[1]));
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "intensity", 0.0f), 800.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "range", 0.0f), 16.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "inner", 0.0f), 10.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "outer", 0.0f), 22.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "sourceRadius", 0.0f), 0.08f, 1.0e-4f);
    EXPECT_FALSE(sceneLightBool(out.objects[1], "enabled", true));
    EXPECT_NEAR(out.objects[1].rotation.y, 0.70710678f, 1.0e-4f);

    EXPECT_EQ(out.objects[2].archetype, "core:sphere");
    EXPECT_NEAR(sceneEmissive(out.objects[2]), 1.0f, 1.0e-4f);
    EXPECT_FALSE(sceneHasLight(out.objects[2]));
    EXPECT_EQ(sceneEmissiveMeshIndex(out.objects[2]), -1);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, V1FileStillLoadsAndSkipsUnknownKeepsKnownLights)
{
    const auto path = tempScenePath("darkengine6_scene_v1_mixed_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 1,
  "name": "legacy_mix",
  "objects": [
    {"archetype": "core:cube", "position": [1, 2, 3]},
    {"archetype": "not_a_real_type", "position": [0, 0, 0]},
    {"archetype": "core:point_light", "position": [4, 1.5, -2], "color": [1, 0.92, 0.75, 1]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 1);
    ASSERT_EQ(data.objects.size(), 3u);
    EXPECT_EQ(data.objects[0].archetype, "core:cube");
    EXPECT_TRUE(sceneIsPlaceholder(data.objects[1]));
    EXPECT_EQ(data.objects[1].archetype, "core:not_a_real_type");
    EXPECT_NE(data.objects[1].overridesJson.find("not_a_real_type"), std::string::npos);
    EXPECT_EQ(data.objects[2].archetype, "core:point_light");
    EXPECT_TRUE(sceneHasLight(data.objects[2]));
    EXPECT_NEAR(sceneLightFloat(data.objects[2], "intensity", 0.0f), 1885.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[2].position.x, 4.0f, 1.0e-4f);

    ASSERT_TRUE(saveSceneToJson(path, data, &err)) << err;
    SceneFileData again{};
    ASSERT_TRUE(loadSceneFromJson(path, again, &err)) << err;
    ASSERT_EQ(again.objects.size(), 3u);
    EXPECT_TRUE(sceneIsPlaceholder(again.objects[1]));
    EXPECT_NE(again.objects[1].overridesJson.find("not_a_real_type"), std::string::npos);
    EXPECT_EQ(again.objects[2].archetype, "core:point_light");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, MinimalSpotLightGetsSpotDefaults)
{
    const auto path = tempScenePath("darkengine6_scene_spot_min_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "spot_min",
  "objects": [
    {"archetype": "core:spot_light", "position": [0, 2, 0], "color": [1, 0.92, 0.75, 1]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_EQ(data.objects[0].archetype, "core:spot_light");
    EXPECT_TRUE(sceneHasLight(data.objects[0]));
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "intensity", 0.0f), 2513.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "range", 0.0f), 16.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "inner", 0.0f), 12.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "outer", 0.0f), 25.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, UnauthoredGlobalLightsSplitIntensity)
{
    const auto path = tempScenePath("darkengine6_scene_global_min_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "global_min",
  "objects": [
    {"archetype": "core:directional_light", "position": [4, 8, -4]},
    {"archetype": "core:ambient_light", "position": [0, 0, 0]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_EQ(data.objects[0].archetype, "core:directional_light");
    EXPECT_TRUE(sceneHasLight(data.objects[0]));
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "intensity", 0.0f), Pi, 1.0e-5f);
    EXPECT_EQ(data.objects[1].archetype, "core:ambient_light");
    EXPECT_TRUE(sceneHasLight(data.objects[1]));
    EXPECT_NEAR(sceneLightFloat(data.objects[1], "intensity", 0.0f), 1.0f, 1.0e-5f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, IblRoundTripRadians)
{
    SceneFileData in{};
    in.version           = 2;
    in.name              = "ut_ibl";
    in.mode              = SceneMode::Scene3D;
    in.environment       = "env/studio_gradient.hdr";
    in.iblIntensity      = 1.25f;
    in.iblRotationRadY   = HalfPi; // 90 degrees stored as radians, not 90

    const auto path = tempScenePath("darkengine6_scene_ibl_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    EXPECT_EQ(out.environment, "env/studio_gradient.hdr");
    EXPECT_NEAR(out.iblIntensity, 1.25f, 1.0e-5f);
    EXPECT_NEAR(out.iblRotationRadY, HalfPi, 1.0e-5f);
    EXPECT_GT(std::fabs(out.iblRotationRadY - 90.0f), 80.0f); // must not store degrees

    std::ifstream inFile(path);
    ASSERT_TRUE(static_cast<bool>(inFile));
    const std::string text((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("iblRotationRadY"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, IblEmptyEnvironmentMeansOff)
{
    SceneFileData in{};
    in.version     = 2;
    in.mode        = SceneMode::Scene3D;
    in.environment = "";

    const auto path = tempScenePath("darkengine6_scene_ibl_off_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_TRUE(out.environment.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, IblMissingKeysDefaultPath)
{
    const auto path = tempScenePath("darkengine6_scene_ibl_missing_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({"version":2,"name":"no_ibl","mode":"3d","objects":[]})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.environment, "env/studio_gradient.hdr");
    EXPECT_NEAR(data.iblIntensity, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.iblRotationRadY, 0.0f, 1.0e-5f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, Ibl2DIgnoresKeys)
{
    SceneFileData in{};
    in.version           = 2;
    in.name              = "ut_2d_ibl";
    in.mode              = SceneMode::Scene2D;
    in.environment       = "env/should_not_save.hdr";
    in.iblIntensity      = 3.0f;
    in.iblRotationRadY   = HalfPi;

    const auto path = tempScenePath("darkengine6_scene_ibl_2d_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::ifstream inFile(path);
    ASSERT_TRUE(static_cast<bool>(inFile));
    const std::string text((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("environment"), std::string::npos);
    EXPECT_EQ(text.find("iblIntensity"), std::string::npos);
    EXPECT_EQ(text.find("iblRotationRadY"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.mode, SceneMode::Scene2D);
    EXPECT_EQ(out.environment, "env/studio_gradient.hdr");

    {
        std::ofstream injected(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(injected));
        injected << R"({"version":2,"name":"ut_2d_ibl","mode":"2d","environment":"env/ignored.hdr","iblIntensity":4.0,"iblRotationRadY":1.5,"world":{"min":[0,0],"max":[8,8]},"objects":[]})";
    }
    SceneFileData ignored{};
    ASSERT_TRUE(loadSceneFromJson(path, ignored, &err)) << err;
    EXPECT_EQ(ignored.mode, SceneMode::Scene2D);
    EXPECT_EQ(ignored.environment, "env/studio_gradient.hdr");
    EXPECT_NEAR(ignored.iblIntensity, 1.0f, 1.0e-5f);
    EXPECT_NEAR(ignored.iblRotationRadY, 0.0f, 1.0e-5f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, TerrainRoundTrip)
{
    SceneFileData in{};
    in.version           = 2;
    in.name              = "ut_terrain";
    in.mode              = SceneMode::Scene3D;
    in.hasTerrain        = true;
    in.terrain.bindLayout     = TerrainSceneDesc::kBindLayoutV1;
    in.terrain.chunkCells     = 16;
    in.terrain.heightBlendK   = 0.5f;
    in.terrain.heightBlendT   = 0.1f;
    in.terrain.triplanarSlope = 0.45f;
    in.terrain.heightFile     = "ut_terrain.height.bin";
    in.terrain.splatFile      = "ut_terrain.splat.png";
    in.terrain.layerCount     = 4;
    in.terrain.layers[0].albedo = "terrain/dirt/albedo.png";
    in.terrain.layers[0].normal = "terrain/dirt/normal.png";
    in.terrain.layers[0].orm    = "terrain/dirt/orm.png";
    in.terrain.layers[0].tiling = 24.0f;
    in.terrain.layers[0].tint[0] = 1.0f;
    in.terrain.layers[0].tint[1] = 0.9f;
    in.terrain.layers[0].tint[2] = 0.8f;
    in.terrain.layers[0].tint[3] = 1.0f;
    in.terrain.layers[1].albedo = "terrain/grass/albedo.png";
    in.terrain.layers[1].tiling = 20.0f;

    const auto path = tempScenePath("darkengine6_scene_terrain_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::ifstream inFile(path);
    ASSERT_TRUE(static_cast<bool>(inFile));
    const std::string text((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"terrain\""), std::string::npos);
    EXPECT_NE(text.find("bindLayout"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    EXPECT_TRUE(out.hasTerrain);
    EXPECT_EQ(out.terrain.bindLayout, TerrainSceneDesc::kBindLayoutV1);
    EXPECT_EQ(out.terrain.chunkCells, 16);
    EXPECT_NEAR(out.terrain.heightBlendK, 0.5f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.heightBlendT, 0.1f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.triplanarSlope, 0.45f, 1.0e-5f);
    EXPECT_EQ(out.terrain.heightFile, "ut_terrain.height.bin");
    EXPECT_EQ(out.terrain.splatFile, "ut_terrain.splat.png");
    EXPECT_EQ(out.terrain.layers[0].albedo, "terrain/dirt/albedo.png");
    EXPECT_EQ(out.terrain.layers[0].normal, "terrain/dirt/normal.png");
    EXPECT_EQ(out.terrain.layers[0].orm, "terrain/dirt/orm.png");
    EXPECT_NEAR(out.terrain.layers[0].tiling, 24.0f, 1.0e-4f);
    EXPECT_NEAR(out.terrain.layers[0].tint[1], 0.9f, 1.0e-4f);
    EXPECT_EQ(out.terrain.layers[1].albedo, "terrain/grass/albedo.png");
    EXPECT_NE(text.find("\"grass\""), std::string::npos);
    EXPECT_EQ(text.find("\"heightMetres\""), std::string::npos);
    expectGrassDefaults(out.terrain.grass);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, TerrainUnknownBindLayoutSkipped)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_layout_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({"version":2,"name":"bad_layout","mode":"3d","terrain":{"bindLayout":99,"heightFile":"x.height.bin","layers":[]},"objects":[]})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_FALSE(data.hasTerrain);
    EXPECT_TRUE(data.objects.empty());

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, Terrain2DIgnores)
{
    SceneFileData in{};
    in.version    = 2;
    in.name       = "ut_2d_terrain";
    in.mode       = SceneMode::Scene2D;
    in.hasTerrain = true;
    in.terrain.bindLayout = TerrainSceneDesc::kBindLayoutV1;
    in.terrain.heightFile = "should_not_save.height.bin";
    in.terrain.heightBlendK = 0.75f;

    const auto path = tempScenePath("darkengine6_scene_terrain_2d_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::ifstream inFile(path);
    ASSERT_TRUE(static_cast<bool>(inFile));
    const std::string text((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("\"terrain\""), std::string::npos);
    EXPECT_EQ(text.find("heightBlendK"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.mode, SceneMode::Scene2D);
    EXPECT_FALSE(out.hasTerrain);

    {
        std::ofstream injected(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(injected));
        injected << R"({"version":2,"name":"ut_2d_terrain","mode":"2d","terrain":{"bindLayout":1,"heightBlendK":0.9,"heightFile":"ignored.height.bin"},"world":{"min":[0,0],"max":[8,8]},"objects":[]})";
    }
    SceneFileData ignored{};
    ASSERT_TRUE(loadSceneFromJson(path, ignored, &err)) << err;
    EXPECT_EQ(ignored.mode, SceneMode::Scene2D);
    EXPECT_FALSE(ignored.hasTerrain);
    EXPECT_NEAR(ignored.terrain.heightBlendK, 0.5f, 1.0e-5f);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, PlayerAndHunterRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_pawns";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData player{};
    player.archetype     = "core:player";
    player.position = Vector3f(2.0f, 0.5f, -4.0f);
    player.color[0] = 0.35f;
    player.color[1] = 0.85f;
    player.color[2] = 0.72f;
    player.color[3] = 1.0f;
    in.objects.push_back(player);

    SceneObjectData hunter{};
    hunter.archetype     = "core:hunter";
    hunter.position = Vector3f(8.0f, 1.0f, 3.0f);
    hunter.color[0] = 0.85f;
    hunter.color[1] = 0.28f;
    hunter.color[2] = 0.22f;
    hunter.color[3] = 1.0f;
    in.objects.push_back(hunter);

    const auto path = tempScenePath("darkengine6_scene_pawns_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 2u);
    EXPECT_EQ(out.objects[0].archetype, "core:player");
    EXPECT_NEAR(out.objects[0].position.x, 2.0f, 1.0e-4f);
    EXPECT_EQ(out.objects[1].archetype, "core:hunter");
    EXPECT_NEAR(out.objects[1].position.z, 3.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, WolfRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_wolf";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData wolf{};
    wolf.archetype     = "core:wolf";
    wolf.position = Vector3f(-6.0f, 0.5f, 12.0f);
    wolf.color[0] = 0.52f;
    wolf.color[1] = 0.46f;
    wolf.color[2] = 0.40f;
    wolf.color[3] = 1.0f;
    in.objects.push_back(wolf);

    const auto path = tempScenePath("darkengine6_scene_wolf_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:wolf");
    EXPECT_NEAR(out.objects[0].position.x, -6.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.z, 12.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, ModelPathRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_model";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData model{};
    model.archetype = "core:model";
    model.position  = Vector3f(4.0f, 0.5f, -2.0f);
    sceneWriteModelPath(model, "models/wolf.gltf");
    in.objects.push_back(model);

    const auto path = tempScenePath("darkengine6_scene_model_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:model");
    EXPECT_EQ(sceneModelPath(out.objects[0]), "models/wolf.gltf");
    EXPECT_NEAR(out.objects[0].position.x, 4.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, TerrainGrid_RoundTrip)
{
    SceneFileData in{};
    in.version    = 2;
    in.name       = "ut_terrain_grid";
    in.mode       = SceneMode::Scene3D;
    in.hasTerrain = true;
    in.terrain.bindLayout     = TerrainSceneDesc::kBindLayoutV1;
    in.terrain.chunkCells     = 64;
    in.terrain.heightBlendK   = 0.5f;
    in.terrain.heightBlendT   = 0.1f;
    in.terrain.triplanarSlope = 0.45f;
    in.terrain.heightFile     = "should_omit.height.bin";
    in.terrain.splatFile      = "should_omit.splat.png";
    in.terrain.hasGrid        = true;
    in.terrain.grid.tilesX       = 4;
    in.terrain.grid.tilesZ       = 4;
    in.terrain.grid.tileCells    = 512;
    in.terrain.grid.cellSize     = 1.0f;
    in.terrain.grid.origin       = Vector3f(-1024.0f, 0.0f, -1024.0f);
    in.terrain.grid.heightScale  = 80.0f;
    in.terrain.grid.seed         = 1337u;
    in.terrain.grid.coarseFile   = "ut.coarse.height.bin";
    in.terrain.grid.tileDir      = "ut.tiles";
    in.terrain.grid.seaLevel     = 12.0f;
    in.terrain.grid.residentRing = 5;
    in.terrain.layerCount        = 1;
    in.terrain.layers[0].albedo  = "terrain/dirt/albedo.png";

    const auto path = tempScenePath("darkengine6_scene_terrain_grid_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::string text;
    {
        std::ifstream inFile(path);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"grid\""), std::string::npos);
    EXPECT_NE(text.find("\"tilesX\""), std::string::npos);
    EXPECT_NE(text.find("\"coarseFile\""), std::string::npos);
    EXPECT_NE(text.find("\"tileDir\""), std::string::npos);
    EXPECT_EQ(text.find("\"heightFile\""), std::string::npos);
    EXPECT_EQ(text.find("\"splatFile\""), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    EXPECT_TRUE(out.hasTerrain);
    EXPECT_TRUE(out.terrain.hasGrid);
    EXPECT_EQ(out.terrain.grid.tilesX, 4u);
    EXPECT_EQ(out.terrain.grid.tilesZ, 4u);
    EXPECT_EQ(out.terrain.grid.tileCells, 512u);
    EXPECT_NEAR(out.terrain.grid.cellSize, 1.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grid.origin.x, -1024.0f, 1.0e-4f);
    EXPECT_NEAR(out.terrain.grid.origin.y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(out.terrain.grid.origin.z, -1024.0f, 1.0e-4f);
    EXPECT_NEAR(out.terrain.grid.heightScale, 80.0f, 1.0e-4f);
    EXPECT_EQ(out.terrain.grid.seed, 1337u);
    EXPECT_EQ(out.terrain.grid.coarseFile, "ut.coarse.height.bin");
    EXPECT_EQ(out.terrain.grid.tileDir, "ut.tiles");
    EXPECT_NEAR(out.terrain.grid.seaLevel, 12.0f, 1.0e-4f);
    EXPECT_EQ(out.terrain.grid.residentRing, 5);
    EXPECT_TRUE(out.terrain.heightFile.empty());
    EXPECT_TRUE(out.terrain.splatFile.empty());
    EXPECT_EQ(out.terrain.layers[0].albedo, "terrain/dirt/albedo.png");

    SceneFileData twoD = in;
    twoD.mode = SceneMode::Scene2D;
    const auto path2d = tempScenePath("darkengine6_scene_terrain_grid_2d_ut.json");
    ASSERT_TRUE(saveSceneToJson(path2d, twoD, &err)) << err;
    std::string text2d;
    {
        std::ifstream in2d(path2d);
        ASSERT_TRUE(static_cast<bool>(in2d));
        text2d.assign((std::istreambuf_iterator<char>(in2d)), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(text2d.find("\"terrain\""), std::string::npos);
    EXPECT_EQ(text2d.find("\"grid\""), std::string::npos);

    SceneFileData eight{};
    eight.version    = 2;
    eight.mode       = SceneMode::Scene3D;
    eight.hasTerrain = true;
    eight.terrain.bindLayout = TerrainSceneDesc::kBindLayoutV1;
    eight.terrain.hasGrid    = true;
    eight.terrain.grid.tilesX = 8;
    eight.terrain.grid.tilesZ = 8;
    eight.terrain.grid.coarseFile = "eight.coarse.height.bin";
    const auto path8 = tempScenePath("darkengine6_scene_terrain_grid_8_ut.json");
    ASSERT_TRUE(saveSceneToJson(path8, eight, &err)) << err;
    SceneFileData out8{};
    ASSERT_TRUE(loadSceneFromJson(path8, out8, &err)) << err;
    EXPECT_TRUE(out8.terrain.hasGrid);
    EXPECT_EQ(out8.terrain.grid.tilesX, 8u);
    EXPECT_EQ(out8.terrain.grid.tilesZ, 8u);
    EXPECT_TRUE(out8.terrain.heightFile.empty());
    EXPECT_TRUE(out8.terrain.splatFile.empty());

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(path2d, removeEc);
    std::filesystem::remove(path8, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_RoundTrip)
{
    SceneFileData in{};
    in.version    = 2;
    in.name       = "ut_terrain_foliage";
    in.mode       = SceneMode::Scene3D;
    in.hasTerrain = true;
    in.terrain.bindLayout        = TerrainSceneDesc::kBindLayoutV1;
    in.terrain.hasGrid           = true;
    in.terrain.grid.tilesX       = 4;
    in.terrain.grid.tilesZ       = 2;
    in.terrain.grid.tileCells    = 128;
    in.terrain.grid.cellSize     = 2.0f;
    in.terrain.grid.origin       = Vector3f(-64.0f, 1.0f, 32.0f);
    in.terrain.grid.heightScale  = 40.0f;
    in.terrain.grid.seed         = 9u;
    in.terrain.grid.coarseFile   = "fol.coarse.height.bin";
    in.terrain.grid.tileDir      = "fol.tiles";
    in.terrain.grid.seaLevel     = 18.0f;
    in.terrain.grid.residentRing = 3;
    in.terrain.foliage.present            = true;
    in.terrain.foliage.seed               = 4242u;
    in.terrain.foliage.dirtTreesPerM2     = 0.25f;
    in.terrain.foliage.dirtFlowersPerM2   = 1.5f;
    in.terrain.foliage.grassTreesPerM2    = 0.5f;
    in.terrain.foliage.grassFlowersPerM2  = 1.25f;
    in.terrain.foliage.rockPerM2          = 0.125f;
    in.terrain.foliage.grassPerM2         = 0.4f;
    in.terrain.foliage.treeMinNormalY     = 0.91f;
    in.terrain.foliage.flowerMinNormalY   = 0.75f;
    in.terrain.foliage.tuftMinNormalY     = 0.66f;
    in.terrain.foliage.rockMinNormalY     = 0.33f;
    in.terrain.foliage.treeMaxLeanDeg     = 12.0f;
    in.terrain.foliage.rockMaxPitchDeg    = 44.0f;
    in.terrain.foliage.treeSpacingM       = 5.5f;
    in.terrain.foliage.rockSpacingM       = 4.0f;
    in.terrain.foliage.patchStrength      = 0.35f;
    in.terrain.foliage.patchLargeM        = 100.0f;
    in.terrain.foliage.patchMediumM       = 18.0f;
    in.terrain.foliage.patchFineM         = 3.0f;
    in.terrain.foliage.stale              = true;
    in.terrain.foliage.treeModel          = "models/tree.gltf";
    in.terrain.foliage.flowerModel        = "models/flower.gltf";
    in.terrain.foliage.rockModel          = "models/rock.gltf";
    in.terrain.foliage.grassModel         = "models/Grass/grass_medium_01_2k.gltf";
    in.terrain.grass.enabled              = true;
    in.terrain.grass.heightMetres         = 0.80f;
    in.terrain.grass.flexibility          = 0.25f;
    in.terrain.grass.densityScale         = 1.25f;
    in.terrain.grass.windBaseYaw          = -1.20f;
    in.terrain.grass.windDeflection       = 0.30f;
    in.terrain.grass.windTipMetres        = 0.15f;
    in.terrain.grass.windSpatialFreq      = 0.008f;
    in.terrain.grass.windTemporalFreq     = 0.040f;
    in.terrain.grass.seed                 = 0xA11F5Eu;

    const auto path = tempScenePath("darkengine6_scene_terrain_foliage_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::string text;
    {
        std::ifstream inFile(path);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"foliage\""), std::string::npos);
    EXPECT_NE(text.find("\"stale\": true"), std::string::npos);
    EXPECT_NE(text.find("\"treeModel\": \"models/tree.gltf\""), std::string::npos);
    EXPECT_NE(text.find("\"flowerModel\": \"models/flower.gltf\""), std::string::npos);
    EXPECT_NE(text.find("\"rockModel\": \"models/rock.gltf\""), std::string::npos);
    EXPECT_NE(text.find("\"grassModel\": \"models/Grass/grass_medium_01_2k.gltf\""), std::string::npos);
    EXPECT_NE(text.find("\"treeMinNormalY\""), std::string::npos);
    EXPECT_NE(text.find("\"flowerMinNormalY\""), std::string::npos);
    EXPECT_NE(text.find("\"tuftMinNormalY\""), std::string::npos);
    EXPECT_NE(text.find("\"rockMinNormalY\""), std::string::npos);
    EXPECT_NE(text.find("\"treeMaxLeanDeg\""), std::string::npos);
    EXPECT_NE(text.find("\"rockMaxPitchDeg\""), std::string::npos);
    EXPECT_NE(text.find("\"treeSpacingM\""), std::string::npos);
    EXPECT_NE(text.find("\"rockSpacingM\""), std::string::npos);
    EXPECT_NE(text.find("\"patchStrength\""), std::string::npos);
    EXPECT_NE(text.find("\"patchLargeM\""), std::string::npos);
    EXPECT_NE(text.find("\"patchMediumM\""), std::string::npos);
    EXPECT_NE(text.find("\"patchFineM\""), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    EXPECT_TRUE(out.hasTerrain);
    EXPECT_TRUE(out.terrain.hasGrid);
    EXPECT_EQ(out.terrain.grid.tilesX, 4u);
    EXPECT_EQ(out.terrain.grid.tilesZ, 2u);
    EXPECT_EQ(out.terrain.grid.tileCells, 128u);
    EXPECT_NEAR(out.terrain.grid.cellSize, 2.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grid.seaLevel, 18.0f, 1.0e-4f);
    EXPECT_EQ(out.terrain.grid.seed, 9u);
    EXPECT_TRUE(out.terrain.foliage.present);
    EXPECT_EQ(out.terrain.foliage.seed, 4242u);
    EXPECT_NEAR(out.terrain.foliage.dirtTreesPerM2, 0.25f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.dirtFlowersPerM2, 1.5f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.grassTreesPerM2, 0.5f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.grassFlowersPerM2, 1.25f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.rockPerM2, 0.125f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.grassPerM2, 0.4f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.treeMinNormalY, 0.91f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.flowerMinNormalY, 0.75f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.tuftMinNormalY, 0.66f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.rockMinNormalY, 0.33f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.treeMaxLeanDeg, 12.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.rockMaxPitchDeg, 44.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.treeSpacingM, 5.5f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.rockSpacingM, 4.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.patchStrength, 0.35f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.patchLargeM, 100.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.patchMediumM, 18.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.foliage.patchFineM, 3.0f, 1.0e-5f);
    EXPECT_TRUE(out.terrain.foliage.stale);
    EXPECT_EQ(out.terrain.foliage.treeModel, "models/tree.gltf");
    EXPECT_EQ(out.terrain.foliage.flowerModel, "models/flower.gltf");
    EXPECT_EQ(out.terrain.foliage.rockModel, "models/rock.gltf");
    EXPECT_EQ(out.terrain.foliage.grassModel, "models/Grass/grass_medium_01_2k.gltf");
    ASSERT_EQ(out.terrain.foliage.trees.size(), in.terrain.foliage.trees.size());
    EXPECT_EQ(out.terrain.foliage.trees[0].path, in.terrain.foliage.trees[0].path);
    EXPECT_TRUE(out.terrain.grass.enabled);
    EXPECT_NEAR(out.terrain.grass.heightMetres, 0.80f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.flexibility, 0.25f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.densityScale, 1.25f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windBaseYaw, -1.20f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windDeflection, 0.30f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windTipMetres, 0.15f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windSpatialFreq, 0.008f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windTemporalFreq, 0.040f, 1.0e-5f);
    EXPECT_EQ(out.terrain.grass.seed, 0xA11F5Eu);
    EXPECT_NE(text.find("\"height\":"), std::string::npos);
    EXPECT_EQ(text.find("\"heightMetres\""), std::string::npos);

    SceneFileData twoD = in;
    twoD.mode = SceneMode::Scene2D;
    const auto path2d = tempScenePath("darkengine6_scene_terrain_foliage_2d_ut.json");
    ASSERT_TRUE(saveSceneToJson(path2d, twoD, &err)) << err;
    std::string text2d;
    {
        std::ifstream in2d(path2d);
        ASSERT_TRUE(static_cast<bool>(in2d));
        text2d.assign((std::istreambuf_iterator<char>(in2d)), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(text2d.find("\"terrain\""), std::string::npos);
    EXPECT_EQ(text2d.find("\"foliage\""), std::string::npos);
    EXPECT_EQ(text2d.find("\"grass\""), std::string::npos);
    EXPECT_NE(text2d.find("\"version\": 2"), std::string::npos);

    const auto omitted = tempScenePath("darkengine6_scene_terrain_foliage_slope_omitted_ut.json");
    {
        std::ofstream file(omitted, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "slope_omitted",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": { "version": 1, "seed": 7 },
    "layers": []
  },
  "objects": []
})";
    }
    SceneFileData omittedOut{};
    ASSERT_TRUE(loadSceneFromJson(omitted, omittedOut, &err)) << err;
    EXPECT_EQ(omittedOut.version, 2);
    EXPECT_TRUE(omittedOut.terrain.foliage.present);
    EXPECT_NEAR(omittedOut.terrain.foliage.treeMinNormalY, 0.87f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.flowerMinNormalY, 0.82f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.tuftMinNormalY, 0.82f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.rockMinNormalY, 0.40f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.treeMaxLeanDeg, 7.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.rockMaxPitchDeg, 30.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.treeSpacingM, 3.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.rockSpacingM, 0.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.patchStrength, 0.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.patchLargeM, 80.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.patchMediumM, 22.0f, 1.0e-5f);
    EXPECT_NEAR(omittedOut.terrain.foliage.patchFineM, 4.0f, 1.0e-5f);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(path2d, removeEc);
    std::filesystem::remove(omitted, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_SpeciesListRoundTrip)
{
    const auto path = tempScenePath("darkengine6_scene_foliage_species_ut.json");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "species",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": {
      "version": 1,
      "trees": [
        { "path": "models/a.gltf", "weight": 3.0, "scaleMin": 0.7, "scaleMax": 1.6, "worldScale": false },
        { "path": "models/b.gltf", "weight": 1.0, "scaleMin": 0.9, "scaleMax": 1.1, "worldScale": true }
      ]
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    ASSERT_EQ(data.terrain.foliage.trees.size(), 2u);
    EXPECT_EQ(data.terrain.foliage.trees[0].path, "models/a.gltf");
    EXPECT_NEAR(data.terrain.foliage.trees[0].weight, 3.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.trees[0].scaleMin, 0.7f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.trees[0].scaleMax, 1.6f, 1.0e-5f);
    EXPECT_FALSE(data.terrain.foliage.trees[0].worldScale);
    EXPECT_EQ(data.terrain.foliage.trees[1].path, "models/b.gltf");
    EXPECT_NEAR(data.terrain.foliage.trees[1].weight, 1.0f, 1.0e-5f);
    EXPECT_TRUE(data.terrain.foliage.trees[1].worldScale);

    const auto saved = tempScenePath("darkengine6_scene_foliage_species_saved_ut.json");
    ASSERT_TRUE(saveSceneToJson(saved, data, &err)) << err;
    std::string text;
    {
        std::ifstream inFile(saved);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);

    SceneFileData again{};
    ASSERT_TRUE(loadSceneFromJson(saved, again, &err)) << err;
    ASSERT_EQ(again.terrain.foliage.trees.size(), 2u);
    EXPECT_EQ(again.terrain.foliage.trees[0].path, "models/a.gltf");
    EXPECT_NEAR(again.terrain.foliage.trees[0].weight, 3.0f, 1.0e-5f);
    EXPECT_EQ(again.terrain.foliage.trees[1].path, "models/b.gltf");
    EXPECT_NEAR(again.terrain.foliage.trees[1].weight, 1.0f, 1.0e-5f);
    EXPECT_TRUE(again.terrain.foliage.trees[1].worldScale);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(saved, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_LegacyTreeModelOneEntry)
{
    const auto path = tempScenePath("darkengine6_scene_foliage_legacy_model_ut.json");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "legacy_tree",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": { "version": 1, "treeModel": "models/oak.gltf" },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_EQ(data.terrain.foliage.treeModel, "models/oak.gltf");
    ASSERT_EQ(data.terrain.foliage.trees.size(), 1u);
    EXPECT_EQ(data.terrain.foliage.trees[0].path, "models/oak.gltf");
    EXPECT_NEAR(data.terrain.foliage.trees[0].weight, 1.0f, 1.0e-5f);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_MissingKey_Defaults)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_foliage_missing_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "nofol",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "grid": {
      "tilesX": 3,
      "tilesZ": 2,
      "tileCells": 128,
      "cellSize": 2.0,
      "seaLevel": 7.5,
      "seed": 99,
      "coarseFile": "nofol.coarse.height.bin",
      "tileDir": "nofol.tiles",
      "residentRing": 3
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_TRUE(data.hasTerrain);
    EXPECT_TRUE(data.terrain.hasGrid);
    EXPECT_EQ(data.terrain.grid.tilesX, 3u);
    EXPECT_EQ(data.terrain.grid.tilesZ, 2u);
    EXPECT_EQ(data.terrain.grid.tileCells, 128u);
    EXPECT_NEAR(data.terrain.grid.cellSize, 2.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.grid.seaLevel, 7.5f, 1.0e-4f);
    EXPECT_EQ(data.terrain.grid.seed, 99u);
    EXPECT_EQ(data.terrain.grid.coarseFile, "nofol.coarse.height.bin");
    EXPECT_EQ(data.terrain.grid.tileDir, "nofol.tiles");
    EXPECT_EQ(data.terrain.grid.residentRing, 3);

    const Terrain::FoliageDensity defaults{};
    EXPECT_FALSE(data.terrain.foliage.present);
    EXPECT_FALSE(data.terrain.foliage.stale);
    EXPECT_EQ(data.terrain.foliage.seed, defaults.seed);
    EXPECT_NEAR(data.terrain.foliage.dirtTreesPerM2, defaults.dirtTreesPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.dirtFlowersPerM2, defaults.dirtFlowersPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.grassTreesPerM2, defaults.grassTreesPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.grassFlowersPerM2, defaults.grassFlowersPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.rockPerM2, defaults.rockPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.grassPerM2, defaults.grassPerM2, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.treeMinNormalY, 0.87f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.flowerMinNormalY, 0.82f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.tuftMinNormalY, 0.82f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.rockMinNormalY, 0.40f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.treeMaxLeanDeg, 7.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.rockMaxPitchDeg, 30.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.treeSpacingM, 3.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.rockSpacingM, 0.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.treeSpacingM, defaults.treeSpacingM, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.rockSpacingM, defaults.rockSpacingM, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchStrength, 0.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchLargeM, 80.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchMediumM, 22.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchFineM, 4.0f, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchStrength, defaults.patchStrength, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchLargeM, defaults.patchLargeM, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchMediumM, defaults.patchMediumM, 1.0e-6f);
    EXPECT_NEAR(data.terrain.foliage.patchFineM, defaults.patchFineM, 1.0e-6f);
    EXPECT_EQ(data.terrain.foliage.treeModel, defaults.treeModel);
    EXPECT_EQ(data.terrain.foliage.flowerModel, defaults.flowerModel);
    EXPECT_EQ(data.terrain.foliage.rockModel, defaults.rockModel);
    EXPECT_EQ(data.terrain.foliage.grassModel, defaults.grassModel);
    expectGrassDefaults(data.terrain.grass);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_ClampDensities)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_foliage_clamp_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "foliage_clamp",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "grid": {
      "tilesX": 2,
      "tilesZ": 2,
      "seaLevel": 4.0,
      "coarseFile": "clamp.coarse.height.bin"
    },
    "foliage": {
      "version": 1,
      "dirtTreesPerM2": 1e20,
      "dirtFlowersPerM2": -0.5,
      "grassTreesPerM2": 4.0,
      "grassFlowersPerM2": 1e20,
      "rockPerM2": -2.5,
      "grassPerM2": 8.0,
      "treeMinNormalY": 3.0,
      "flowerMinNormalY": -1.0,
      "tuftMinNormalY": 4.0,
      "rockMinNormalY": -0.2,
      "treeMaxLeanDeg": 40.0,
      "rockMaxPitchDeg": 120.0,
      "treeSpacingM": 40.0,
      "rockSpacingM": -3.0,
      "patchStrength": 5.0,
      "patchLargeM": 500.0,
      "patchMediumM": 100.0,
      "patchFineM": 20.0,
      "treeModel": "models/oak.gltf",
      "grassModel": "models/Grass/custom.gltf",
      "notes": "ignored"
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_TRUE(data.hasTerrain);
    EXPECT_TRUE(data.terrain.hasGrid);
    EXPECT_EQ(data.terrain.grid.tilesX, 2u);
    EXPECT_EQ(data.terrain.grid.tilesZ, 2u);
    EXPECT_NEAR(data.terrain.grid.seaLevel, 4.0f, 1.0e-4f);
    EXPECT_EQ(data.terrain.grid.coarseFile, "clamp.coarse.height.bin");
    EXPECT_TRUE(data.terrain.foliage.present);
    EXPECT_EQ(data.terrain.foliage.seed, 1337u);
    EXPECT_FALSE(data.terrain.foliage.stale);
    EXPECT_NEAR(data.terrain.foliage.dirtTreesPerM2, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.dirtFlowersPerM2, 0.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.grassTreesPerM2, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.grassFlowersPerM2, 2.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.rockPerM2, 0.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.grassPerM2, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.treeMinNormalY, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.flowerMinNormalY, 0.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.tuftMinNormalY, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.rockMinNormalY, 0.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.treeMaxLeanDeg, 15.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.rockMaxPitchDeg, 89.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.treeSpacingM, 16.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.rockSpacingM, 0.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.patchStrength, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.patchLargeM, 120.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.patchMediumM, 30.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.foliage.patchFineM, 6.0f, 1.0e-5f);
    EXPECT_EQ(data.terrain.foliage.treeModel, "models/oak.gltf");
    EXPECT_EQ(data.terrain.foliage.grassModel, "models/Grass/custom.gltf");
    const Terrain::FoliageDensity modelDefaults{};
    EXPECT_EQ(data.terrain.foliage.flowerModel, modelDefaults.flowerModel);
    EXPECT_EQ(data.terrain.foliage.rockModel, modelDefaults.rockModel);

    const auto lowPath = tempScenePath("darkengine6_scene_terrain_foliage_clamp_low_ut.json");
    {
        std::ofstream lowFile(lowPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(lowFile));
        lowFile << R"({
  "version": 2,
  "name": "foliage_clamp_low",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": {
      "version": 1,
      "patchStrength": -1.0,
      "patchLargeM": 10.0,
      "patchMediumM": 1.0,
      "patchFineM": 0.5
    },
    "layers": []
  },
  "objects": []
})";
    }
    SceneFileData low{};
    ASSERT_TRUE(loadSceneFromJson(lowPath, low, &err)) << err;
    EXPECT_NEAR(low.terrain.foliage.patchStrength, 0.0f, 1.0e-5f);
    EXPECT_NEAR(low.terrain.foliage.patchLargeM, 60.0f, 1.0e-5f);
    EXPECT_NEAR(low.terrain.foliage.patchMediumM, 15.0f, 1.0e-5f);
    EXPECT_NEAR(low.terrain.foliage.patchFineM, 2.0f, 1.0e-5f);

    std::string text;
    {
        std::ifstream inFile(path);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(lowPath, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_TrailsMissingLoadsEmpty)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_trails_missing_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "notrails",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": { "version": 1 },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_TRUE(data.hasTerrain);
    EXPECT_TRUE(data.terrain.foliage.present);
    EXPECT_FALSE(data.terrain.trails.autoGenerate);
    EXPECT_EQ(data.terrain.trails.maxAutoPaths, 12);
    EXPECT_NEAR(data.terrain.trails.humanWidth, 1.6f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.trails.animalWidth, 0.8f, 1.0e-5f);
    EXPECT_TRUE(data.terrain.trails.paths.empty());

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, TerrainFoliage_TrailsMissingAuthoredAndClamp)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_trails_authored_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "trails",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": { "version": 1 },
    "trails": {
      "autoGenerate": true,
      "maxAutoPaths": 99,
      "humanWidth": 12.0,
      "animalWidth": 0.0,
      "paths": [
        {
          "kind": "human",
          "width": 1.6,
          "points": [[1.0, 2.0], [3.0, 4.0]]
        },
        {
          "kind": "animal",
          "width": 9.0,
          "authored": false,
          "closed": true,
          "points": [[0.0, 0.0], [8.0, 1.0]]
        },
        {
          "kind": "bird",
          "width": 1.0,
          "points": [[0.0, 0.0], [1.0, 1.0]]
        },
        {
          "kind": "human",
          "width": -2.0,
          "authored": true,
          "points": [[4.0, 5.0], [6.0, 7.0]]
        }
      ]
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_TRUE(data.terrain.foliage.present);
    EXPECT_TRUE(data.terrain.trails.autoGenerate);
    EXPECT_EQ(data.terrain.trails.maxAutoPaths, 64);
    EXPECT_NEAR(data.terrain.trails.humanWidth, 8.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.trails.animalWidth, 0.8f, 1.0e-5f);
    ASSERT_EQ(data.terrain.trails.paths.size(), 3u);
    EXPECT_EQ(data.terrain.trails.paths[0].kind, Terrain::TrailKind::Human);
    EXPECT_TRUE(data.terrain.trails.paths[0].authored);
    EXPECT_NEAR(data.terrain.trails.paths[0].width, 1.6f, 1.0e-5f);
    ASSERT_EQ(data.terrain.trails.paths[0].points.size(), 2u);
    EXPECT_NEAR(data.terrain.trails.paths[0].points[0].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(data.terrain.trails.paths[0].points[0].z, 2.0f, 1.0e-5f);
    EXPECT_EQ(data.terrain.trails.paths[1].kind, Terrain::TrailKind::Animal);
    EXPECT_FALSE(data.terrain.trails.paths[1].authored);
    EXPECT_TRUE(data.terrain.trails.paths[1].closed);
    EXPECT_NEAR(data.terrain.trails.paths[1].width, 8.0f, 1.0e-5f);
    EXPECT_EQ(data.terrain.trails.paths[2].kind, Terrain::TrailKind::Human);
    EXPECT_TRUE(data.terrain.trails.paths[2].authored);
    EXPECT_NEAR(data.terrain.trails.paths[2].width, 1.6f, 1.0e-5f);

    const auto negative = tempScenePath("darkengine6_scene_terrain_trails_negative_ut.json");
    {
        std::ofstream out(negative, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "trails_neg",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "trails": { "maxAutoPaths": -4 },
    "layers": []
  },
  "objects": []
})";
    }
    SceneFileData neg{};
    ASSERT_TRUE(loadSceneFromJson(negative, neg, &err)) << err;
    EXPECT_FALSE(neg.terrain.trails.autoGenerate);
    EXPECT_EQ(neg.terrain.trails.maxAutoPaths, 0);
    EXPECT_TRUE(neg.terrain.trails.paths.empty());

    SceneFileData round{};
    round.version = 2;
    round.name = "trail_round";
    round.mode = SceneMode::Scene3D;
    round.hasTerrain = true;
    round.terrain.foliage.present = true;
    round.terrain.trails.autoGenerate = false;
    round.terrain.trails.maxAutoPaths = 4;
    round.terrain.trails.humanWidth = 2.0f;
    round.terrain.trails.animalWidth = 0.5f;
    Terrain::TrailPath human;
    human.kind = Terrain::TrailKind::Human;
    human.width = 2.0f;
    human.authored = true;
    human.points.push_back(Terrain::TrailPoint{ 1.0f, 2.0f });
    human.points.push_back(Terrain::TrailPoint{ 3.0f, 4.0f });
    human.points.push_back(Terrain::TrailPoint{ 5.0f, 6.0f });
    Terrain::TrailPath animal;
    animal.kind = Terrain::TrailKind::Animal;
    animal.width = 0.5f;
    animal.authored = false;
    animal.closed = true;
    animal.points.push_back(Terrain::TrailPoint{ 0.0f, 0.0f });
    animal.points.push_back(Terrain::TrailPoint{ 8.0f, 1.0f });
    animal.points.push_back(Terrain::TrailPoint{ 8.0f, 4.0f });
    round.terrain.trails.paths.push_back(human);
    round.terrain.trails.paths.push_back(animal);

    const auto saved = tempScenePath("darkengine6_scene_terrain_trails_round_ut.json");
    ASSERT_TRUE(saveSceneToJson(saved, round, &err)) << err;
    std::string text;
    {
        std::ifstream inFile(saved);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"trails\""), std::string::npos);
    EXPECT_NE(text.find("\"authored\": false"), std::string::npos);

    SceneFileData loaded{};
    ASSERT_TRUE(loadSceneFromJson(saved, loaded, &err)) << err;
    EXPECT_EQ(loaded.version, 2);
    EXPECT_FALSE(loaded.terrain.trails.autoGenerate);
    EXPECT_EQ(loaded.terrain.trails.maxAutoPaths, 4);
    EXPECT_NEAR(loaded.terrain.trails.humanWidth, 2.0f, 1.0e-5f);
    EXPECT_NEAR(loaded.terrain.trails.animalWidth, 0.5f, 1.0e-5f);
    ASSERT_EQ(loaded.terrain.trails.paths.size(), 2u);
    EXPECT_TRUE(loaded.terrain.trails.paths[0].authored);
    EXPECT_EQ(loaded.terrain.trails.paths[0].kind, Terrain::TrailKind::Human);
    EXPECT_EQ(loaded.terrain.trails.paths[0].points.size(), 3u);
    EXPECT_FALSE(loaded.terrain.trails.paths[1].authored);
    EXPECT_TRUE(loaded.terrain.trails.paths[1].closed);
    EXPECT_EQ(loaded.terrain.trails.paths[1].kind, Terrain::TrailKind::Animal);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(negative, removeEc);
    std::filesystem::remove(saved, removeEc);
}

TEST_F(SceneFile, FoliageShedScale_RoundTripClampAndMissing)
{
    SceneFileData in{};
    in.version                 = 2;
    in.name                    = "ut_shed";
    in.mode                    = SceneMode::Scene3D;
    in.hasTerrain              = true;
    in.terrain.hasGrid         = true;
    in.terrain.grid.tilesX     = 1;
    in.terrain.grid.tilesZ     = 1;
    in.terrain.foliage.present = true;
    in.terrain.foliage.shedScale = 0.25f;

    const auto path = tempScenePath("darkengine6_scene_shed_scale_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_NEAR(out.terrain.foliage.shedScale, 0.25f, 1.0e-5f);
    Terrain::LeafField field;
    field.setShedScale(out.terrain.foliage.shedScale);
    EXPECT_NEAR(field.shedScale(), 0.25f, 1.0e-5f);

    const auto missingPath = tempScenePath("darkengine6_scene_shed_missing_ut.json");
    {
        std::ofstream file(missingPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "shed_missing",
  "mode": "3d",
  "terrain": { "bindLayout": 1, "foliage": { "version": 1, "seed": 7 }, "layers": [] },
  "objects": []
})";
    }
    SceneFileData missing{};
    ASSERT_TRUE(loadSceneFromJson(missingPath, missing, &err)) << err;
    EXPECT_TRUE(missing.terrain.foliage.present);
    EXPECT_NEAR(missing.terrain.foliage.shedScale, 1.0f, 1.0e-5f);

    const auto clampPath = tempScenePath("darkengine6_scene_shed_clamp_ut.json");
    {
        std::ofstream file(clampPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "shed_low",
  "mode": "3d",
  "terrain": { "bindLayout": 1, "foliage": { "shedScale": -2.5 }, "layers": [] },
  "objects": []
})";
    }
    SceneFileData low{};
    ASSERT_TRUE(loadSceneFromJson(clampPath, low, &err)) << err;
    EXPECT_NEAR(low.terrain.foliage.shedScale, 0.0f, 1.0e-5f);
    field.setShedScale(low.terrain.foliage.shedScale);
    Terrain::LeafField::Crown crown;
    crown.id     = 1;
    crown.crownY = 12.0f;
    Terrain::LeafField::Step step;
    step.dt         = 1.0f;
    step.simTime    = 1.0;
    step.crowns     = &crown;
    step.crownCount = 1;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);

    {
        std::ofstream file(clampPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "shed_high",
  "mode": "3d",
  "terrain": { "bindLayout": 1, "foliage": { "shedScale": 4.0 }, "layers": [] },
  "objects": []
})";
    }
    SceneFileData high{};
    ASSERT_TRUE(loadSceneFromJson(clampPath, high, &err)) << err;
    EXPECT_NEAR(high.terrain.foliage.shedScale, 1.0f, 1.0e-5f);

    {
        std::ofstream file(clampPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(file));
        file << R"({
  "version": 2,
  "name": "shed_bad",
  "mode": "3d",
  "terrain": { "bindLayout": 1, "foliage": { "shedScale": "lots" }, "layers": [] },
  "objects": []
})";
    }
    SceneFileData bad{};
    ASSERT_TRUE(loadSceneFromJson(clampPath, bad, &err)) << err;
    EXPECT_NEAR(bad.terrain.foliage.shedScale, 1.0f, 1.0e-5f);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(missingPath, removeEc);
    std::filesystem::remove(clampPath, removeEc);
}

TEST_F(SceneFile, LevelJson_MissingShedScaleDefaultsToOne)
{
    const auto path = defaultScenePath("level.json");
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc) || existsEc)
        GTEST_SKIP() << "level.json is not in the content root";
    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_NEAR(data.terrain.foliage.shedScale, 1.0f, 1.0e-5f);
}

TEST_F(SceneFile, TerrainGrass_RoundTrip)
{
    const auto missingPath = tempScenePath("darkengine6_scene_terrain_grass_missing_ut.json");
    {
        std::ofstream out(missingPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "grass_missing",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "heightFile": "grass_missing.height.bin",
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData missing{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(missingPath, missing, &err)) << err;
    EXPECT_EQ(missing.version, 2);
    EXPECT_TRUE(missing.hasTerrain);
    EXPECT_FALSE(missing.terrain.foliage.present);
    expectGrassDefaults(missing.terrain.grass);

    const auto highPath = tempScenePath("darkengine6_scene_terrain_grass_high_ut.json");
    {
        std::ofstream out(highPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "grass_high",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "foliage": {
      "version": 1,
      "grassPerM2": 0.4,
      "stale": true,
      "treeModel": "models/tree.gltf",
      "grassModel": "models/Grass/grass_medium_01_2k.gltf",
      "notes": "ignored"
    },
    "grass": {
      "enabled": true,
      "height": 10,
      "flexibility": 4,
      "densityScale": 9,
      "windBaseYaw": 10,
      "windDeflection": 5,
      "windTipMetres": 4,
      "windSpatialFreq": 1,
      "windTemporalFreq": 1,
      "seed": 99,
      "notes": "ignored"
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData high{};
    ASSERT_TRUE(loadSceneFromJson(highPath, high, &err)) << err;
    EXPECT_EQ(high.version, 2);
    EXPECT_TRUE(high.terrain.foliage.present);
    EXPECT_TRUE(high.terrain.foliage.stale);
    EXPECT_NEAR(high.terrain.foliage.grassPerM2, 0.4f, 1.0e-5f);
    EXPECT_EQ(high.terrain.foliage.treeModel, "models/tree.gltf");
    EXPECT_EQ(high.terrain.foliage.grassModel, "models/Grass/grass_medium_01_2k.gltf");
    EXPECT_NEAR(high.terrain.grass.heightMetres, 1.50f, 1.0e-5f);
    EXPECT_NEAR(high.terrain.grass.flexibility, 1.0f, 1.0e-5f);
    EXPECT_NEAR(high.terrain.grass.densityScale, 1.5f, 1.0e-5f);
    Terrain::GrassParams highParams{};
    highParams.enabled          = true;
    highParams.heightMetres     = 10.0f;
    highParams.flexibility      = 4.0f;
    highParams.densityScale     = 9.0f;
    highParams.windBaseYaw      = 10.0f;
    highParams.windDeflection   = 5.0f;
    highParams.windTipMetres    = 4.0f;
    highParams.windSpatialFreq  = 1.0f;
    highParams.windTemporalFreq = 1.0f;
    highParams.seed             = 99u;
    expectGrassMatchesClamp(high.terrain.grass, highParams);

    const auto lowPath = tempScenePath("darkengine6_scene_terrain_grass_low_ut.json");
    {
        std::ofstream out(lowPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "grass_low",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "grass": {
      "enabled": false,
      "height": 0,
      "flexibility": -1,
      "densityScale": -4,
      "windBaseYaw": -10,
      "windDeflection": -1,
      "windTipMetres": -2,
      "windSpatialFreq": 0,
      "windTemporalFreq": -1,
      "seed": 0
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData low{};
    ASSERT_TRUE(loadSceneFromJson(lowPath, low, &err)) << err;
    EXPECT_EQ(low.version, 2);
    EXPECT_FALSE(low.terrain.grass.enabled);
    EXPECT_NEAR(low.terrain.grass.heightMetres, 0.05f, 1.0e-5f);
    EXPECT_NEAR(low.terrain.grass.flexibility, 0.0f, 1.0e-5f);
    EXPECT_NEAR(low.terrain.grass.densityScale, 0.0f, 1.0e-5f);
    EXPECT_EQ(low.terrain.grass.seed, 0u);
    Terrain::GrassParams lowParams{};
    lowParams.enabled          = false;
    lowParams.heightMetres     = 0.0f;
    lowParams.flexibility      = -1.0f;
    lowParams.densityScale     = -4.0f;
    lowParams.windBaseYaw      = -10.0f;
    lowParams.windDeflection   = -1.0f;
    lowParams.windTipMetres    = -2.0f;
    lowParams.windSpatialFreq  = 0.0f;
    lowParams.windTemporalFreq = -1.0f;
    lowParams.seed             = 0u;
    expectGrassMatchesClamp(low.terrain.grass, lowParams);

    const auto partialPath = tempScenePath("darkengine6_scene_terrain_grass_partial_ut.json");
    {
        std::ofstream out(partialPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "grass_partial",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "grass": { "enabled": true, "seed": 5, "notes": "ignored" },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData partial{};
    ASSERT_TRUE(loadSceneFromJson(partialPath, partial, &err)) << err;
    EXPECT_TRUE(partial.terrain.grass.enabled);
    EXPECT_EQ(partial.terrain.grass.seed, 5u);
    const GrassSceneDesc partialDefaults{};
    EXPECT_NEAR(partial.terrain.grass.heightMetres, partialDefaults.heightMetres, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.flexibility, partialDefaults.flexibility, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.densityScale, partialDefaults.densityScale, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.windBaseYaw, partialDefaults.windBaseYaw, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.windDeflection, partialDefaults.windDeflection, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.windTipMetres, partialDefaults.windTipMetres, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.windSpatialFreq, partialDefaults.windSpatialFreq, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.windTemporalFreq, partialDefaults.windTemporalFreq, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.faceEdge, partialDefaults.faceEdge, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.faceYaw, partialDefaults.faceYaw, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.faceLean, partialDefaults.faceLean, 1.0e-5f);
    EXPECT_NEAR(partial.terrain.grass.faceSteep, partialDefaults.faceSteep, 1.0e-5f);

    const auto badPath = tempScenePath("darkengine6_scene_terrain_grass_nonobject_ut.json");
    {
        std::ofstream out(badPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "grass_nonobject",
  "mode": "3d",
  "terrain": { "bindLayout": 1, "grass": false, "layers": [] },
  "objects": []
})";
    }

    SceneFileData nonObject{};
    ASSERT_TRUE(loadSceneFromJson(badPath, nonObject, &err)) << err;
    EXPECT_EQ(nonObject.version, 2);
    expectGrassDefaults(nonObject.terrain.grass);

    SceneFileData in{};
    in.version    = 2;
    in.name       = "ut_terrain_grass";
    in.mode       = SceneMode::Scene3D;
    in.hasTerrain = true;
    in.terrain.bindLayout = TerrainSceneDesc::kBindLayoutV1;
    in.terrain.foliage.present    = true;
    in.terrain.foliage.grassPerM2 = 0.4f;
    in.terrain.foliage.stale      = true;
    in.terrain.foliage.treeModel  = "models/tree.gltf";
    in.terrain.grass.enabled          = true;
    in.terrain.grass.heightMetres     = 10.0f;
    in.terrain.grass.flexibility      = 4.0f;
    in.terrain.grass.densityScale     = 9.0f;
    in.terrain.grass.windBaseYaw      = 1.37f + Math::TwoPi;
    in.terrain.grass.windDeflection   = 1.20f;
    in.terrain.grass.windTipMetres    = 0.0f;
    in.terrain.grass.windSpatialFreq  = 0.0005f;
    in.terrain.grass.windTemporalFreq = 0.0f;
    in.terrain.grass.faceEdge         = 1.5f;
    in.terrain.grass.faceYaw          = 0.2f;
    in.terrain.grass.faceLean         = -0.5f;
    in.terrain.grass.faceSteep        = 0.8f;
    in.terrain.grass.seed             = 0xFFFFFFFFu;

    const auto path = tempScenePath("darkengine6_scene_terrain_grass_ut.json");
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    std::string text;
    {
        std::ifstream inFile(path);
        ASSERT_TRUE(static_cast<bool>(inFile));
        text.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_EQ(text.find("\"version\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"grass\""), std::string::npos);
    EXPECT_NE(text.find("\"height\":"), std::string::npos);
    EXPECT_EQ(text.find("\"heightMetres\""), std::string::npos);
    EXPECT_NE(text.find("\"enabled\": true"), std::string::npos);
    EXPECT_NE(text.find("\"faceEdge\""), std::string::npos);
    EXPECT_EQ(text.find("\"height\": 10"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    EXPECT_TRUE(out.terrain.foliage.present);
    EXPECT_TRUE(out.terrain.foliage.stale);
    EXPECT_NEAR(out.terrain.foliage.grassPerM2, 0.4f, 1.0e-5f);
    EXPECT_EQ(out.terrain.foliage.treeModel, "models/tree.gltf");
    EXPECT_TRUE(out.terrain.grass.enabled);
    EXPECT_NEAR(out.terrain.grass.heightMetres, 1.50f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.flexibility, 1.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.densityScale, 1.5f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windBaseYaw, 1.37f, 1.0e-4f);
    EXPECT_NEAR(out.terrain.grass.windDeflection, 1.20f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windTipMetres, 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.windSpatialFreq, 0.0005f, 1.0e-6f);
    EXPECT_NEAR(out.terrain.grass.windTemporalFreq, 0.0f, 1.0e-5f);
    EXPECT_EQ(out.terrain.grass.seed, 0xFFFFFFFFu);
    EXPECT_NEAR(out.terrain.grass.faceEdge, 1.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.faceYaw, 0.2f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.faceLean, 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.terrain.grass.faceSteep, 0.8f, 1.0e-5f);
    Terrain::GrassParams roundParams{};
    roundParams.enabled          = true;
    roundParams.heightMetres     = 10.0f;
    roundParams.flexibility      = 4.0f;
    roundParams.densityScale     = 9.0f;
    roundParams.windBaseYaw      = 1.37f + Math::TwoPi;
    roundParams.windDeflection   = 1.20f;
    roundParams.windTipMetres    = 0.0f;
    roundParams.windSpatialFreq  = 0.0005f;
    roundParams.windTemporalFreq = 0.0f;
    roundParams.faceEdge         = 1.5f;
    roundParams.faceYaw          = 0.2f;
    roundParams.faceLean         = -0.5f;
    roundParams.faceSteep        = 0.8f;
    roundParams.seed             = 0xFFFFFFFFu;
    expectGrassMatchesClamp(out.terrain.grass, roundParams);

    SceneFileData zeros = in;
    zeros.terrain.grass.enabled          = false;
    zeros.terrain.grass.heightMetres     = 0.05f;
    zeros.terrain.grass.flexibility      = 0.0f;
    zeros.terrain.grass.densityScale     = 0.0f;
    zeros.terrain.grass.windBaseYaw      = Math::Pi;
    zeros.terrain.grass.windDeflection   = 0.0f;
    zeros.terrain.grass.windTipMetres    = 0.0f;
    zeros.terrain.grass.windSpatialFreq  = 0.02f;
    zeros.terrain.grass.windTemporalFreq = 0.10f;
    zeros.terrain.grass.faceEdge         = 0.0f;
    zeros.terrain.grass.faceYaw          = 0.0f;
    zeros.terrain.grass.faceLean         = 0.0f;
    zeros.terrain.grass.faceSteep        = 0.0f;
    zeros.terrain.grass.seed             = 0u;
    const auto zeroPath = tempScenePath("darkengine6_scene_terrain_grass_zero_ut.json");
    ASSERT_TRUE(saveSceneToJson(zeroPath, zeros, &err)) << err;
    SceneFileData zeroOut{};
    ASSERT_TRUE(loadSceneFromJson(zeroPath, zeroOut, &err)) << err;
    EXPECT_FALSE(zeroOut.terrain.grass.enabled);
    EXPECT_NEAR(zeroOut.terrain.grass.heightMetres, 0.05f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.flexibility, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.densityScale, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.windBaseYaw, Math::Pi, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.windDeflection, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.windTipMetres, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.windSpatialFreq, 0.02f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.windTemporalFreq, 0.10f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.faceEdge, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.faceYaw, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.faceLean, 0.0f, 1.0e-5f);
    EXPECT_NEAR(zeroOut.terrain.grass.faceSteep, 0.0f, 1.0e-5f);
    EXPECT_EQ(zeroOut.terrain.grass.seed, 0u);
    EXPECT_NEAR(zeroOut.terrain.foliage.grassPerM2, 0.4f, 1.0e-5f);

    std::error_code removeEc;
    std::filesystem::remove(missingPath, removeEc);
    std::filesystem::remove(highPath, removeEc);
    std::filesystem::remove(lowPath, removeEc);
    std::filesystem::remove(partialPath, removeEc);
    std::filesystem::remove(badPath, removeEc);
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(zeroPath, removeEc);
}

TEST_F(SceneFile, TerrainWind_RoundTrip)
{
    SceneFileData scene{};
    scene.version             = 2;
    scene.name                = "wind";
    scene.mode                = SceneMode::Scene3D;
    scene.hasTerrain          = true;
    scene.terrain.bindLayout  = TerrainSceneDesc::kBindLayoutV1;
    scene.terrain.heightFile  = "wind.height.bin";
    scene.terrain.wind.present  = true;
    scene.terrain.wind.settings = Terrain::defaultWindSettings();
    scene.terrain.wind.settings.strength   = 1.5f;
    scene.terrain.wind.settings.headingRad = 0.4f;
    scene.terrain.wind.settings.kind[static_cast<int>(Terrain::WindKind::Tree)].mainBendMetres = 0.8f;
    scene.terrain.grass.enabled = true;

    const auto path = tempScenePath("darkengine6_scene_wind_rt.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, scene, &err)) << err;

    SceneFileData loaded{};
    ASSERT_TRUE(loadSceneFromJson(path, loaded, &err)) << err;
    EXPECT_TRUE(loaded.terrain.wind.present);
    EXPECT_NEAR(loaded.terrain.wind.settings.strength, 1.5f, 1.0e-4f);
    EXPECT_NEAR(loaded.terrain.wind.settings.headingRad, 0.4f, 1.0e-4f);
    EXPECT_NEAR(loaded.terrain.wind.settings.kind[static_cast<int>(Terrain::WindKind::Tree)].mainBendMetres, 0.8f, 1.0e-4f);
    EXPECT_NEAR(loaded.terrain.wind.settings.kind[static_cast<int>(Terrain::WindKind::Rock)].influence, 0.0f, 1.0e-4f);
    EXPECT_NEAR(loaded.terrain.grass.windBaseYaw, loaded.terrain.wind.settings.headingRad, 1.0e-4f);
    EXPECT_TRUE(loaded.terrain.grass.enabled);

    SceneFileData omitted{};
    omitted.version            = 2;
    omitted.name               = "wind_omit";
    omitted.mode               = SceneMode::Scene3D;
    omitted.hasTerrain         = true;
    omitted.terrain.bindLayout = TerrainSceneDesc::kBindLayoutV1;
    omitted.terrain.heightFile = "wind_omit.height.bin";
    omitted.terrain.grass.windBaseYaw      = 0.25f;
    omitted.terrain.grass.windTemporalFreq = 0.5f;
    omitted.terrain.grass.seed             = 42u;
    const auto omitPath = tempScenePath("darkengine6_scene_wind_omit.json");
    ASSERT_TRUE(saveSceneToJson(omitPath, omitted, &err)) << err;
    SceneFileData omitLoaded{};
    ASSERT_TRUE(loadSceneFromJson(omitPath, omitLoaded, &err)) << err;
    EXPECT_FALSE(omitLoaded.terrain.wind.present);
    EXPECT_NEAR(omitLoaded.terrain.wind.settings.headingRad, 0.25f, 1.0e-4f);
    EXPECT_NEAR(omitLoaded.terrain.wind.settings.temporalFreq, 0.5f, 1.0e-4f);
    EXPECT_EQ(omitLoaded.terrain.wind.settings.seed, 42u);
    EXPECT_NEAR(omitLoaded.terrain.grass.windBaseYaw, 0.25f, 1.0e-4f);
    EXPECT_NEAR(omitLoaded.terrain.grass.windTemporalFreq, 0.5f, 1.0e-4f);
    EXPECT_NEAR(omitLoaded.terrain.wind.settings.kind[static_cast<int>(Terrain::WindKind::Rock)].influence, 0.0f, 1.0e-4f);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
    std::filesystem::remove(omitPath, removeEc);
}

TEST_F(SceneFile, Terrain_LegacyHeightFile_StillLoads)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_legacy_hf_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "legacy_hf",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "chunkCells": 16,
    "heightBlendK": 0.5,
    "heightFile": "legacy.height.bin",
    "splatFile": "legacy.splat.png",
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 2);
    EXPECT_TRUE(data.hasTerrain);
    EXPECT_FALSE(data.terrain.hasGrid);
    EXPECT_EQ(data.terrain.heightFile, "legacy.height.bin");
    EXPECT_EQ(data.terrain.splatFile, "legacy.splat.png");
    EXPECT_EQ(data.terrain.bindLayout, TerrainSceneDesc::kBindLayoutV1);
    EXPECT_EQ(data.terrain.chunkCells, 16);
    expectGrassDefaults(data.terrain.grass);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, TerrainGrid_TilesX9Rejected)
{
    const auto path = tempScenePath("darkengine6_scene_terrain_grid_9_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "tiles9",
  "mode": "3d",
  "terrain": {
    "bindLayout": 1,
    "grid": {
      "tilesX": 9,
      "tilesZ": 4,
      "tileCells": 512,
      "coarseFile": "too_wide.coarse.height.bin",
      "tileDir": "too_wide.tiles"
    },
    "layers": []
  },
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_TRUE(data.hasTerrain);
    EXPECT_FALSE(data.terrain.hasGrid);
    EXPECT_EQ(data.terrain.grid.tilesX, 4u);

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST_F(SceneFile, AtmosphereAndTerrainSourceRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_atmosphere";
    in.mode    = SceneMode::Scene3D;
    in.hasTerrain = true;
    in.terrain.source       = "terrain/HurricaneRidge";
    in.terrain.worldSize    = 1024.0f;
    in.terrain.importHeight = 480.0f;
    in.terrain.chunkCells   = 64;
    in.terrain.heightBlendK = -1.0f;

    in.sky.present       = true;
    in.sky.timeOfDay     = 16.2f;
    in.sky.dayOfYear     = 172.0f;
    in.sky.latitude      = 47.6f;
    in.sky.timeScale     = 0.0f;
    in.sky.weather       = "partly";
    in.sky.cloudCoverage = 0.35f;
    in.sky.turbidity     = 2.4f;
    in.sky.windSpeed     = 0.04f;
    in.sky.windDir[0]    = 1.0f;
    in.sky.windDir[1]    = 0.2f;
    in.sky.rain          = 0.0f;

    in.fog.present         = true;
    in.fog.autoFromWeather = false;
    in.fog.distanceScale   = 1.5f;
    in.fog.heightScale     = 0.5f;
    in.fog.valleyScale     = 2.0f;
    in.fog.distanceDensity = 0.02f;
    in.fog.heightDensity   = 0.01f;
    in.fog.valleyDensity   = 0.03f;
    in.fog.heightFalloff   = 0.08f;
    in.fog.valleyHeight    = 20.0f;
    in.fog.color[0]        = 0.2f;
    in.fog.color[1]        = 0.3f;
    in.fog.color[2]        = 0.4f;

    in.water.present          = true;
    in.water.hasLevel         = true;
    in.water.level            = 182.4f;
    in.water.levelFraction    = 0.38f;
    in.water.chunkCells       = 16;
    in.water.lodDistanceCount = 5;
    in.water.lodDistances[0]  = 40.0f;
    in.water.lodDistances[4]  = 640.0f;
    in.water.flowDir[0]       = 1.0f;
    in.water.flowDir[1]       = 0.35f;
    in.water.flowStrength     = 0.85f;
    in.water.steepness        = 0.55f;
    in.water.amplitudeScale   = 1.25f;
    in.water.speedScale       = 0.5f;
    in.water.waveCount        = 1;
    in.water.waves[0].angleFromFlow = 0.1f;
    in.water.waves[0].frequency     = 0.224399f;
    in.water.waves[0].amplitude     = 0.42f;
    in.water.waves[0].speed         = 1.15f;

    in.exposure.present      = true;
    in.exposure.autoExposure = false;
    in.exposure.evBias       = -0.25f;
    in.exposure.maxEv        = 1.5f;
    in.exposure.adaptBright  = 6.0f;
    in.exposure.adaptDark    = 1.0f;

    const auto path = tempScenePath("darkengine6_scene_atmosphere_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_TRUE(out.hasTerrain);
    EXPECT_EQ(out.terrain.source, "terrain/HurricaneRidge");
    EXPECT_NEAR(out.terrain.worldSize, 1024.0f, 1.0e-3f);
    EXPECT_NEAR(out.terrain.importHeight, 480.0f, 1.0e-3f);
    EXPECT_TRUE(out.sky.present);
    EXPECT_NEAR(out.sky.timeOfDay, 16.2f, 1.0e-4f);
    EXPECT_EQ(out.sky.weather, "partly");
    EXPECT_NEAR(out.sky.cloudCoverage, 0.35f, 1.0e-4f);
    EXPECT_TRUE(out.fog.present);
    EXPECT_FALSE(out.fog.autoFromWeather);
    EXPECT_NEAR(out.fog.distanceDensity, 0.02f, 1.0e-5f);
    EXPECT_NEAR(out.fog.color[2], 0.4f, 1.0e-4f);
    EXPECT_TRUE(out.water.present);
    EXPECT_TRUE(out.water.hasLevel);
    EXPECT_NEAR(out.water.level, 182.4f, 1.0e-3f);
    EXPECT_NEAR(out.water.flowDir[1], 0.35f, 1.0e-4f);
    EXPECT_EQ(out.water.waveCount, 1);
    EXPECT_NEAR(out.water.waves[0].amplitude, 0.42f, 1.0e-4f);
    EXPECT_EQ(out.water.lodDistanceCount, 5);
    EXPECT_NEAR(out.water.lodDistances[4], 640.0f, 1.0e-3f);

    const WaterParams params = sceneWaterParams(out.water, out.water.level);
    EXPECT_NEAR(params.waterLevel, 182.4f, 1.0e-3f);
    EXPECT_NEAR(params.flowDir.y, 0.35f, 1.0e-4f);
    EXPECT_NEAR(params.waves[0].amplitude, 0.42f, 1.0e-4f);
    EXPECT_NEAR(params.amplitudeScale, 1.25f, 1.0e-4f);

    Sky::Environment env{};
    applySceneAtmosphere(env, out);
    EXPECT_NEAR(env.timeOfDay, 16.2f, 1.0e-4f);
    EXPECT_NEAR(env.weather.cloudCoverage, 0.35f, 1.0e-4f);
    EXPECT_FALSE(env.fogAuto);
    EXPECT_NEAR(env.fogDensity(), 0.02f, 1.0e-5f);
    EXPECT_TRUE(out.exposure.present);
    EXPECT_FALSE(out.exposure.autoExposure);
    EXPECT_NEAR(out.exposure.evBias, -0.25f, 1.0e-4f);
    EXPECT_NEAR(out.exposure.maxEv, 1.5f, 1.0e-4f);
    EXPECT_NEAR(out.exposure.adaptBright, 6.0f, 1.0e-4f);
    EXPECT_NEAR(out.exposure.adaptDark, 1.0f, 1.0e-4f);

    SceneFileData omitted = in;
    omitted.exposure = {};
    const auto omittedPath = tempScenePath("darkengine6_scene_exposure_omit_ut.json");
    ASSERT_TRUE(saveSceneToJson(omittedPath, omitted, &err)) << err;
    SceneFileData missing{};
    ASSERT_TRUE(loadSceneFromJson(omittedPath, missing, &err)) << err;
    EXPECT_FALSE(missing.exposure.present);
    EXPECT_FALSE(missing.exposure.autoExposure);
    EXPECT_NEAR(missing.exposure.evBias, 0.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(omittedPath, ec);
}

TEST_F(SceneFile, CastShadowMissingKeyStaysFalse)
{
    const auto path = tempScenePath("darkengine6_scene_cast_missing_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "cast_missing",
  "objects": [
    {"archetype": "core:point_light", "overrides": {"LocalLight": {"intensity": 4, "range": 3, "enabled": true}}}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_TRUE(sceneHasLight(data.objects[0]));
    EXPECT_FALSE(sceneLightBool(data.objects[0], "castShadow", false));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, StreamRoundTrip)
{
    SceneFileData in{};
    in.version = 3;
    in.mode = SceneMode::Scene3D;
    in.name = "streams";
    SceneObjectData stream;
    stream.archetype = "core:stream";
    stream.position  = Vector3f(9.0f, 8.0f, 7.0f);
    sceneWriteStream(stream, 4.0f, 2.0f, { Vector2f(1.0f, 2.0f), Vector2f(3.0f, 4.0f), Vector2f(5.0f, 6.0f) });
    in.objects.push_back(stream);

    const auto path = tempScenePath("darkengine6_scene_stream_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:stream");
    float width = 0.0f;
    float flow  = 0.0f;
    std::vector<Vector2f> points;
    ASSERT_TRUE(sceneReadStream(out.objects[0], width, flow, points));
    EXPECT_NEAR(width, 4.0f, 1.0e-4f);
    EXPECT_NEAR(flow, 2.0f, 1.0e-4f);
    ASSERT_EQ(points.size(), 3u);
    EXPECT_NEAR(points[2].x, 5.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.y, 8.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.z, 2.0f, 1.0e-4f);

    in.water.present = true;
    in.water.foam = 0.4f;
    in.water.flowSpeed = 1.2f;
    in.water.foamWidthScale = 1.5f;
    in.water.detail = 0.25f;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    SceneFileData waterOut{};
    ASSERT_TRUE(loadSceneFromJson(path, waterOut, &err)) << err;
    EXPECT_NEAR(waterOut.water.foam, 0.4f, 1.0e-4f);
    EXPECT_NEAR(waterOut.water.flowSpeed, 1.2f, 1.0e-4f);
    EXPECT_NEAR(waterOut.water.foamWidthScale, 1.5f, 1.0e-4f);
    EXPECT_NEAR(waterOut.water.detail, 0.25f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, CastShadowTrueRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_cast_true";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData point{};
    point.archetype = "core:point_light";
    SceneLightFields pointLight;
    pointLight.kind       = "point";
    pointLight.intensity  = 10.0f;
    pointLight.range      = 4.0f;
    pointLight.enabled    = true;
    pointLight.castShadow = true;
    sceneWriteLight(point, pointLight);
    in.objects.push_back(point);

    const auto path = tempScenePath("darkengine6_scene_cast_true_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.version, 2);
    EXPECT_TRUE(sceneLightBool(out.objects[0], "castShadow", false));
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "intensity", 0.0f), 10.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, DirectionalLightDoesNotGainCastShadow)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_cast_sun";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData sun{};
    sun.archetype = "core:directional_light";
    SceneLightFields sunLight;
    sunLight.kind       = "directional";
    sunLight.intensity  = 3.0f;
    sunLight.castShadow = true;
    sceneWriteLight(sun, sunLight);
    in.objects.push_back(sun);

    const auto path = tempScenePath("darkengine6_scene_cast_sun_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    std::ifstream file(path);
    ASSERT_TRUE(static_cast<bool>(file));
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("castShadow"), std::string::npos);

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:directional_light");
    EXPECT_FALSE(sceneLightBool(out.objects[0], "castShadow", true));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, CastShadowNonBoolStaysFalse)
{
    const auto path = tempScenePath("darkengine6_scene_cast_bad_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "cast_bad",
  "objects": [
    {"archetype": "core:point_light", "overrides": {"LocalLight": {"intensity": 4, "range": 3, "enabled": true, "castShadow": 1}}},
    {"archetype": "core:spot_light", "overrides": {"LocalLight": {"intensity": 4, "range": 3, "enabled": true, "castShadow": "true"}}}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_FALSE(sceneLightBool(data.objects[0], "castShadow", false));
    EXPECT_FALSE(sceneLightBool(data.objects[1], "castShadow", false));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, CloudDeckRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_cloud_deck";
    in.mode    = SceneMode::Scene3D;
    in.sky.present           = true;
    in.sky.cloudAltitude     = 3200.0f;
    in.sky.cloudAmount       = 1.25f;
    in.sky.cloudWindSpeedMps = 11.0f;

    const auto path = tempScenePath("darkengine6_scene_cloud_deck_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_NEAR(out.sky.cloudAltitude, 3200.0f, 1.0e-3f);
    EXPECT_NEAR(out.sky.cloudAmount, 1.25f, 1.0e-4f);
    EXPECT_NEAR(out.sky.cloudWindSpeedMps, 11.0f, 1.0e-3f);

    Sky::Environment env;
    env.cloudLayer.altitude     = 4100.0f;
    env.cloudLayer.amount       = 0.4f;
    env.cloudLayer.windSpeedMps = 6.0f;
    env.tick(0.2f);
    EXPECT_GT(env.cloudWindBase().Magnitude(), 0.5f);

    SceneFileData captured{};
    captureSceneAtmosphere(env, captured);
    Sky::Environment loaded;
    loaded.tick(0.2f);
    applySceneAtmosphere(loaded, captured);
    EXPECT_NEAR(loaded.cloudLayer.altitude, 4100.0f, 1.0e-3f);
    EXPECT_NEAR(loaded.cloudLayer.amount, 0.4f, 1.0e-4f);
    EXPECT_NEAR(loaded.cloudLayer.windSpeedMps, 6.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(loaded.cloudWindBase().x, 0.0f);
    EXPECT_FLOAT_EQ(loaded.cloudWindBase().y, 0.0f);

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
}

TEST_F(SceneFile, CloudDeckMissingKeysUseDefaults)
{
    const auto path = tempScenePath("darkengine6_scene_cloud_old_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "sky_old",
  "objects": [],
  "sky": {
    "timeOfDay": 16.2,
    "weather": "partly",
    "cloudCoverage": 0.35,
    "cloudLayerEnabled": true,
    "cloudThickness": 100,
    "cloudTauMax": 4,
    "cloudWindDir": [0, 1]
  }
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_TRUE(data.sky.present);
    EXPECT_NEAR(data.sky.cloudCoverage, 0.35f, 1.0e-4f);
    EXPECT_NEAR(data.sky.cloudAltitude, 2000.0f, 1.0e-3f);
    EXPECT_NEAR(data.sky.cloudAmount, 1.0f, 1.0e-4f);
    EXPECT_NEAR(data.sky.cloudWindSpeedMps, 8.0f, 1.0e-3f);

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
}

TEST_F(SceneFile, SpawnerRoundTrip)
{
    SceneFileData in{};
    in.version = 3;
    in.name    = "ut_spawner";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData spawner{};
    spawner.archetype = "core:spawner";
    spawner.position  = Vector3f(12.0f, 0.5f, -6.0f);
    SpawnerDesc authored;
    authored.mode         = SpawnerMode::Waves;
    authored.unit         = SpawnerUnit::Wolf;
    authored.gate         = SpawnerWaveGate::Time;
    authored.count        = 4;
    authored.radius       = 9.5f;
    authored.respawnDelay = 2.25f;
    authored.waveInterval = 12.0f;
    authored.maxWaves     = 3;
    authored.enabled      = false;
    authored.seed         = 99;
    sceneWriteSpawner(spawner, authored);
    in.objects.push_back(spawner);

    const auto path = tempScenePath("darkengine6_scene_spawner_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].archetype, "core:spawner");
    SpawnerDesc loaded;
    ASSERT_TRUE(sceneReadSpawner(out.objects[0], loaded));
    EXPECT_EQ(loaded.mode, SpawnerMode::Waves);
    EXPECT_EQ(loaded.unit, SpawnerUnit::Wolf);
    EXPECT_EQ(loaded.gate, SpawnerWaveGate::Time);
    EXPECT_EQ(loaded.count, 4);
    EXPECT_NEAR(loaded.radius, 9.5f, 1.0e-4f);
    EXPECT_NEAR(loaded.respawnDelay, 2.25f, 1.0e-4f);
    EXPECT_NEAR(loaded.waveInterval, 12.0f, 1.0e-4f);
    EXPECT_EQ(loaded.maxWaves, 3);
    EXPECT_FALSE(loaded.enabled);
    EXPECT_EQ(loaded.seed, 99u);
    EXPECT_NEAR(out.objects[0].position.x, 12.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.z, -6.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, SpawnerMistypedFieldsKeepDefaults)
{
    const auto path = tempScenePath("darkengine6_scene_spawner_bad_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 3,
  "name": "spawner_bad",
  "objects": [
    {"archetype": "core:spawner", "overrides": {"Spawner": {"mode": 1, "unit": false, "gate": "nope", "count": "x", "radius": true, "enabled": "yes", "seed": -4, "maxWaves": 0}}}
  ]
})";
    }

    std::vector<std::string> logs;
    g_sceneLogs = &logs;
    Log::setCapture(&captureSceneLog);
    struct ClearCapture
    {
        ~ClearCapture()
        {
            Log::setCapture(nullptr);
            g_sceneLogs = nullptr;
        }
    } clearCapture;

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 1u);
    SpawnerDesc loaded;
    ASSERT_TRUE(sceneReadSpawner(data.objects[0], loaded));
    EXPECT_EQ(loaded.mode, SpawnerMode::Maintain);
    EXPECT_EQ(loaded.unit, SpawnerUnit::Hunter);
    EXPECT_EQ(loaded.gate, SpawnerWaveGate::Cleared);
    EXPECT_EQ(loaded.count, 3);
    EXPECT_NEAR(loaded.radius, 6.0f, 1.0e-4f);
    EXPECT_TRUE(loaded.enabled);
    EXPECT_EQ(loaded.seed, 1u);
    EXPECT_EQ(loaded.maxWaves, 0);

    auto logged = [&logs](const char* key) {
        for (const std::string& line : logs)
        {
            if (line.find(key) != std::string::npos)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(logged("\"mode\""));
    EXPECT_TRUE(logged("\"unit\""));
    EXPECT_TRUE(logged("\"count\""));
    EXPECT_TRUE(logged("\"radius\""));
    EXPECT_TRUE(logged("\"enabled\""));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, StealthRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_stealth";
    in.mode    = SceneMode::Scene3D;
    in.stealth.present           = true;
    in.stealth.awareness         = true;
    in.stealth.hasHearStand      = true;
    in.stealth.hearStand         = 0.0f;
    in.stealth.hasHearWalk       = true;
    in.stealth.hearWalk          = 6.0f;
    in.stealth.hasHearSprint     = true;
    in.stealth.hearSprint        = 16.0f;
    in.stealth.hasHearCrouchWalk = true;
    in.stealth.hearCrouchWalk    = 2.5f;

    const auto path = tempScenePath("darkengine6_scene_stealth_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_TRUE(out.stealth.present);
    EXPECT_TRUE(out.stealth.awareness);
    EXPECT_TRUE(out.stealth.hasHearStand);
    EXPECT_FLOAT_EQ(out.stealth.hearStand, 0.0f);
    EXPECT_TRUE(out.stealth.hasHearWalk);
    EXPECT_FLOAT_EQ(out.stealth.hearWalk, 6.0f);
    EXPECT_TRUE(out.stealth.hasHearSprint);
    EXPECT_FLOAT_EQ(out.stealth.hearSprint, 16.0f);
    EXPECT_TRUE(out.stealth.hasHearCrouchWalk);
    EXPECT_FLOAT_EQ(out.stealth.hearCrouchWalk, 2.5f);
    EXPECT_FALSE(out.stealth.hasHearCrouchStill);

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
}

TEST_F(SceneFile, StealthMissingKeyStaysOff)
{
    const auto path = tempScenePath("darkengine6_scene_stealth_off_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "no_stealth",
  "objects": []
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_FALSE(data.stealth.present);
    EXPECT_FALSE(data.stealth.awareness);
    EXPECT_FALSE(data.stealth.hasHearWalk);

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
}

TEST_F(SceneFile, StealthBooleanAndHearOnly)
{
    const auto flagPath = tempScenePath("darkengine6_scene_stealth_flag_ut.json");
    {
        std::ofstream out(flagPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "stealth_flag",
  "stealth": true,
  "objects": []
})";
    }

    SceneFileData flagged{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(flagPath, flagged, &err)) << err;
    EXPECT_TRUE(flagged.stealth.present);
    EXPECT_TRUE(flagged.stealth.awareness);
    EXPECT_FALSE(flagged.stealth.hasHearWalk);
    EXPECT_FALSE(flagged.stealth.hasSettings);

    const auto hearPath = tempScenePath("darkengine6_scene_stealth_hear_ut.json");
    {
        std::ofstream out(hearPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "stealth_hear",
  "stealth": { "awareness": false, "hearWalk": 6.0, "hearSprint": -3 },
  "objects": []
})";
    }

    SceneFileData hear{};
    ASSERT_TRUE(loadSceneFromJson(hearPath, hear, &err)) << err;
    EXPECT_TRUE(hear.stealth.present);
    EXPECT_FALSE(hear.stealth.awareness);
    EXPECT_TRUE(hear.stealth.hasHearWalk);
    EXPECT_FLOAT_EQ(hear.stealth.hearWalk, 6.0f);
    EXPECT_TRUE(hear.stealth.hasHearSprint);
    EXPECT_FLOAT_EQ(hear.stealth.hearSprint, 0.0f);
    EXPECT_FALSE(hear.stealth.hasHearStand);
    EXPECT_FALSE(hear.stealth.hasSettings);

    std::error_code ecRemove;
    std::filesystem::remove(flagPath, ecRemove);
    std::filesystem::remove(hearPath, ecRemove);
}

TEST_F(SceneFile, StealthSettingsRoundTrip)
{
    const auto path = tempScenePath("darkengine6_scene_stealth_settings_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "stealth_settings",
  "stealth": {
    "awareness": true,
    "settings": {
      "alertShareRadius": 3.5,
      "wallScale": 0.25,
      "stanceSprint": 1.7,
      "bodyWeight": [0.4, 0.4, 0.55]
    }
  },
  "objects": []
})";
    }

    SceneFileData partial{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, partial, &err)) << err;
    EXPECT_TRUE(partial.stealth.present);
    EXPECT_TRUE(partial.stealth.awareness);
    EXPECT_TRUE(partial.stealth.hasSettings);
    EXPECT_NEAR(partial.stealth.settings.alertShareRadius, 3.5f, 1.0e-4f);
    EXPECT_NEAR(partial.stealth.settings.wallScale, 0.25f, 1.0e-4f);
    EXPECT_NEAR(partial.stealth.settings.stanceSprint, 1.7f, 1.0e-4f);
    EXPECT_NEAR(partial.stealth.settings.bodyWeight[2], 0.55f, 1.0e-4f);
    EXPECT_NEAR(partial.stealth.settings.fillNear, AI::AwarenessSettings{}.fillNear, 1.0e-4f);
    EXPECT_EQ(partial.stealth.settingsMask & (1ull << 0), 0ull);
    EXPECT_NE(partial.stealth.settingsMask & (1ull << 27), 0ull);
    EXPECT_NE(partial.stealth.settingsMask & (1ull << 30), 0ull);
    EXPECT_NE(partial.stealth.settingsMask & (1ull << 35), 0ull);
    EXPECT_NE(partial.stealth.settingsMask & (1ull << 52), 0ull);

    const auto badPath = tempScenePath("darkengine6_scene_stealth_bad_settings_ut.json");
    {
        std::ofstream out(badPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "stealth_bad_settings",
  "stealth": { "awareness": true, "hearWalk": 6, "settings": { "version": 2, "alertShareRadius": 1 } },
  "objects": []
})";
    }
    SceneFileData bad{};
    ASSERT_TRUE(loadSceneFromJson(badPath, bad, &err)) << err;
    EXPECT_TRUE(bad.stealth.awareness);
    EXPECT_TRUE(bad.stealth.hasHearWalk);
    EXPECT_FLOAT_EQ(bad.stealth.hearWalk, 6.0f);
    EXPECT_FALSE(bad.stealth.hasSettings);

    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_aware_settings";
    in.mode    = SceneMode::Scene3D;
    in.stealth.present      = true;
    in.stealth.awareness    = true;
    in.stealth.hasSettings  = true;
    in.stealth.settingsMask = ~0ull;
    in.stealth.settings.alertShareRadius = 3.5f;
    in.stealth.settings.wallScale        = 0.25f;
    in.stealth.settings.stanceSprint     = 1.7f;
    in.stealth.settings.bodyWeight[2]    = 0.55f;

    const auto roundPath = tempScenePath("darkengine6_scene_stealth_settings_round_ut.json");
    ASSERT_TRUE(saveSceneToJson(roundPath, in, &err)) << err;
    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(roundPath, out, &err)) << err;
    EXPECT_TRUE(out.stealth.hasSettings);
    EXPECT_NEAR(out.stealth.settings.alertShareRadius, 3.5f, 1.0e-4f);
    EXPECT_NEAR(out.stealth.settings.wallScale, 0.25f, 1.0e-4f);
    EXPECT_NEAR(out.stealth.settings.stanceSprint, 1.7f, 1.0e-4f);
    EXPECT_NEAR(out.stealth.settings.bodyWeight[0], 0.4f, 1.0e-4f);
    EXPECT_NEAR(out.stealth.settings.bodyWeight[2], 0.55f, 1.0e-4f);
    EXPECT_NEAR(out.stealth.settings.fillNear, 1.8f, 1.0e-4f);
    EXPECT_NE(out.stealth.settingsMask & (1ull << 0), 0ull);
    EXPECT_NE(out.stealth.settingsMask & (1ull << 27), 0ull);
    EXPECT_NE(out.stealth.settingsMask & (1ull << 50), 0ull);
    EXPECT_NE(out.stealth.settingsMask & (1ull << 52), 0ull);
    EXPECT_EQ(out.stealth.settingsMask >> 53, 0ull);

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
    std::filesystem::remove(badPath, ecRemove);
    std::filesystem::remove(roundPath, ecRemove);
}

TEST_F(SceneFile, QuestTagsWrongTypeLogsAndKeepsDefaults)
{
    const auto goodPath = tempScenePath("darkengine6_scene_quest_tags_ut.json");
    {
        std::ofstream out(goodPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "quest_tags",
  "quest": "north_lab",
  "objects": [
    { "archetype": "core:cube", "conceal": true, "goal": true, "circuit": "power", "zone": "gate", "pickup": "rock", "pickupCount": 3, "dialogue": "dialogue/hunter_gate.dlg.json", "squad": "gate", "block": false, "patrol": [[1, 2], [3, 4]] }
  ]
})";
    }

    SceneFileData good{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(goodPath, good, &err)) << err;
    EXPECT_EQ(good.quest, "north_lab");
    ASSERT_EQ(good.objects.size(), 1u);
    EXPECT_TRUE(sceneConceal(good.objects[0]));
    EXPECT_TRUE(sceneGoal(good.objects[0]));
    EXPECT_EQ(sceneCircuit(good.objects[0]), "power");
    EXPECT_EQ(sceneZone(good.objects[0]), "gate");
    EXPECT_EQ(scenePickup(good.objects[0]), "rock");
    EXPECT_EQ(scenePickupCount(good.objects[0]), 3);
    EXPECT_EQ(sceneDialogue(good.objects[0]), "dialogue/hunter_gate.dlg.json");
    EXPECT_EQ(sceneSquad(good.objects[0]), "gate");
    EXPECT_FALSE(sceneBlock(good.objects[0]));
    const std::vector<Vector2f> goodPatrol = scenePatrol(good.objects[0]);
    ASSERT_EQ(goodPatrol.size(), 2u);
    EXPECT_FLOAT_EQ(goodPatrol[0].x, 1.0f);
    EXPECT_FLOAT_EQ(goodPatrol[0].y, 2.0f);
    EXPECT_FLOAT_EQ(goodPatrol[1].x, 3.0f);
    EXPECT_FLOAT_EQ(goodPatrol[1].y, 4.0f);

    const auto roundPath = tempScenePath("darkengine6_scene_quest_tags_round_ut.json");
    ASSERT_TRUE(saveSceneToJson(roundPath, good, &err)) << err;
    SceneFileData round{};
    ASSERT_TRUE(loadSceneFromJson(roundPath, round, &err)) << err;
    ASSERT_EQ(round.objects.size(), 1u);
    EXPECT_TRUE(sceneConceal(round.objects[0]));
    EXPECT_TRUE(sceneGoal(round.objects[0]));
    EXPECT_EQ(sceneCircuit(round.objects[0]), "power");
    EXPECT_EQ(sceneZone(round.objects[0]), "gate");
    EXPECT_EQ(scenePickup(round.objects[0]), "rock");
    EXPECT_EQ(scenePickupCount(round.objects[0]), 3);
    EXPECT_EQ(sceneDialogue(round.objects[0]), "dialogue/hunter_gate.dlg.json");
    EXPECT_EQ(sceneSquad(round.objects[0]), "gate");
    EXPECT_FALSE(sceneBlock(round.objects[0]));
    const std::vector<Vector2f> roundPatrol = scenePatrol(round.objects[0]);
    ASSERT_EQ(roundPatrol.size(), 2u);
    EXPECT_FLOAT_EQ(roundPatrol[0].x, 1.0f);
    EXPECT_FLOAT_EQ(roundPatrol[0].y, 2.0f);
    EXPECT_FLOAT_EQ(roundPatrol[1].x, 3.0f);
    EXPECT_FLOAT_EQ(roundPatrol[1].y, 4.0f);

    const auto badPath = tempScenePath("darkengine6_scene_quest_tags_bad_ut.json");
    {
        std::ofstream out(badPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "quest_tags_bad",
  "quest": 12,
  "objects": [
    { "archetype": "core:cube", "conceal": "yes", "goal": 1, "circuit": true, "zone": 4, "pickup": false, "pickupCount": "two", "dialogue": 1 }
  ]
})";
    }

    std::vector<std::string> logs;
    g_sceneLogs = &logs;
    Log::setCapture(&captureSceneLog);
    struct ClearCapture
    {
        ~ClearCapture()
        {
            Log::setCapture(nullptr);
            g_sceneLogs = nullptr;
        }
    } clearCapture;

    SceneFileData bad{};
    ASSERT_TRUE(loadSceneFromJson(badPath, bad, &err)) << err;
    EXPECT_TRUE(bad.quest.empty());
    ASSERT_EQ(bad.objects.size(), 1u);
    EXPECT_FALSE(sceneConceal(bad.objects[0]));
    EXPECT_FALSE(sceneGoal(bad.objects[0]));
    EXPECT_TRUE(sceneCircuit(bad.objects[0]).empty());
    EXPECT_TRUE(sceneZone(bad.objects[0]).empty());
    EXPECT_TRUE(scenePickup(bad.objects[0]).empty());
    EXPECT_EQ(scenePickupCount(bad.objects[0]), 1);
    EXPECT_TRUE(sceneDialogue(bad.objects[0]).empty());

    auto logged = [&logs](const char* key) {
        for (const std::string& line : logs)
        {
            if (line.find(key) != std::string::npos)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(logged("\"quest\""));
    EXPECT_TRUE(logged("\"conceal\""));
    EXPECT_TRUE(logged("\"goal\""));
    EXPECT_TRUE(logged("\"circuit\""));
    EXPECT_TRUE(logged("\"zone\""));
    EXPECT_TRUE(logged("\"pickup\""));
    EXPECT_TRUE(logged("\"pickupCount\""));
    EXPECT_TRUE(logged("\"dialogue\""));

    std::error_code ecRemove;
    std::filesystem::remove(goodPath, ecRemove);
    std::filesystem::remove(badPath, ecRemove);
    std::filesystem::remove(roundPath, ecRemove);
}

TEST_F(SceneFile, MovieRoundTrip)
{
    SceneFileData in{};
    in.version = 3;
    in.name    = "movie_cue";
    in.mode    = SceneMode::Scene3D;
    SceneObjectData movie{};
    movie.archetype = "core:movie";
    movie.position  = Vector3f{ 1.0f, 2.0f, 3.0f };
    sceneWriteMovie(movie, "movies/intro.mp4", 4.5f, true);
    in.objects.push_back(movie);
    SceneObjectData plain{};
    plain.archetype = "core:cube";
    in.objects.push_back(plain);

    const auto path = tempScenePath("darkengine6_scene_movie_round_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    {
        std::ifstream saved(path, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(saved));
        const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("\"Movie\""), std::string::npos);
        EXPECT_NE(text.find("movies/intro.mp4"), std::string::npos);
        EXPECT_NE(text.find("\"path\""), std::string::npos);
        EXPECT_NE(text.find("\"radius\""), std::string::npos);
        EXPECT_NE(text.find("\"loop\""), std::string::npos);
        EXPECT_EQ(text.find("\"movie\":"), std::string::npos);
        EXPECT_EQ(text.find("\"movieRadius\""), std::string::npos);
        EXPECT_EQ(text.find("\"movieLoop\""), std::string::npos);
        const auto cubeAt = text.find("\"archetype\": \"core:cube\"");
        ASSERT_NE(cubeAt, std::string::npos);
        EXPECT_EQ(text.find("\"Movie\"", cubeAt), std::string::npos);
    }

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 2u);
    EXPECT_EQ(out.objects[0].archetype, "core:movie");
    EXPECT_EQ(sceneMoviePath(out.objects[0]), "movies/intro.mp4");
    EXPECT_NEAR(sceneMovieRadius(out.objects[0]), 4.5f, 1.0e-4f);
    EXPECT_TRUE(sceneMovieLoop(out.objects[0]));
    EXPECT_EQ(out.objects[1].archetype, "core:cube");
    EXPECT_TRUE(sceneMoviePath(out.objects[1]).empty());
    EXPECT_NEAR(sceneMovieRadius(out.objects[1]), 2.0f, 1.0e-5f);
    EXPECT_FALSE(sceneMovieLoop(out.objects[1]));

    SceneFileData defaults{};
    defaults.version = 3;
    defaults.name    = "movie_defaults";
    defaults.mode    = SceneMode::Scene3D;
    SceneObjectData untouched{};
    untouched.archetype = "core:movie";
    defaults.objects.push_back(untouched);
    const auto defaultPath = tempScenePath("darkengine6_scene_movie_defaults_ut.json");
    ASSERT_TRUE(saveSceneToJson(defaultPath, defaults, &err)) << err;
    {
        std::ifstream saved(defaultPath, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(saved));
        const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        EXPECT_EQ(text.find("\"Movie\""), std::string::npos);
        EXPECT_EQ(text.find("\"movie\":"), std::string::npos);
        EXPECT_EQ(text.find("\"movieRadius\""), std::string::npos);
        EXPECT_EQ(text.find("\"movieLoop\""), std::string::npos);
    }
    SceneFileData loadedDefaults{};
    ASSERT_TRUE(loadSceneFromJson(defaultPath, loadedDefaults, &err)) << err;
    ASSERT_EQ(loadedDefaults.objects.size(), 1u);
    EXPECT_EQ(loadedDefaults.objects[0].archetype, "core:movie");
    EXPECT_TRUE(sceneMoviePath(loadedDefaults.objects[0]).empty());
    EXPECT_NEAR(sceneMovieRadius(loadedDefaults.objects[0]), 2.0f, 1.0e-5f);
    EXPECT_FALSE(sceneMovieLoop(loadedDefaults.objects[0]));

    const auto rawPath = tempScenePath("darkengine6_scene_movie_raw_ut.json");
    {
        std::ofstream raw(rawPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(raw));
        raw << R"({
  "version": 2,
  "name": "movie_raw",
  "objects": [
    { "archetype": "core:movie" },
    { "archetype": "core:movie", "overrides": { "Movie": { "path": "movies/a.mp4", "radius": -3.5, "loop": true } } }
  ]
})";
    }
    SceneFileData rawOut{};
    ASSERT_TRUE(loadSceneFromJson(rawPath, rawOut, &err)) << err;
    ASSERT_EQ(rawOut.objects.size(), 2u);
    EXPECT_TRUE(sceneMoviePath(rawOut.objects[0]).empty());
    EXPECT_NEAR(sceneMovieRadius(rawOut.objects[0]), 2.0f, 1.0e-5f);
    EXPECT_FALSE(sceneMovieLoop(rawOut.objects[0]));
    EXPECT_EQ(sceneMoviePath(rawOut.objects[1]), "movies/a.mp4");
    EXPECT_FLOAT_EQ(sceneMovieRadius(rawOut.objects[1]), 0.0f);
    EXPECT_TRUE(sceneMovieLoop(rawOut.objects[1]));

    std::error_code ecRemove;
    std::filesystem::remove(path, ecRemove);
    std::filesystem::remove(defaultPath, ecRemove);
    std::filesystem::remove(rawPath, ecRemove);
}

TEST_F(SceneFile, ComponentOverridesRoundTrip)
{
    SceneFileData in{};
    in.version = 3;
    in.name    = "ut_overrides";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData point{};
    point.archetype = "core:point_light";
    SceneLightFields pointLight;
    pointLight.kind      = "point";
    pointLight.intensity = 600.0f;
    pointLight.range     = 8.0f;
    pointLight.enabled   = true;
    pointLight.circuit   = "gate";
    sceneWriteLight(point, pointLight);
    in.objects.push_back(point);

    SceneObjectData cloud{};
    cloud.archetype = "core:cloud_volume";
    CloudVolumeDesc cloudDesc;
    cloudDesc.shape   = CloudShape::Box;
    cloudDesc.density = 2.0f;
    sceneDataFromCloudDesc(cloudDesc, cloud);
    in.objects.push_back(cloud);

    SceneObjectData stream{};
    stream.archetype = "core:stream";
    stream.position  = Vector3f(9.0f, 8.0f, 7.0f);
    sceneWriteStream(stream, 4.0f, 2.0f, { Vector2f(1.0f, 2.0f), Vector2f(3.0f, 4.0f) });
    in.objects.push_back(stream);

    SceneObjectData spawner{};
    spawner.archetype = "core:spawner";
    SpawnerDesc spawnerDesc;
    spawnerDesc.mode  = SpawnerMode::Waves;
    spawnerDesc.unit  = SpawnerUnit::Wolf;
    spawnerDesc.count = 4;
    spawnerDesc.seed  = 99;
    sceneWriteSpawner(spawner, spawnerDesc);
    in.objects.push_back(spawner);

    SceneObjectData model{};
    model.archetype = "core:model";
    sceneWriteModelPath(model, "models/wolf.gltf");
    in.objects.push_back(model);

    SceneObjectData glow{};
    glow.archetype = "core:sphere";
    sceneWriteEmissive(glow, 1.0f);
    in.objects.push_back(glow);

    const auto path = tempScenePath("darkengine6_scene_overrides_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    {
        std::ifstream saved(path, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(saved));
        const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("\"LocalLight\""), std::string::npos);
        EXPECT_NE(text.find("\"CloudVolume\""), std::string::npos);
        EXPECT_NE(text.find("\"Stream\""), std::string::npos);
        EXPECT_NE(text.find("\"Spawner\""), std::string::npos);
        EXPECT_NE(text.find("\"Model\""), std::string::npos);
        EXPECT_NE(text.find("\"Mesh\""), std::string::npos);
        EXPECT_NE(text.find("models/wolf.gltf"), std::string::npos);
        EXPECT_EQ(text.find("\"light\":"), std::string::npos);
        EXPECT_EQ(text.find("\"particle\":"), std::string::npos);
        EXPECT_EQ(text.find("\"cloud\":"), std::string::npos);
        EXPECT_EQ(text.find("\"stream\":"), std::string::npos);
        EXPECT_EQ(text.find("\"spawner\":"), std::string::npos);
        EXPECT_EQ(text.find("\"model\":"), std::string::npos);
        const auto meshAt = text.find("\"Mesh\"");
        ASSERT_NE(meshAt, std::string::npos);
        EXPECT_EQ(text.find("\"emissive\":"), text.find("\"emissive\":", meshAt));
        EXPECT_NE(text.find("\"circuit\""), std::string::npos);
    }

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 6u);
    EXPECT_EQ(out.version, 3);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "intensity", 0.0f), 600.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[0], "range", 0.0f), 8.0f, 1.0e-4f);
    EXPECT_TRUE(sceneHasLight(out.objects[0]));
    EXPECT_EQ(sceneCircuit(out.objects[0]), "gate");
    CloudVolumeDesc loadedCloud;
    cloudDescFromSceneData(out.objects[1], loadedCloud);
    EXPECT_TRUE(sceneCarriesComponent(out.objects[1], "CloudVolume"));
    EXPECT_EQ(loadedCloud.shape, CloudShape::Box);
    EXPECT_NEAR(loadedCloud.density, 2.0f, 1.0e-4f);
    float width = 0.0f;
    float flow  = 0.0f;
    std::vector<Vector2f> points;
    ASSERT_TRUE(sceneReadStream(out.objects[2], width, flow, points));
    EXPECT_NEAR(width, 4.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[2].position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[2].position.y, 8.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[2].position.z, 2.0f, 1.0e-4f);
    SpawnerDesc loadedSpawner;
    ASSERT_TRUE(sceneReadSpawner(out.objects[3], loadedSpawner));
    EXPECT_EQ(loadedSpawner.unit, SpawnerUnit::Wolf);
    EXPECT_EQ(loadedSpawner.mode, SpawnerMode::Waves);
    EXPECT_EQ(loadedSpawner.count, 4);
    EXPECT_EQ(loadedSpawner.seed, 99u);
    EXPECT_EQ(sceneModelPath(out.objects[4]), "models/wolf.gltf");
    EXPECT_NEAR(sceneEmissive(out.objects[5]), 1.0f, 1.0e-4f);
    EXPECT_NE(out.objects[0].overridesJson.find("LocalLight"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, OldPayloadKeysAreIgnored)
{
    const auto path = tempScenePath("darkengine6_scene_legacy_payload_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "legacy_payload",
  "objects": [
    {"archetype": "core:point_light", "light": {"intensity": 4, "range": 3, "enabled": true}},
    {"archetype": "core:model", "model": "models/wolf.gltf"},
    {"archetype": "core:stream", "position": [9, 8, 7], "stream": {"width": 4, "flowSpeed": 2, "points": [[1, 2], [3, 4]]}},
    {"archetype": "core:sphere", "emissive": 1.5},
    {"type": "cube", "position": [1, 2, 3]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 4u);
    EXPECT_EQ(data.objects[0].archetype, "core:point_light");
    EXPECT_TRUE(sceneHasLight(data.objects[0]));
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "intensity", 0.0f), 1885.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "range", 0.0f), 8.0f, 1.0e-4f);
    EXPECT_TRUE(sceneModelPath(data.objects[1]).empty());
    EXPECT_EQ(data.objects[2].archetype, "core:sphere");
    EXPECT_NEAR(sceneEmissive(data.objects[2]), 0.0f, 1.0e-4f);
    EXPECT_TRUE(sceneIsPlaceholder(data.objects[3]));
    EXPECT_TRUE(data.objects[3].archetype.empty());
    EXPECT_NEAR(data.objects[3].position.y, 2.0f, 1.0e-4f);

    ASSERT_TRUE(saveSceneToJson(path, data, &err)) << err;
    {
        std::ifstream saved(path, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(saved));
        const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        EXPECT_EQ(text.find("\"light\":"), std::string::npos);
        EXPECT_EQ(text.find("\"model\":"), std::string::npos);
        EXPECT_EQ(text.find("\"stream\":"), std::string::npos);
        EXPECT_EQ(text.find("\"emissive\":"), std::string::npos);
        EXPECT_EQ(text.find("\"LocalLight\""), std::string::npos);
        EXPECT_EQ(text.find("\"Stream\""), std::string::npos);
        EXPECT_EQ(text.find("\"Model\""), std::string::npos);
        EXPECT_EQ(text.find("\"Mesh\""), std::string::npos);
    }
    SceneFileData again{};
    ASSERT_TRUE(loadSceneFromJson(path, again, &err)) << err;
    ASSERT_EQ(again.objects.size(), 4u);
    EXPECT_NEAR(sceneLightFloat(again.objects[0], "intensity", 0.0f), 1885.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(again.objects[0], "range", 0.0f), 8.0f, 1.0e-4f);
    EXPECT_TRUE(sceneModelPath(again.objects[1]).empty());
    EXPECT_NEAR(sceneEmissive(again.objects[2]), 0.0f, 1.0e-4f);
    EXPECT_TRUE(sceneIsPlaceholder(again.objects[3]));
    EXPECT_EQ(again.version, 2);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, OldPayloadKeysDoNotWin)
{
    const auto path = tempScenePath("darkengine6_scene_override_wins_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 3,
  "name": "override_wins",
  "objects": [
    {
      "archetype": "core:point_light",
      "light": {"intensity": 4, "range": 3, "enabled": true, "castShadow": true},
      "overrides": {
        "LocalLight": {"kind": "point", "intensity": 12, "range": 9, "enabled": false, "castShadow": false, "useObjectColor": true}
      }
    },
    {
      "archetype": "core:spawner",
      "spawner": {"mode": "maintain", "unit": "hunter", "count": 3, "seed": 1},
      "overrides": {"Spawner": {"mode": "waves", "unit": "wolf", "count": 6, "seed": 99}}
    }
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "intensity", 0.0f), 12.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(data.objects[0], "range", 0.0f), 9.0f, 1.0e-4f);
    EXPECT_FALSE(sceneLightBool(data.objects[0], "enabled", true));
    EXPECT_FALSE(sceneLightBool(data.objects[0], "castShadow", true));
    SpawnerDesc loaded;
    ASSERT_TRUE(sceneReadSpawner(data.objects[1], loaded));
    EXPECT_EQ(loaded.mode, SpawnerMode::Waves);
    EXPECT_EQ(loaded.unit, SpawnerUnit::Wolf);
    EXPECT_EQ(loaded.count, 6);
    EXPECT_EQ(loaded.seed, 99u);
    EXPECT_NE(data.objects[0].overridesJson.find("LocalLight"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, CustomOverrideSurvivesRoundTrip)
{
    SceneFileData in{};
    in.version = 3;
    in.name    = "ut_custom_override";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData cube{};
    cube.archetype          = "core:cube";
    cube.overridesJson = R"({"Health":{"max":40}})";
    in.objects.push_back(cube);

    SceneObjectData point{};
    point.archetype     = "core:point_light";
    point.overridesJson = R"({"LocalLight":{"kind":"point","intensity":99,"range":4,"enabled":true,"castShadow":false},"Tag":{"name":"lamp"}})";
    in.objects.push_back(point);

    const auto path = tempScenePath("darkengine6_scene_custom_override_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 2u);
    EXPECT_NE(out.objects[0].overridesJson.find("\"Health\""), std::string::npos);
    EXPECT_NE(out.objects[0].overridesJson.find("40"), std::string::npos);
    EXPECT_EQ(out.objects[0].overridesJson.find("LocalLight"), std::string::npos);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "intensity", 0.0f), 99.0f, 1.0e-4f);
    EXPECT_NEAR(sceneLightFloat(out.objects[1], "range", 0.0f), 4.0f, 1.0e-4f);
    EXPECT_NE(out.objects[1].overridesJson.find("\"Tag\""), std::string::npos);
    EXPECT_NE(out.objects[1].overridesJson.find("LocalLight"), std::string::npos);

    ASSERT_TRUE(saveSceneToJson(path, out, &err)) << err;
    SceneFileData again{};
    ASSERT_TRUE(loadSceneFromJson(path, again, &err)) << err;
    EXPECT_NE(again.objects[0].overridesJson.find("\"Health\""), std::string::npos);
    EXPECT_NEAR(sceneLightFloat(again.objects[1], "intensity", 0.0f), 99.0f, 1.0e-4f);
    EXPECT_NE(again.objects[1].overridesJson.find("\"Tag\""), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_F(SceneFile, EditorSaveKeepsDiffsAndUnknownKeys)
{
    using json = nlohmann::json;

    SceneObjectData model;
    model.archetype     = "core:model";
    model.overridesJson = R"({"Model":{"gltf":"models/custom.gltf","anim":"models/custom.anim.json"}})";
    sceneWriteModelDiff(model, "models/custom.gltf");
    json modelBag = json::parse(model.overridesJson, nullptr, false);
    ASSERT_TRUE(modelBag.is_object());
    ASSERT_TRUE(modelBag.contains("Model") && modelBag["Model"].is_object());
    const auto* modelGltf = modelBag["Model"]["gltf"].get_ptr<const json::string_t*>();
    const auto* modelAnim = modelBag["Model"]["anim"].get_ptr<const json::string_t*>();
    ASSERT_NE(modelGltf, nullptr);
    ASSERT_NE(modelAnim, nullptr);
    EXPECT_EQ(*modelGltf, "models/custom.gltf");
    EXPECT_EQ(*modelAnim, "models/custom.anim.json");

    SceneFileData saved{};
    saved.version = 3;
    saved.name    = "ut_editor_diff";
    saved.objects.push_back(model);
    const auto path = tempScenePath("darkengine6_editor_diff_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, saved, &err)) << err;
    SceneFileData loaded{};
    ASSERT_TRUE(loadSceneFromJson(path, loaded, &err)) << err;
    ASSERT_EQ(loaded.objects.size(), 1u);
    EXPECT_NE(loaded.objects[0].overridesJson.find("models/custom.anim.json"), std::string::npos);
    EXPECT_NE(loaded.objects[0].overridesJson.find("models/custom.gltf"), std::string::npos);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    SceneObjectData playerProbe;
    playerProbe.archetype = "core:player";
    const json playerModel = json::parse(sceneArchetypeComponentJson(playerProbe, "Model"), nullptr, false);
    const auto* playerGltf = playerModel.is_object() && playerModel.contains("gltf") ? playerModel["gltf"].get_ptr<const json::string_t*>() : nullptr;
    ASSERT_NE(playerGltf, nullptr);
    SceneObjectData player;
    player.archetype     = "core:player";
    player.overridesJson = std::string(R"({"Model":{"gltf":")") + *playerGltf + R"(","anim":"models/custom.anim.json"}})";
    sceneWriteModelDiff(player, *playerGltf);
    json playerBag = json::parse(player.overridesJson, nullptr, false);
    ASSERT_TRUE(playerBag.contains("Model") && playerBag["Model"].is_object());
    EXPECT_FALSE(playerBag["Model"].contains("gltf"));
    const auto* playerAnim = playerBag["Model"]["anim"].get_ptr<const json::string_t*>();
    ASSERT_NE(playerAnim, nullptr);
    EXPECT_EQ(*playerAnim, "models/custom.anim.json");

    SceneObjectData point;
    point.archetype = "core:point_light";
    SceneLightFields fields;
    fields.kind         = "point";
    fields.intensity    = sceneLightFloat(point, "intensity", 0.0f);
    fields.range        = sceneLightFloat(point, "range", 0.0f);
    fields.innerDeg     = sceneLightFloat(point, "inner", 0.0f);
    fields.outerDeg     = sceneLightFloat(point, "outer", 0.0f);
    fields.sourceRadius = sceneLightFloat(point, "sourceRadius", 0.0f);
    fields.enabled      = sceneLightBool(point, "enabled", false);
    fields.castShadow   = sceneLightBool(point, "castShadow", true);
    sceneWriteLightDiff(point, fields);
    EXPECT_EQ(point.overridesJson.find("LocalLight"), std::string::npos);

    fields.intensity = sceneLightFloat(point, "intensity", 0.0f) + 25.0f;
    sceneWriteLightDiff(point, fields);
    json lightBag = json::parse(point.overridesJson, nullptr, false);
    ASSERT_TRUE(lightBag.contains("LocalLight") && lightBag["LocalLight"].is_object());
    EXPECT_EQ(lightBag["LocalLight"].size(), 1u);
    EXPECT_TRUE(lightBag["LocalLight"].contains("intensity"));

    SceneObjectData spot;
    spot.archetype     = "core:spot_light";
    spot.overridesJson = R"({"LocalLight":{"searchlight":true,"sweep":40.0,"circuit":"gate"}})";
    SceneLightFields spotFields;
    spotFields.kind         = "spot";
    spotFields.intensity    = sceneLightFloat(spot, "intensity", 0.0f);
    spotFields.range        = sceneLightFloat(spot, "range", 0.0f);
    spotFields.innerDeg     = sceneLightFloat(spot, "inner", 0.0f);
    spotFields.outerDeg     = sceneLightFloat(spot, "outer", 0.0f);
    spotFields.sourceRadius = sceneLightFloat(spot, "sourceRadius", 0.0f);
    spotFields.enabled      = sceneLightBool(spot, "enabled", false);
    spotFields.castShadow   = sceneLightBool(spot, "castShadow", true);
    sceneWriteLightDiff(spot, spotFields);
    json spotBag = json::parse(spot.overridesJson, nullptr, false);
    ASSERT_TRUE(spotBag.contains("LocalLight") && spotBag["LocalLight"].is_object());
    const auto* searchlight = spotBag["LocalLight"]["searchlight"].get_ptr<const json::boolean_t*>();
    const auto* circuit     = spotBag["LocalLight"]["circuit"].get_ptr<const json::string_t*>();
    ASSERT_NE(searchlight, nullptr);
    ASSERT_NE(circuit, nullptr);
    EXPECT_TRUE(*searchlight);
    EXPECT_EQ(*circuit, "gate");
    float sweep = 0.0f;
    EXPECT_TRUE(jsonFloatOr(spotBag["LocalLight"], "sweep", sweep, 0.0f, "EditorSaveDiff"));
    EXPECT_NEAR(sweep, 40.0f, 1.0e-4f);
    EXPECT_FALSE(spotBag["LocalLight"].contains("intensity"));

    SceneObjectData emitter;
    emitter.archetype = "core:particle_emitter";
    sceneWriteParticleDiff(emitter, ParticleEmitterDesc{});
    EXPECT_EQ(emitter.overridesJson.find("ParticleEmitter"), std::string::npos);
    ParticleEmitterDesc edited{};
    edited.emissionRate = 12.0f;
    sceneWriteParticleDiff(emitter, edited);
    json emitterBag = json::parse(emitter.overridesJson, nullptr, false);
    ASSERT_TRUE(emitterBag.contains("ParticleEmitter") && emitterBag["ParticleEmitter"].is_object());
    EXPECT_EQ(emitterBag["ParticleEmitter"].size(), 1u);
    float rate = 0.0f;
    EXPECT_TRUE(jsonFloatOr(emitterBag["ParticleEmitter"], "emissionRate", rate, 0.0f, "EditorSaveDiff"));
    EXPECT_NEAR(rate, 12.0f, 1.0e-3f);
}
