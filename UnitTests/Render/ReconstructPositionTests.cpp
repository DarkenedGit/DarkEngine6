#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"

using Dark::Camera3D;
using Dark::Math::Matrix4f;
using Dark::Math::Vector3f;
using Dark::Math::Vector4f;

namespace
{
    Vector3f reconstruct(const Matrix4f& invViewProj, float ndcX, float ndcY, float depth)
    {
        const Vector4f w = invViewProj * Vector4f(ndcX, ndcY, depth, 1.0f);
        return w.xyz() * (1.0f / w.w);
    }
} // namespace

TEST(ReconstructPosition, MatchesScreenPointToRayNdc)
{
    Camera3D cam;
    cam.SetLens(60.0f * Dark::Math::DegToRad, 16.0f / 9.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(4.0f, 6.0f, -12.0f), Vector3f(0.0f, 2.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    const float vw = 2560.0f;
    const float vh = 1600.0f;
    const float screenX = 640.0f;
    const float screenY = 400.0f;
    const float ndcX = (screenX / vw) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (screenY / vh) * 2.0f;
    const float depth = 0.37f;

    const Matrix4f inv = cam.GetViewProj().Inverse();
    const Vector3f world = reconstruct(inv, ndcX, ndcY, depth);

    const Dark::Math::Ray3f ray = cam.ScreenPointToRay(screenX, screenY, vw, vh);
    const Vector3f to = world - ray.Origin;
    const float along = to.Dot(ray.Direction);
    const Vector3f closest = ray.Origin + ray.Direction * along;
    const Vector3f err = world - closest;
    EXPECT_LT(err.Magnitude(), 1.0e-3f);
}

TEST(ReconstructPosition, NearAndFarOnRay)
{
    Camera3D cam;
    cam.SetLens(60.0f * Dark::Math::DegToRad, 1.0f, 0.18f, 2000.0f);
    cam.SetPosition(0.0f, 2.0f, -8.0f);

    const Matrix4f inv = cam.GetViewProj().Inverse();
    const Vector3f nearP = reconstruct(inv, 0.0f, 0.0f, 0.0f);
    const Vector3f farP  = reconstruct(inv, 0.0f, 0.0f, 1.0f);
    const Dark::Math::Ray3f ray = cam.ScreenPointToRay(0.5f * 100.0f, 0.5f * 100.0f, 100.0f, 100.0f);

    const Vector3f nErr = nearP - ray.Origin;
    EXPECT_LT(nErr.Magnitude(), 1.0e-3f);

    Vector3f farDir = farP - nearP;
    farDir.Normalize();
    EXPECT_NEAR(farDir.Dot(ray.Direction), 1.0f, 1.0e-4f);
}
