#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Render/SceneRenderer.h"

using namespace Dark;

TEST(SceneRenderer, DiscardTemporalHistoryDropsPreviousView)
{
    SceneRenderer scene;
    Camera3D camera;
    EXPECT_FALSE(scene.havePrevViewProj());

    scene.endCameraFrame(camera, camera.GetViewProj());
    EXPECT_TRUE(scene.havePrevViewProj());

    scene.discardTemporalHistory();
    EXPECT_FALSE(scene.havePrevViewProj());
}
