#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Scene/SceneFile.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Dark;
using namespace Dark::Math;

namespace
{

std::filesystem::path tempScenePath(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TEST(SceneTypes, Parse2DAnd3D)
{
    SceneObjectType t{};
    EXPECT_TRUE(tryParseSceneObjectType("platform", t));
    EXPECT_EQ(t, SceneObjectType::Platform);
    EXPECT_TRUE(tryParseSceneObjectType("coin", t));
    EXPECT_TRUE(tryParseSceneObjectType("spawn", t));
    EXPECT_TRUE(isScene2DType(SceneObjectType::Platform));
    EXPECT_FALSE(isScene2DType(SceneObjectType::Cube));
    EXPECT_TRUE(isScene3DType(SceneObjectType::Sphere));
    EXPECT_TRUE(tryParseSceneObjectType("point_light", t));
    EXPECT_EQ(t, SceneObjectType::PointLight);
    EXPECT_TRUE(tryParseSceneObjectType("spot_light", t));
    EXPECT_EQ(t, SceneObjectType::SpotLight);
    EXPECT_TRUE(tryParseSceneObjectType("ambient_light", t));
    EXPECT_EQ(t, SceneObjectType::AmbientLight);
    EXPECT_TRUE(tryParseSceneObjectType("directional_light", t));
    EXPECT_EQ(t, SceneObjectType::DirectionalLight);
    EXPECT_TRUE(isScene3DType(SceneObjectType::PointLight));
    EXPECT_TRUE(isScene3DType(SceneObjectType::SpotLight));
    EXPECT_TRUE(isScene3DType(SceneObjectType::AmbientLight));
    EXPECT_TRUE(isScene3DType(SceneObjectType::DirectionalLight));
    EXPECT_TRUE(isGlobalLightType(SceneObjectType::AmbientLight));
    EXPECT_TRUE(isGlobalLightType(SceneObjectType::DirectionalLight));
    EXPECT_FALSE(isGlobalLightType(SceneObjectType::PointLight));
    EXPECT_STREQ(toString(SceneObjectType::PointLight), "point_light");
    EXPECT_STREQ(toString(SceneObjectType::SpotLight), "spot_light");
    EXPECT_STREQ(toString(SceneObjectType::AmbientLight), "ambient_light");
    EXPECT_STREQ(toString(SceneObjectType::DirectionalLight), "directional_light");
    EXPECT_TRUE(tryParseSceneObjectType("player", t));
    EXPECT_EQ(t, SceneObjectType::Player);
    EXPECT_TRUE(tryParseSceneObjectType("hunter", t));
    EXPECT_EQ(t, SceneObjectType::Hunter);
    EXPECT_TRUE(tryParseSceneObjectType("enemy", t));
    EXPECT_EQ(t, SceneObjectType::Hunter);
    EXPECT_TRUE(isScene3DType(SceneObjectType::Player));
    EXPECT_TRUE(isScene3DType(SceneObjectType::Hunter));
    EXPECT_TRUE(isPawnType(SceneObjectType::Player));
    EXPECT_TRUE(isPawnType(SceneObjectType::Hunter));
    EXPECT_FALSE(isPawnType(SceneObjectType::Cube));
    EXPECT_STREQ(toString(SceneObjectType::Player), "player");
    EXPECT_STREQ(toString(SceneObjectType::Hunter), "hunter");

    SceneMode mode{};
    EXPECT_TRUE(tryParseSceneMode("2d", mode));
    EXPECT_EQ(mode, SceneMode::Scene2D);
    EXPECT_TRUE(tryParseSceneMode("3d", mode));
    EXPECT_EQ(mode, SceneMode::Scene3D);
}

TEST(SceneFile, RoundTrip2D)
{
    SceneFileData in{};
    in.version  = 1;
    in.name     = "ut_level2d";
    in.mode     = SceneMode::Scene2D;
    in.worldMin = Vector2f(0.0f, -1.0f);
    in.worldMax = Vector2f(40.0f, 12.0f);

    SceneObjectData plat{};
    plat.type     = SceneObjectType::Platform;
    plat.position = Vector3f(10.0f, 1.0f, 0.0f);
    plat.scale    = Vector3f(8.0f, 2.0f, 1.0f);
    plat.color[0] = 0.5f;
    plat.color[1] = 0.4f;
    plat.color[2] = 0.3f;
    plat.color[3] = 1.0f;
    in.objects.push_back(plat);

    SceneObjectData coin{};
    coin.type     = SceneObjectType::Coin;
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
    EXPECT_EQ(out.objects[0].type, SceneObjectType::Platform);
    EXPECT_NEAR(out.objects[0].position.x, 10.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].scale.x, 8.0f, 1.0e-4f);
    EXPECT_EQ(out.objects[1].type, SceneObjectType::Coin);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, MissingModeDefaultsTo3D)
{
    const auto path = tempScenePath("darkengine6_scene3d_legacy_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({"version":1,"name":"legacy","objects":[{"type":"cube","position":[1,2,3]}]})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.mode, SceneMode::Scene3D);
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_EQ(data.objects[0].type, SceneObjectType::Cube);
    EXPECT_NEAR(data.objects[0].position.y, 2.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, DefaultScenePathUsesScenesFolder)
{
    const auto p = defaultScenePath("level2d.json");
    EXPECT_EQ(p.filename(), "level2d.json");
    EXPECT_EQ(p.parent_path().filename(), "scenes");
}

TEST(SceneFile, V1RoundTripStillLoads)
{
    SceneFileData in{};
    in.version = 1;
    in.name    = "ut_v1";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData cube{};
    cube.type     = SceneObjectType::Cube;
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
    EXPECT_EQ(out.objects[0].type, SceneObjectType::Cube);
    EXPECT_FALSE(out.objects[0].hasLight);
    EXPECT_NEAR(out.objects[0].emissive, 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.objects[0].position.z, 3.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, V2LocalLightRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_v2_lights";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData point{};
    point.type              = SceneObjectType::PointLight;
    point.position          = Vector3f(4.0f, 1.5f, -2.0f);
    point.color[0]          = 1.0f;
    point.color[1]          = 0.92f;
    point.color[2]          = 0.75f;
    point.color[3]          = 1.0f;
    point.hasLight          = true;
    point.lightIntensity    = 600.0f;
    point.lightRange        = 8.0f;
    point.lightInnerDeg     = 12.0f;
    point.lightOuterDeg     = 25.0f;
    point.lightSourceRadius = 0.05f;
    point.lightEnabled      = true;
    in.objects.push_back(point);

    SceneObjectData spot{};
    spot.type              = SceneObjectType::SpotLight;
    spot.position          = Vector3f(0.0f, 2.0f, 1.0f);
    spot.rotation          = Quaternion(0.70710678f, 0.0f, 0.70710678f, 0.0f);
    spot.color[0]          = 0.8f;
    spot.color[1]          = 0.9f;
    spot.color[2]          = 1.0f;
    spot.color[3]          = 1.0f;
    spot.hasLight          = true;
    spot.lightIntensity    = 800.0f;
    spot.lightRange        = 16.0f;
    spot.lightInnerDeg     = 10.0f;
    spot.lightOuterDeg     = 22.0f;
    spot.lightSourceRadius = 0.08f;
    spot.lightEnabled      = false;
    in.objects.push_back(spot);

    SceneObjectData glow{};
    glow.type     = SceneObjectType::Sphere;
    glow.position = Vector3f(1.0f, 0.5f, 0.0f);
    glow.emissive = 1.0f;
    in.objects.push_back(glow);
    in.objects[0].emissiveMeshIndex = 2;

    const auto path = tempScenePath("darkengine6_scene_v2_lights_ut.json");
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;

    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    EXPECT_EQ(out.version, 2);
    ASSERT_EQ(out.objects.size(), 3u);

    EXPECT_EQ(out.objects[0].type, SceneObjectType::PointLight);
    EXPECT_TRUE(out.objects[0].hasLight);
    EXPECT_NEAR(out.objects[0].position.y, 1.5f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].lightIntensity, 600.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].lightRange, 8.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].lightInnerDeg, 12.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].lightOuterDeg, 25.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].lightSourceRadius, 0.05f, 1.0e-4f);
    EXPECT_TRUE(out.objects[0].lightEnabled);
    EXPECT_NEAR(out.objects[0].color[1], 0.92f, 1.0e-4f);
    EXPECT_EQ(out.objects[0].emissiveMeshIndex, 2);

    EXPECT_EQ(out.objects[1].type, SceneObjectType::SpotLight);
    EXPECT_TRUE(out.objects[1].hasLight);
    EXPECT_NEAR(out.objects[1].lightIntensity, 800.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[1].lightRange, 16.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[1].lightInnerDeg, 10.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[1].lightOuterDeg, 22.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[1].lightSourceRadius, 0.08f, 1.0e-4f);
    EXPECT_FALSE(out.objects[1].lightEnabled);
    EXPECT_NEAR(out.objects[1].rotation.y, 0.70710678f, 1.0e-4f);

    EXPECT_EQ(out.objects[2].type, SceneObjectType::Sphere);
    EXPECT_NEAR(out.objects[2].emissive, 1.0f, 1.0e-4f);
    EXPECT_FALSE(out.objects[2].hasLight);
    EXPECT_EQ(out.objects[2].emissiveMeshIndex, -1);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, V1FileStillLoadsAndSkipsUnknownKeepsKnownLights)
{
    const auto path = tempScenePath("darkengine6_scene_v1_mixed_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 1,
  "name": "legacy_mix",
  "objects": [
    {"type": "cube", "position": [1, 2, 3]},
    {"type": "not_a_real_type", "position": [0, 0, 0]},
    {"type": "point_light", "position": [4, 1.5, -2], "color": [1, 0.92, 0.75, 1]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    EXPECT_EQ(data.version, 1);
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_EQ(data.objects[0].type, SceneObjectType::Cube);
    EXPECT_EQ(data.objects[1].type, SceneObjectType::PointLight);
    EXPECT_TRUE(data.objects[1].hasLight);
    EXPECT_NEAR(data.objects[1].lightIntensity, 1885.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[1].position.x, 4.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, MinimalSpotLightGetsSpotDefaults)
{
    const auto path = tempScenePath("darkengine6_scene_spot_min_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "spot_min",
  "objects": [
    {"type": "spot_light", "position": [0, 2, 0], "color": [1, 0.92, 0.75, 1]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 1u);
    EXPECT_EQ(data.objects[0].type, SceneObjectType::SpotLight);
    EXPECT_TRUE(data.objects[0].hasLight);
    EXPECT_NEAR(data.objects[0].lightIntensity, 2513.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[0].lightRange, 16.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[0].lightInnerDeg, 12.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[0].lightOuterDeg, 25.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, UnauthoredGlobalLightsSplitIntensity)
{
    const auto path = tempScenePath("darkengine6_scene_global_min_ut.json");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << R"({
  "version": 2,
  "name": "global_min",
  "objects": [
    {"type": "directional_light", "position": [4, 8, -4]},
    {"type": "ambient_light", "position": [0, 0, 0]}
  ]
})";
    }

    SceneFileData data{};
    std::string err;
    ASSERT_TRUE(loadSceneFromJson(path, data, &err)) << err;
    ASSERT_EQ(data.objects.size(), 2u);
    EXPECT_EQ(data.objects[0].type, SceneObjectType::DirectionalLight);
    EXPECT_TRUE(data.objects[0].hasLight);
    EXPECT_NEAR(data.objects[0].lightIntensity, Pi, 1.0e-5f);
    EXPECT_EQ(data.objects[1].type, SceneObjectType::AmbientLight);
    EXPECT_TRUE(data.objects[1].hasLight);
    EXPECT_NEAR(data.objects[1].lightIntensity, 1.0f, 1.0e-5f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(SceneFile, IblRoundTripRadians)
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

TEST(SceneFile, IblEmptyEnvironmentMeansOff)
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

TEST(SceneFile, IblMissingKeysDefaultPath)
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

TEST(SceneFile, Ibl2DIgnoresKeys)
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

TEST(SceneFile, TerrainRoundTrip)
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

    std::error_code removeEc;
    std::filesystem::remove(path, removeEc);
}

TEST(SceneFile, TerrainUnknownBindLayoutSkipped)
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

TEST(SceneFile, Terrain2DIgnores)
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

TEST(SceneFile, PlayerAndHunterRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_pawns";
    in.mode    = SceneMode::Scene3D;

    SceneObjectData player{};
    player.type     = SceneObjectType::Player;
    player.position = Vector3f(2.0f, 0.5f, -4.0f);
    player.color[0] = 0.35f;
    player.color[1] = 0.85f;
    player.color[2] = 0.72f;
    player.color[3] = 1.0f;
    in.objects.push_back(player);

    SceneObjectData hunter{};
    hunter.type     = SceneObjectType::Hunter;
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
    EXPECT_EQ(out.objects[0].type, SceneObjectType::Player);
    EXPECT_NEAR(out.objects[0].position.x, 2.0f, 1.0e-4f);
    EXPECT_EQ(out.objects[1].type, SceneObjectType::Hunter);
    EXPECT_NEAR(out.objects[1].position.z, 3.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
