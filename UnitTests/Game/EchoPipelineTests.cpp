#include <gtest/gtest.h>

#include "Game/EchoPipeline.h"
#include "Render/RhiDevice.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{

ComPtr<ID3D12Device> createTestDevice()
{
    return Dark::createWarpTestDevice();
}

} // namespace

TEST(EchoPipeline, CreatesSnapshotAndCompositePsos)
{
    ComPtr<ID3D12Device> device = createTestDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    Dark::EchoPipeline echo;
    ASSERT_TRUE(echo.create(device.Get()));
    EXPECT_TRUE(echo.isValid());
    EXPECT_FALSE(echo.hasSnapshot());
}

TEST(EchoPipeline, BuiltinChirpCrossesFortyMetresBeforeTheNextPulse)
{
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kPeriodSeconds, 1.25f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kSpeedMetersPerSec, 50.0f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kRangeMeters, 40.0f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kEdgeMeters, 8.0f);
    EXPECT_LT(Dark::EchoPipeline::kExpandSeconds, Dark::EchoPipeline::kPeriodSeconds);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kRangeMeters / Dark::EchoPipeline::kSpeedMetersPerSec, Dark::EchoPipeline::kExpandSeconds);
    EXPECT_EQ(Dark::EchoPipeline::kSnapshotDivisor, 8u);
}
