#include <gtest/gtest.h>

#include "Render/InstancedMeshPipeline.h"

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"

#include <dxgi1_4.h>

#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace Dark;
using namespace Dark::Math;

namespace
{

ComPtr<ID3D12Device> tryCreateWarpDevice()
{
    ComPtr<ID3D12Device>  device;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return {};
    ComPtr<IDXGIAdapter> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
    {
        if (SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            return device;
    }
    return {};
}

Vector4f immediateClip(const Matrix4f& world, const Matrix4f& viewProj, const Vector4f& point)
{
    return (world * viewProj) * point;
}

Vector4f instancedClip(const Matrix4f& world, const Matrix4f& viewProj, const Vector4f& point)
{
    return viewProj * (world * point);
}

void expectClip(const Vector4f& immediate, const Vector4f& instanced)
{
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(immediate[i], instanced[i], 1e-3f);
}

// clip() discards when the argument is negative. alphaModeMask at 0.5 stays off.
bool maskKeepsFragment(float albedoA, float alphaCutoff, float alphaModeMask)
{
    if (alphaModeMask > 0.5f)
        return (albedoA - alphaCutoff) >= 0.0f;
    return true;
}

} // namespace

TEST(InstancedMesh, NullDeviceLeavesPipelineInvalid)
{
    InstancedMeshPipeline pipe;
    EXPECT_FALSE(pipe.create(nullptr));
    EXPECT_FALSE(pipe.isValid());
}

TEST(InstancedMesh, WarpCreatesGBufferAndDepthPsos)
{
    ComPtr<ID3D12Device> device = tryCreateWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    InstancedMeshPipeline pipe;
    EXPECT_TRUE(pipe.create(device.Get()));
    EXPECT_TRUE(pipe.isValid());
}

TEST(InstancedMesh, CpuMulMatchesImmediateShader)
{
    const Matrix4f local  = Matrix4f::ScaleMatrixXYZ(2.0f, 0.5f, 4.0f);
    const Matrix4f entity = Matrix4f::TranslationMatrix(10.0f, -3.0f, 1.0f);
    const Matrix4f prevEntity = Matrix4f::TranslationMatrix(-4.0f, 0.5f, 0.0f);
    const Matrix4f baked      = local * entity;
    const Matrix4f prevBaked  = local * prevEntity;
    EXPECT_NE(baked, entity * local);

    const Matrix4f view = Matrix4f::LookAtLHMatrix(Vector3f(0.0f, 2.0f, -6.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f proj = Matrix4f::PerspectiveFovLHReverseMatrix(0.7f, 16.0f / 9.0f, 0.1f, 80.0f);
    const Matrix4f viewProj = view * proj;
    const Matrix4f prevView = Matrix4f::LookAtLHMatrix(Vector3f(1.0f, 3.0f, -5.0f), Vector3f(0.0f, 0.0f, 2.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f prevViewProj = prevView * proj;

    MeshInstanceGpu gpu{};
    std::memcpy(gpu.world, baked.m_afEntry, sizeof(gpu.world));
    std::memcpy(gpu.prevWorld, prevBaked.m_afEntry, sizeof(gpu.prevWorld));
    Matrix4f loaded;
    Matrix4f loadedPrev;
    std::memcpy(loaded.m_afEntry, gpu.world, sizeof(gpu.world));
    std::memcpy(loadedPrev.m_afEntry, gpu.prevWorld, sizeof(gpu.prevWorld));

    const Vector4f corners[] = {
        Vector4f(0.5f, 0.5f, 0.5f, 1.0f),
        Vector4f(-0.5f, 0.2f, 0.8f, 1.0f),
        Vector4f(1.0f, 0.0f, -1.0f, 1.0f),
    };
    for (const Vector4f& corner : corners)
    {
        expectClip(immediateClip(baked, viewProj, corner), instancedClip(loaded, viewProj, corner));
        expectClip(immediateClip(prevBaked, prevViewProj, corner), instancedClip(loadedPrev, prevViewProj, corner));

        const Matrix4f swapped = entity * local;
        const Vector4f wrong   = immediateClip(swapped, viewProj, corner);
        const Vector4f right   = instancedClip(loaded, viewProj, corner);
        float          delta   = 0.0f;
        for (int i = 0; i < 4; ++i)
            delta += std::fabs(wrong[i] - right[i]);
        EXPECT_GT(delta, 1.0f);
    }

    const Vector4f normal = loaded * Vector4f(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(normal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(normal.y, 0.5f, 1e-5f);
    EXPECT_NEAR(normal.z, 0.0f, 1e-5f);
    EXPECT_NEAR(normal.w, 0.0f, 1e-5f);

    const Vector4f inverseTranspose = Matrix4f::ScaleMatrixXYZ(0.5f, 2.0f, 0.25f) * Vector4f(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_GT(std::fabs(inverseTranspose.y - normal.y), 1.0f);

    EXPECT_FALSE(maskKeepsFragment(0.2f, 0.5f, 1.0f));
    EXPECT_TRUE(maskKeepsFragment(0.5f, 0.5f, 1.0f));
    EXPECT_TRUE(maskKeepsFragment(0.9f, 0.5f, 1.0f));
    EXPECT_TRUE(maskKeepsFragment(0.0f, 0.9f, 0.5f));
    EXPECT_FALSE(maskKeepsFragment(0.0f, 0.1f, 0.51f));
}
