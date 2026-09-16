#include <gtest/gtest.h>

#include "Editor/TranslateGizmo.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/Camera3D.h"

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Dark::Math;

namespace
{
    Camera3D makeCam(const Vector3f& pos)
    {
        Camera3D cam;
        cam.SetLens(Pi * 0.25f, 1.0f, 0.1f, 1000.0f);
        cam.LookAt(pos, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
        return cam;
    }
} // namespace

TEST(TranslateGizmo, WorldLengthGrowsWithDistance)
{
    Camera3D nearCam = makeCam(Vector3f(0.0f, 0.0f, -5.0f));
    Camera3D farCam  = makeCam(Vector3f(0.0f, 0.0f, -20.0f));
    const float nearLen = gizmoWorldLength(nearCam, Vector3f(0.0f, 0.0f, 0.0f), 200.0f, 90.0f);
    const float farLen  = gizmoWorldLength(farCam, Vector3f(0.0f, 0.0f, 0.0f), 200.0f, 90.0f);
    EXPECT_GT(nearLen, 0.0f);
    EXPECT_NEAR(farLen / nearLen, 4.0f, 0.05f);
}

TEST(TranslateGizmo, AxisDragMovesOnlyX)
{
    const Vector3f pos = applyTranslateDrag(
        TranslateGizmoAxis::X, Vector3f(1.0f, 2.0f, 3.0f), Vector3f(1.0f, 2.0f, 3.0f), Vector3f(4.0f, 9.0f, 8.0f), 0.0f);
    EXPECT_FLOAT_EQ(pos.x, 4.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

TEST(TranslateGizmo, AxisDragMovesOnlyY)
{
    const Vector3f pos = applyTranslateDrag(
        TranslateGizmoAxis::Y, Vector3f(1.0f, 2.0f, 3.0f), Vector3f(1.0f, 2.0f, 3.0f), Vector3f(4.0f, 5.5f, 8.0f), 0.0f);
    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 5.5f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

TEST(TranslateGizmo, PlaneDragKeepsUnusedAxis)
{
    const Vector3f pos = applyTranslateDrag(
        TranslateGizmoAxis::XY, Vector3f(0.0f, 0.0f, 4.0f), Vector3f(0.0f, 0.0f, 4.0f), Vector3f(2.0f, -1.0f, 9.0f), 0.0f);
    EXPECT_FLOAT_EQ(pos.x, 2.0f);
    EXPECT_FLOAT_EQ(pos.y, -1.0f);
    EXPECT_FLOAT_EQ(pos.z, 4.0f);
}

TEST(TranslateGizmo, SnapOnMovedAxes)
{
    const Vector3f pos = applyTranslateDrag(
        TranslateGizmoAxis::Y, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.4f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 1.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST(TranslateGizmo, DragPointXAxisFromFront)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -10.0f));
    Ray3f    ray(Vector3f(2.0f, 0.0f, -10.0f), Vector3f(0.0f, 0.0f, 1.0f));
    Vector3f out(0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(translateDragPoint(TranslateGizmoAxis::X, ray, Vector3f(0.0f, 0.0f, 0.0f), cam.GetLook(), out));
    EXPECT_NEAR(out.x, 2.0f, 1.0e-3f);
    EXPECT_NEAR(out.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(out.z, 0.0f, 1.0e-3f);
}

TEST(TranslateGizmo, DragPointYAxisFromFront)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -10.0f));
    Ray3f    ray(Vector3f(0.0f, 3.0f, -10.0f), Vector3f(0.0f, 0.0f, 1.0f));
    Vector3f out(0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(translateDragPoint(TranslateGizmoAxis::Y, ray, Vector3f(0.0f, 0.0f, 0.0f), cam.GetLook(), out));
    EXPECT_NEAR(out.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(out.y, 3.0f, 1.0e-3f);
    EXPECT_NEAR(out.z, 0.0f, 1.0e-3f);
}

TEST(TranslateGizmo, PickXAndYAxes)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -8.0f));
    const float vw = 200.0f;
    const float vh = 200.0f;
    TranslateGizmoStyle style{};
    style.selected = true;
    const Vector3f origin(0.0f, 0.0f, 0.0f);
    const float size = gizmoWorldLength(cam, origin, vh, style.pixelLength);

    Vector2f x0{}, x1{};
    ASSERT_TRUE(projectToScreen(cam, origin + Vector3f(size * 0.18f, 0.0f, 0.0f), vw, vh, x0));
    ASSERT_TRUE(projectToScreen(cam, origin + Vector3f(size, 0.0f, 0.0f), vw, vh, x1));
    const Vector2f xMid((x0.x + x1.x) * 0.5f, (x0.y + x1.y) * 0.5f);
    EXPECT_EQ(pickTranslateGizmo(cam, origin, xMid, vw, vh, style), TranslateGizmoAxis::X);

    Vector2f y0{}, y1{};
    ASSERT_TRUE(projectToScreen(cam, origin + Vector3f(0.0f, size * 0.18f, 0.0f), vw, vh, y0));
    ASSERT_TRUE(projectToScreen(cam, origin + Vector3f(0.0f, size, 0.0f), vw, vh, y1));
    const Vector2f yMid((y0.x + y1.x) * 0.5f, (y0.y + y1.y) * 0.5f);
    EXPECT_EQ(pickTranslateGizmo(cam, origin, yMid, vw, vh, style), TranslateGizmoAxis::Y);
}

TEST(TranslateGizmo, PickXYPlaneWhenSelected)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -8.0f));
    const float vw = 200.0f;
    const float vh = 200.0f;
    TranslateGizmoStyle style{};
    style.selected = true;
    const Vector3f origin(0.0f, 0.0f, 0.0f);
    const float size = gizmoWorldLength(cam, origin, vh, style.pixelLength);
    Vector2f p{};
    ASSERT_TRUE(projectToScreen(cam, origin + Vector3f(size * 0.35f, size * 0.35f, 0.0f), vw, vh, p));
    EXPECT_EQ(pickTranslateGizmo(cam, origin, p, vw, vh, style), TranslateGizmoAxis::XY);
}

TEST(TranslateGizmo, EdgeOnPlanesAreIgnored)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -8.0f));
    EXPECT_FALSE(gizmoPlaneFacingCamera(Vector3f(0.0f, 1.0f, 0.0f), cam.GetLook()));
    EXPECT_FALSE(gizmoPlaneFacingCamera(Vector3f(1.0f, 0.0f, 0.0f), cam.GetLook()));
    EXPECT_TRUE(gizmoPlaneFacingCamera(Vector3f(0.0f, 0.0f, 1.0f), cam.GetLook()));
}

TEST(TranslateGizmo, MissOutsideGizmo)
{
    Camera3D cam = makeCam(Vector3f(0.0f, 0.0f, -8.0f));
    TranslateGizmoStyle style{};
    style.selected = true;
    EXPECT_EQ(pickTranslateGizmo(cam, Vector3f(0.0f, 0.0f, 0.0f), Vector2f(10.0f, 10.0f), 200.0f, 200.0f, style),
              TranslateGizmoAxis::None);
}
