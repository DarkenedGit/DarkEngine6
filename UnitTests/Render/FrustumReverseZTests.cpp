#include <gtest/gtest.h>

#include "Math/Vector3f.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"

using Dark::Camera3D;
using Dark::Frustum3f;
using Dark::Math::Vector3f;

TEST(Frustum3f, ReverseZ_PointInsideNearFar)
{
    Camera3D cam;
    cam.SetLens(1.04719755f, 1.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));

    const Frustum3f cull(cam.GetCullViewProj());
    const Frustum3f raster(cam.GetViewProj());

    const Vector3f inFront(0.0f, 0.0f, 1.0f);
    const Vector3f behind(0.0f, 0.0f, -1.0f);
    const Vector3f beyondFar(0.0f, 0.0f, cam.GetFarZ() + 50.0f);

    EXPECT_TRUE(cull.Contains(inFront));
    EXPECT_FALSE(cull.Contains(behind));
    EXPECT_FALSE(cull.Contains(beyondFar));

    EXPECT_TRUE(raster.Contains(inFront));
    EXPECT_FALSE(raster.Contains(behind));
    EXPECT_TRUE(raster.Contains(beyondFar));
}
