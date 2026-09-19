#include <gtest/gtest.h>

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"

using Dark::Camera3D;
using Dark::Frustum3f;
using Dark::Math::Mat4f;
using Dark::Math::Matrix4f;
using Dark::Math::Vector3f;
using Dark::Math::Vector4f;

TEST(Camera3D, GetProj_IsInfiniteReverse)
{
    Camera3D cam;
    cam.SetLens(1.0f, 16.0f / 9.0f, 0.18f, 2000.0f);

    const Matrix4f& proj = cam.GetProj();
    EXPECT_FLOAT_EQ(proj.m_afEntry[Mat4f::m33], 0.0f);
    EXPECT_FLOAT_EQ(proj.m_afEntry[Mat4f::m43], cam.GetNearZ());
    EXPECT_FLOAT_EQ(proj.m_afEntry[Mat4f::m34], 1.0f);
    EXPECT_EQ(cam.GetProj(), cam.GetProjUnjittered());
}

TEST(Camera3D, GetCullProj_UsesFar)
{
    Camera3D cam;
    cam.SetLens(1.0f, 1.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));

    const Matrix4f& cull = cam.GetCullProj();
    EXPECT_NE(cull.m_afEntry[Mat4f::m33], 0.0f);

    const Vector4f nearClip = cull * Vector4f(0.0f, 0.0f, cam.GetNearZ(), 1.0f);
    const Vector4f farClip  = cull * Vector4f(0.0f, 0.0f, cam.GetFarZ(), 1.0f);
    EXPECT_NEAR(nearClip.z / nearClip.w, 1.0f, 1.0e-4f);
    EXPECT_NEAR(farClip.z / farClip.w, 0.0f, 1.0e-4f);

    const Frustum3f frustum(cam.GetCullViewProj());
    const Vector3f  beyond = cam.GetPosition() + cam.GetLook() * (cam.GetFarZ() + 10.0f);
    EXPECT_FALSE(frustum.Contains(beyond));
    EXPECT_TRUE(frustum.Contains(cam.GetPosition() + cam.GetLook() * 1.0f));
}

TEST(Camera3D, Jitter_DoesNotTouchCullProj)
{
    Camera3D cam;
    cam.SetLens(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    const Matrix4f cullBefore = cam.GetCullProj();
    const Matrix4f cullVp     = cam.GetCullViewProj();

    cam.SetSubpixelJitter(0.5f, -0.5f, 1920, 1080);
    EXPECT_EQ(cam.GetCullProj(), cullBefore);
    EXPECT_EQ(cam.GetCullViewProj(), cullVp);
    EXPECT_NE(cam.GetProj(), cam.GetProjUnjittered());
}

TEST(Camera3D, Ortho_CullProjEqualsRasterUnjittered)
{
    Camera3D cam;
    cam.SetOrthographic(20.0f, 10.0f, 0.1f, 100.0f);

    EXPECT_EQ(cam.GetCullProj(), cam.GetProjUnjittered());
    EXPECT_EQ(cam.GetCullProj(), cam.GetProj());

    const Vector4f nearClip = cam.GetCullProj() * Vector4f(0.0f, 0.0f, 0.1f, 1.0f);
    const Vector4f farClip  = cam.GetCullProj() * Vector4f(0.0f, 0.0f, 100.0f, 1.0f);
    EXPECT_GT(nearClip.z / nearClip.w, farClip.z / farClip.w);

    cam.SetSubpixelJitter(0.25f, 0.25f, 800, 600);
    EXPECT_EQ(cam.GetCullProj(), cam.GetProjUnjittered());
    EXPECT_NE(cam.GetProj(), cam.GetProjUnjittered());
}
