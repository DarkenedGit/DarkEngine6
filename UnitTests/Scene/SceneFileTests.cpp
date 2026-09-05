#include <gtest/gtest.h>

#include "Scene/SceneFile.h"

#include <filesystem>
#include <fstream>

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
    EXPECT_TRUE(isScene3DType(SceneObjectType::PointLight));
    EXPECT_TRUE(isScene3DType(SceneObjectType::SpotLight));
    EXPECT_STREQ(toString(SceneObjectType::PointLight), "point_light");
    EXPECT_STREQ(toString(SceneObjectType::SpotLight), "spot_light");

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
    EXPECT_NEAR(data.objects[1].lightIntensity, 600.0f, 1.0e-4f);
    EXPECT_NEAR(data.objects[1].position.x, 4.0f, 1.0e-4f);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
