#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/LocalShadowMath.h"
#include "Render/LocalShadowSystem.h"
#include "Render/SkinningUploadRing.h"

#include <cmath>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <limits>
#include <vector>
#include <wrl/client.h>

using namespace Dark;
using namespace Dark::Math;

namespace
{

Vector3f ndc(const Matrix4f& vp, const Vector3f& point)
{
    const Vector4f clip = vp * Vector4f(point, 1.0f);
    return Vector3f(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
}

void makeView(Camera3D& cam, Frustum3f& frustum)
{
    cam.SetPosition(Vector3f(0.0f, 0.0f, 0.0f));
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    cam.SetLens(DegreesToRadians(60.0f), 2560.0f / 1600.0f, 0.18f, 2000.0f);
    frustum = Frustum3f(cam.GetCullViewProj());
}

Entity addShadowLight(World& world, LocalLightType type, float intensity)
{
    Entity e = world.createEntity();
    auto& xf = world.emplace<TransformComponent>(e);
    xf.position = Vector3f(0.0f, 1.0f, 5.0f);
    xf.rotation = Quaternion::IDENTITY;
    auto& light = world.emplace<LocalLightComponent>(e);
    light.type         = type;
    light.color        = Vector3f(1.0f, 1.0f, 1.0f);
    light.intensity    = intensity;
    light.range        = 8.0f;
    light.outerConeDeg = 25.0f;
    light.enabled      = true;
    light.castShadow   = true;
    return e;
}

Microsoft::WRL::ComPtr<ID3D12Device> tryCreateDevice()
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return {};
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
    {
        if (SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            return device;
    }
    device.Reset();
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return device;
    return {};
}

} // namespace

TEST(LocalShadowMath, SpotOnAxisIsCentered)
{
    const Matrix4f vp = buildSpotShadowViewProj(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 10.0f, 25.0f, 0.05f);
    const Vector3f n  = ndc(vp, Vector3f(0.0f, 0.0f, 5.0f));
    EXPECT_NEAR(n.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(n.y, 0.0f, 1.0e-3f);
    EXPECT_GT(n.z, 0.0f);
    EXPECT_LT(n.z, 1.0f);
}

TEST(LocalShadowMath, PointFaceCentersAndPlusYUp)
{
    const Vector3f pos(2.0f, 3.0f, 4.0f);
    const Vector3f axes[kLocalShadowPointFaces] = {
        Vector3f(1.0f, 0.0f, 0.0f),
        Vector3f(-1.0f, 0.0f, 0.0f),
        Vector3f(0.0f, 1.0f, 0.0f),
        Vector3f(0.0f, -1.0f, 0.0f),
        Vector3f(0.0f, 0.0f, 1.0f),
        Vector3f(0.0f, 0.0f, -1.0f),
    };
    for (int face = 0; face < kLocalShadowPointFaces; ++face)
    {
        const Matrix4f vp = buildPointFaceViewProj(pos, face, 10.0f, 0.05f);
        const Vector3f n  = ndc(vp, pos + axes[face] * 2.0f);
        EXPECT_NEAR(n.x, 0.0f, 1.0e-3f) << face;
        EXPECT_NEAR(n.y, 0.0f, 1.0e-3f) << face;
        EXPECT_GT(n.z, 0.0f) << face;
        EXPECT_LT(n.z, 1.0f) << face;
        EXPECT_EQ(selectPointShadowFace(axes[face]), face);
    }

    // +Y looks along +Y with cubemap up (0,0,-1), so that offset is +NDC Y.
    const Matrix4f upFace = buildPointFaceViewProj(Vector3f(0.0f, 0.0f, 0.0f), 2, 10.0f, 0.05f);
    const Vector3f raised = ndc(upFace, Vector3f(0.0f, 2.0f, -0.25f));
    EXPECT_GT(raised.y, 0.0f);
}

TEST(LocalShadowMath, NdcBiasClampsAtFourTexels)
{
    const float cap   = 4.0f / 1024.0f;
    const float close = localShadowNdcBias(0.05f, 0.05f, 10.0f, 0.2f, 1024.0f);
    const float mid   = localShadowNdcBias(0.05f, 0.05f, 10.0f, 8.0f, 1024.0f);
    EXPECT_NEAR(close, cap, 1.0e-6f);
    EXPECT_TRUE(std::isfinite(mid));
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, cap);
}

TEST(LocalShadowAlloc, FourSpotsFitFifthDoesNot)
{
    World world;
    Entity lights[5];
    for (int i = 0; i < 5; ++i)
        lights[i] = addShadowLight(world, LocalLightType::Spot, 50.0f - static_cast<float>(i));

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    EXPECT_TRUE(shadows.debugEnabled());
    shadows.update(world, cam, frustum, 0, true);

    EXPECT_EQ(shadows.faceCountThisFrame(), 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_GE(shadows.recordFor(lights[i]), 0);
    EXPECT_EQ(shadows.recordFor(lights[4]), -1);
}

TEST(LocalShadowAlloc, TwoPointsFillTheSliceBudget)
{
    World world;
    Entity a = addShadowLight(world, LocalLightType::Point, 30.0f);
    Entity b = addShadowLight(world, LocalLightType::Point, 20.0f);
    Entity c = addShadowLight(world, LocalLightType::Point, 10.0f);

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    shadows.update(world, cam, frustum, 0, true);

    EXPECT_EQ(shadows.faceCountThisFrame(), 12);
    EXPECT_EQ(shadows.recordFor(a), 0);
    EXPECT_EQ(shadows.recordFor(b), 1);
    EXPECT_EQ(shadows.recordFor(c), -1);
}

TEST(LocalShadowAlloc, PointThatDoesNotFitLeavesASliceForASpot)
{
    World world;
    Entity spotHi  = addShadowLight(world, LocalLightType::Spot, 100.0f);
    Entity pointHi = addShadowLight(world, LocalLightType::Point, 50.0f);
    Entity pointLo = addShadowLight(world, LocalLightType::Point, 40.0f);
    Entity spotLo  = addShadowLight(world, LocalLightType::Spot, 30.0f);

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    shadows.update(world, cam, frustum, 0, true);

    EXPECT_EQ(shadows.faceCountThisFrame(), 8);
    EXPECT_GE(shadows.recordFor(spotHi), 0);
    EXPECT_GE(shadows.recordFor(pointHi), 0);
    EXPECT_EQ(shadows.recordFor(pointLo), -1);
    EXPECT_GE(shadows.recordFor(spotLo), 0);
}

TEST(LocalShadowAlloc, HysteresisKeepsHolderAcrossABlockedFrame)
{
    World world;
    Entity holder = addShadowLight(world, LocalLightType::Spot, 20.0f);
    addShadowLight(world, LocalLightType::Spot, 100.0f);
    addShadowLight(world, LocalLightType::Spot, 90.0f);
    addShadowLight(world, LocalLightType::Spot, 80.0f);

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    shadows.update(world, cam, frustum, 0, true);
    EXPECT_GE(shadows.recordFor(holder), 0);

    shadows.update(world, cam, frustum, 1, false);
    EXPECT_EQ(shadows.faceCountThisFrame(), 0);
    EXPECT_EQ(shadows.recordFor(holder), -1);
    EXPECT_FLOAT_EQ(shadows.debugSlice(0), 12.0f);

    Entity challenger = addShadowLight(world, LocalLightType::Spot, 21.0f);
    shadows.update(world, cam, frustum, 2, true);
    EXPECT_GE(shadows.recordFor(holder), 0);
    EXPECT_EQ(shadows.recordFor(challenger), -1);
    EXPECT_EQ(shadows.faceCountThisFrame(), 4);
}

TEST(LocalShadowAlloc, DebugFlagOffWritesNothingAndKeepsHolder)
{
    World world;
    Entity holder = addShadowLight(world, LocalLightType::Spot, 20.0f);
    addShadowLight(world, LocalLightType::Spot, 100.0f);
    addShadowLight(world, LocalLightType::Spot, 90.0f);
    addShadowLight(world, LocalLightType::Spot, 80.0f);

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    shadows.update(world, cam, frustum, 0, true);

    shadows.setDebugEnabled(false);
    shadows.update(world, cam, frustum, 1, true);
    EXPECT_EQ(shadows.faceCountThisFrame(), 0);
    EXPECT_EQ(shadows.recordFor(holder), -1);
    EXPECT_FLOAT_EQ(shadows.debugSlice(0), 12.0f);

    shadows.setDebugEnabled(true);
    Entity challenger = addShadowLight(world, LocalLightType::Spot, 21.0f);
    shadows.update(world, cam, frustum, 2, true);
    EXPECT_GE(shadows.recordFor(holder), 0);
    EXPECT_EQ(shadows.recordFor(challenger), -1);
}

TEST(LocalShadowAlloc, NonFinitePositionIsSkipped)
{
    World world;
    Entity bad  = addShadowLight(world, LocalLightType::Spot, 100.0f);
    Entity good = addShadowLight(world, LocalLightType::Spot, 40.0f);
    world.get<TransformComponent>(bad)->position = Vector3f(std::numeric_limits<float>::quiet_NaN(), 1.0f, 5.0f);

    Camera3D cam;
    Frustum3f frustum;
    makeView(cam, frustum);
    LocalShadowSystem shadows;
    shadows.update(world, cam, frustum, 0, true);
    EXPECT_EQ(shadows.recordFor(bad), -1);
    EXPECT_GE(shadows.recordFor(good), 0);
    EXPECT_EQ(shadows.faceCountThisFrame(), 1);
}

TEST(SkinningUploadRing, SamePoseReusesOneSlotPerFrame)
{
    Microsoft::WRL::ComPtr<ID3D12Device> device = tryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    SkinningUploadRing ring;
    ASSERT_TRUE(ring.create(device.Get()));
    ring.beginFrame(0);

    AnimPose first;
    first.boneCount = 1;
    AnimPose second;
    second.boneCount = 1;

    const D3D12_GPU_VIRTUAL_ADDRESS firstVa = ring.alloc(first);
    ASSERT_NE(firstVa, 0u);
    EXPECT_EQ(ring.alloc(first), firstVa);

    const D3D12_GPU_VIRTUAL_ADDRESS secondVa = ring.alloc(second);
    ASSERT_NE(secondVa, 0u);
    EXPECT_NE(secondVa, firstVa);
    EXPECT_EQ(ring.alloc(second), secondVa);

    // first and second already occupy two slots. Thirty more fill the ring.
    std::vector<AnimPose> extra(31);
    for (int i = 0; i < 30; ++i)
    {
        extra[static_cast<size_t>(i)].boneCount = 1;
        EXPECT_NE(ring.alloc(extra[static_cast<size_t>(i)]), 0u);
    }
    EXPECT_EQ(ring.alloc(extra[30]), 0u);
    EXPECT_EQ(ring.alloc(first), firstVa);

    ring.beginFrame(1);
    const D3D12_GPU_VIRTUAL_ADDRESS nextFrame = ring.alloc(first);
    ASSERT_NE(nextFrame, 0u);
    EXPECT_EQ(ring.alloc(first), nextFrame);
    EXPECT_EQ(nextFrame, firstVa + static_cast<UINT64>(SkinningUploadRing::kMaxInstances) * sizeof(BonePaletteCB));
}
