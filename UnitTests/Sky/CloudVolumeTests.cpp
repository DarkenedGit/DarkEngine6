#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Render/CloudVolumePipeline.h"
#include "Render/ShaderCompile.h"
#include "Scene/SceneFile.h"
#include "Sky/CloudVolume.h"

#include <d3d12.h>
#include <filesystem>
#include <wrl/client.h>

using namespace Dark;
using namespace Dark::Math;
using Microsoft::WRL::ComPtr;

TEST(CloudVolume, GpuStrideIs128)
{
    EXPECT_EQ(sizeof(GpuCloudVolume), 128u);
}

TEST(CloudVolume, PackDisabledFails)
{
    TransformComponent xf{};
    xf.position = Vector3f(1.0f, 2.0f, 3.0f);
    xf.scale    = Vector3f(10.0f, 4.0f, 8.0f);
    CloudVolumeDesc desc{};
    desc.enabled = false;
    GpuCloudVolume gpu{};
    EXPECT_FALSE(packGpuCloudVolume(xf, desc, gpu));
}

TEST(CloudVolume, PackCopiesShapeAndAlbedo)
{
    TransformComponent xf{};
    xf.position = Vector3f(4.0f, 8.0f, -2.0f);
    xf.scale    = Vector3f(20.0f, 6.0f, 12.0f);
    CloudVolumeDesc desc{};
    desc.shape  = CloudShape::Box;
    desc.albedo = Vector3f(0.5f, 0.6f, 0.7f);
    desc.density = 1.25f;
    GpuCloudVolume gpu{};
    ASSERT_TRUE(packGpuCloudVolume(xf, desc, gpu));
    EXPECT_NEAR(gpu.center[0], 4.0f, 1.0e-4f);
    EXPECT_NEAR(gpu.center[1], 8.0f, 1.0e-4f);
    EXPECT_NEAR(gpu.halfExtents[0], 10.0f, 1.0e-4f);
    EXPECT_NEAR(gpu.halfExtents[1], 3.0f, 1.0e-4f);
    EXPECT_NEAR(gpu.halfExtents[2], 6.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(gpu.shape, 0.0f);
    EXPECT_NEAR(gpu.albedo[0], 0.5f, 1.0e-4f);
    EXPECT_NEAR(gpu.density, 1.25f, 1.0e-4f);
}

TEST(CloudVolume, RayHitsBoxFromOutsideAndInside)
{
    TransformComponent xf{};
    xf.position = Vector3f(0.0f, 10.0f, 0.0f);
    xf.scale    = Vector3f(20.0f, 8.0f, 20.0f);

    Ray3f outside(Vector3f(0.0f, 10.0f, -40.0f), Vector3f(0.0f, 0.0f, 1.0f));
    float t0 = -1.0f;
    float t1 = -1.0f;
    ASSERT_TRUE(intersectCloudVolume(outside, xf, CloudShape::Box, t0, t1));
    EXPECT_GT(t0, 0.0f);
    EXPECT_GT(t1, t0);

    Ray3f inside(Vector3f(0.0f, 10.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f));
    ASSERT_TRUE(intersectCloudVolume(inside, xf, CloudShape::Box, t0, t1));
    EXPECT_NEAR(t0, 0.0f, 1.0e-4f);
    EXPECT_GT(t1, 1.0f);
}

TEST(CloudVolume, RayMissesBox)
{
    TransformComponent xf{};
    xf.position = Vector3f(0.0f, 10.0f, 0.0f);
    xf.scale    = Vector3f(4.0f, 4.0f, 4.0f);
    Ray3f miss(Vector3f(20.0f, 10.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
    float t0 = 0.0f;
    float t1 = 0.0f;
    EXPECT_FALSE(intersectCloudVolume(miss, xf, CloudShape::Box, t0, t1));
}

TEST(CloudVolume, EllipsoidInsideStartsAtZero)
{
    TransformComponent xf{};
    xf.position = Vector3f(0.0f, 20.0f, 0.0f);
    xf.scale    = Vector3f(30.0f, 10.0f, 30.0f);
    Ray3f inside(xf.position, Vector3f(1.0f, 0.0f, 0.0f));
    float t0 = -1.0f;
    float t1 = -1.0f;
    ASSERT_TRUE(intersectCloudVolume(inside, xf, CloudShape::Ellipsoid, t0, t1));
    EXPECT_NEAR(t0, 0.0f, 1.0e-4f);
    EXPECT_NEAR(t1, 15.0f, 0.05f);
}

TEST(CloudVolume, ShapeMaskCenterVsOutside)
{
    TransformComponent xf{};
    xf.position = Vector3f(0.0f, 0.0f, 0.0f);
    xf.scale    = Vector3f(10.0f, 10.0f, 10.0f);
    CloudVolumeDesc desc{};
    desc.shape    = CloudShape::Ellipsoid;
    desc.softness = 0.2f;
    EXPECT_GT(cloudShapeMask(Vector3f(0.0f, 0.0f, 0.0f), xf, desc), 0.8f);
    EXPECT_LT(cloudShapeMask(Vector3f(20.0f, 0.0f, 0.0f), xf, desc), 0.05f);

    desc.shape = CloudShape::Box;
    EXPECT_GT(cloudShapeMask(Vector3f(0.0f, 0.0f, 0.0f), xf, desc), 0.8f);
    EXPECT_LT(cloudShapeMask(Vector3f(20.0f, 0.0f, 0.0f), xf, desc), 0.05f);
}

TEST(CloudVolume, GatherSkipsDisabledAndCaps)
{
    World world;
    for (int i = 0; i < 3; ++i)
    {
        Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = Vector3f(static_cast<float>(i) * 50.0f, 20.0f, 0.0f);
        xf.scale    = Vector3f(10.0f, 4.0f, 10.0f);
        world.emplace<TransformComponent>(e, xf);
        CloudVolumeComponent cloud{};
        cloud.desc.enabled = (i != 1);
        world.emplace<CloudVolumeComponent>(e, cloud);
    }
    CloudVolumeDrawList list{};
    ASSERT_TRUE(gatherCloudVolumes(world, nullptr, list));
    EXPECT_EQ(list.count, 2u);
}

TEST(CloudVolume, SceneDataRoundTrip)
{
    CloudVolumeDesc src{};
    src.shape      = CloudShape::Box;
    src.density    = 1.4f;
    src.coverage   = 0.33f;
    src.albedo     = Vector3f(0.2f, 0.3f, 0.4f);
    src.windDir    = Vector3f(0.0f, 1.0f, 0.0f);
    src.enabled    = true;
    SceneObjectData d{};
    sceneDataFromCloudDesc(src, d);
    EXPECT_TRUE(d.hasCloud);
    EXPECT_EQ(d.cloudShape, 0);
    CloudVolumeDesc dst{};
    cloudDescFromSceneData(d, dst);
    EXPECT_EQ(dst.shape, CloudShape::Box);
    EXPECT_NEAR(dst.density, 1.4f, 1.0e-4f);
    EXPECT_NEAR(dst.coverage, 0.33f, 1.0e-4f);
    EXPECT_NEAR(dst.albedo.y, 0.3f, 1.0e-4f);
}

TEST(CloudVolume, SceneFileJsonRoundTrip)
{
    SceneFileData in{};
    in.version = 2;
    in.name    = "ut_clouds";
    in.mode    = SceneMode::Scene3D;
    SceneObjectData o{};
    o.type       = SceneObjectType::CloudVolume;
    o.position   = Vector3f(3.0f, 12.0f, -4.0f);
    o.scale      = Vector3f(40.0f, 10.0f, 22.0f);
    o.hasCloud   = true;
    o.cloudShape = 0;
    o.cloudDensity = 2.0f;
    o.cloudCoverage = 0.41f;
    o.cloudAlbedo[0] = 0.8f;
    o.cloudAlbedo[1] = 0.85f;
    o.cloudAlbedo[2] = 0.9f;
    in.objects.push_back(o);

    const auto path = std::filesystem::temp_directory_path() / "darkengine6_cloud_ut.json";
    std::string err;
    ASSERT_TRUE(saveSceneToJson(path, in, &err)) << err;
    SceneFileData out{};
    ASSERT_TRUE(loadSceneFromJson(path, out, &err)) << err;
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_EQ(out.objects[0].type, SceneObjectType::CloudVolume);
    EXPECT_TRUE(out.objects[0].hasCloud);
    EXPECT_EQ(out.objects[0].cloudShape, 0);
    EXPECT_NEAR(out.objects[0].cloudDensity, 2.0f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].cloudCoverage, 0.41f, 1.0e-4f);
    EXPECT_NEAR(out.objects[0].position.y, 12.0f, 1.0e-4f);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(CloudVolume, LodDefaultsAndSanitize)
{
    CloudLodSettings lod{};
    EXPECT_TRUE(lod.enabled);
    EXPECT_FLOAT_EQ(lod.detailDistance, 200.0f);
    EXPECT_FLOAT_EQ(lod.fadeDistance, 48.0f);
    EXPECT_FLOAT_EQ(lod.nearStep, 4.0f);
    EXPECT_FLOAT_EQ(lod.farStep, 18.0f);
    EXPECT_EQ(lod.maxNearSteps, 32);
    EXPECT_EQ(lod.maxFarSteps, 16);
    EXPECT_EQ(lod.nearLightSteps, 5);
    EXPECT_EQ(lod.farLightSteps, 2);
    EXPECT_EQ(sizeof(CloudVolumePassConstants), 40u * sizeof(float));

    lod.detailDistance = 0.0f;
    lod.fadeDistance   = -4.0f;
    lod.nearStep       = 100.0f;
    lod.farStep        = 1.0f;
    lod.maxNearSteps   = 0;
    lod.maxFarSteps    = 1000;
    lod.nearLightSteps = 0;
    lod.farLightSteps  = 40;
    sanitizeCloudLod(lod);
    EXPECT_FLOAT_EQ(lod.detailDistance, 1.0f);
    EXPECT_FLOAT_EQ(lod.fadeDistance, 0.0f);
    EXPECT_FLOAT_EQ(lod.nearStep, 64.0f);
    EXPECT_FLOAT_EQ(lod.farStep, 64.0f);
    EXPECT_EQ(lod.maxNearSteps, 1);
    EXPECT_EQ(lod.maxFarSteps, 64);
    EXPECT_EQ(lod.nearLightSteps, 1);
    EXPECT_EQ(lod.farLightSteps, 8);
}

TEST(CloudVolume, ShaderCompiles)
{
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/CloudVolume.hlsl", "VSMain", "vs_5_0", vs));
    ASSERT_TRUE(compileShaderFromContent("shaders/CloudVolume.hlsl", "PSMain", "ps_5_0", ps));
    EXPECT_NE(vs.Get(), nullptr);
    EXPECT_NE(ps.Get(), nullptr);
}
