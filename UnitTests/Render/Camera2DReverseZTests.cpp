#include <gtest/gtest.h>

#include "Math/Vector4f.h"
#include "Render/Camera2D.h"

using Dark::Camera2D;
using Dark::Math::Vector4f;

namespace
{
    float worldNdcZ(const Camera2D& cam, float z)
    {
        const Vector4f clip = cam.GetViewProj() * Vector4f(0.0f, 0.0f, z, 1.0f);
        return clip.z / clip.w;
    }
} // namespace

TEST(Camera2D, ReverseOrtho_Z0IsNear)
{
    Camera2D cam;
    EXPECT_FLOAT_EQ(cam.GetNearZ(), 0.0f);
    EXPECT_NEAR(worldNdcZ(cam, 0.0f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(worldNdcZ(cam, cam.GetFarZ()), 0.0f, 1.0e-5f);

    cam.SetClipPlanes(0.0f, 80.0f);
    EXPECT_NEAR(worldNdcZ(cam, 0.0f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(worldNdcZ(cam, 80.0f), 0.0f, 1.0e-5f);
    EXPECT_GT(worldNdcZ(cam, 0.0f), worldNdcZ(cam, 80.0f));
}
