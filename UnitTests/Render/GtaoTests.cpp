#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/GtaoPipeline.h"
#include "Render/SceneBuffers.h"

#include <type_traits>
#include <utility>

using Dark::Camera3D;
using Dark::GtaoSettings;
using Dark::SceneBuffers;
using Dark::Math::Mat4f;
using Dark::Math::Matrix4f;

namespace
{
    template <class T, class = void>
    struct HasHalfRes : std::false_type
    {
    };

    template <class T>
    struct HasHalfRes<T, std::void_t<decltype(std::declval<T&>().halfRes)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasThickness : std::false_type
    {
    };

    template <class T>
    struct HasThickness<T, std::void_t<decltype(std::declval<T&>().thickness)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasSteps : std::false_type
    {
    };

    template <class T>
    struct HasSteps<T, std::void_t<decltype(std::declval<T&>().steps)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasDirections : std::false_type
    {
    };

    template <class T>
    struct HasDirections<T, std::void_t<decltype(std::declval<T&>().directions)>> : std::true_type
    {
    };
} // namespace

TEST(Gtao, Settings_Defaults)
{
    const GtaoSettings s{};
    EXPECT_FALSE(s.enabled);
    EXPECT_FLOAT_EQ(s.radius, 0.5f);
    EXPECT_FLOAT_EQ(s.power, 1.5f);
    EXPECT_FLOAT_EQ(s.intensity, 1.0f);
    EXPECT_FALSE(HasHalfRes<GtaoSettings>::value);
    EXPECT_FALSE(HasThickness<GtaoSettings>::value);
    EXPECT_FALSE(HasSteps<GtaoSettings>::value);
    EXPECT_FALSE(HasDirections<GtaoSettings>::value);
}

TEST(Gtao, LightingCount_Unchanged)
{
    EXPECT_EQ(SceneBuffers::kLightingCount, 9u);
    EXPECT_EQ(SceneBuffers::kLightingAo, 5u);
}

TEST(Gtao, SetLightingAoSrv_NullDevice)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
}

TEST(Gtao, SetLightingAoSrv_ZeroHandle)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Reset_ClearsLightingAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
    buffers.reset();
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Create_NullDevice_DropsAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);

    const float hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    EXPECT_FALSE(buffers.create(nullptr, 64, 64, true, {}, hdrClear));
    EXPECT_FALSE(buffers.valid());
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Create_ZeroSize_DropsAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
    EXPECT_FALSE(buffers.create(nullptr, 0, 0, true, {}, nullptr));
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Camera3D_GetProjUnjittered)
{
    Camera3D cam;
    cam.SetLens(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    Matrix4f unjittered = cam.GetProjUnjittered();
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProj().m_afEntry[i], unjittered.m_afEntry[i]);

    cam.SetSubpixelJitter(0.5f, -0.5f, 1920, 1080);
    EXPECT_NEAR(cam.GetProj().m_afEntry[Mat4f::m31], unjittered.m_afEntry[Mat4f::m31] + 2.0f * 0.5f / 1920.0f, 1e-6f);
    EXPECT_NEAR(cam.GetProj().m_afEntry[Mat4f::m32], unjittered.m_afEntry[Mat4f::m32] + 2.0f * -0.5f / 1080.0f, 1e-6f);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProjUnjittered().m_afEntry[i], unjittered.m_afEntry[i]);

    cam.ClearSubpixelJitter();
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProj().m_afEntry[i], cam.GetProjUnjittered().m_afEntry[i]);
}
