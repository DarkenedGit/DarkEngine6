#include <gtest/gtest.h>

#include "Render/AutoExposure.h"
#include "Render/BloomPipeline.h"
#include "Render/ShaderCompile.h"

#include <cmath>
#include <d3d12.h>

using Dark::AutoExposureResult;
using Dark::AutoExposureSettings;
using Dark::AutoExposureState;
using Dark::ExposureMode;
using Dark::LumaSample;
using Dark::adaptExposure;
using Dark::bloomKneeForExposure;
using Dark::bloomThresholdForExposure;
using Dark::compileShaderFromContent;
using Dark::logAverageLuma;
using Dark::measuredLumaFromSums;
using Dark::resetAutoExposure;
using Microsoft::WRL::ComPtr;

namespace
{

AutoExposureSettings autoSettings()
{
    AutoExposureSettings s;
    s.mode = ExposureMode::Auto;
    return s;
}

} // namespace

TEST(AutoExp, ManualIsArtistic)
{
    AutoExposureState state;
    state.ev = 0.5f;
    AutoExposureSettings s;
    s.mode = ExposureMode::Manual;

    const AutoExposureResult bright = adaptExposure(state, 4.0f, 0.55f, 1.0f, s);
    EXPECT_FLOAT_EQ(bright.evCorrection, 0.0f);
    EXPECT_FLOAT_EQ(bright.finalExposure, 0.55f);
    EXPECT_FLOAT_EQ(state.ev, 0.5f);

    const AutoExposureResult missing = adaptExposure(state, 0.0f, 1.05f, 0.5f, s);
    EXPECT_FLOAT_EQ(missing.evCorrection, 0.0f);
    EXPECT_FLOAT_EQ(missing.finalExposure, 1.05f);
    EXPECT_FLOAT_EQ(state.ev, 0.5f);
}

TEST(AutoExp, MidGreyIsZero)
{
    AutoExposureState state;
    state.ev = 0.75f;
    const AutoExposureResult r = adaptExposure(state, 0.18f, 0.8f, 30.0f, autoSettings());
    EXPECT_NEAR(r.evCorrection, 0.0f, 1.0e-4f);
    EXPECT_NEAR(r.finalExposure, 0.8f, 1.0e-4f);
}

TEST(AutoExp, EvClamp)
{
    AutoExposureSettings s = autoSettings();
    const float          env = 0.8f;

    AutoExposureState dark;
    const AutoExposureResult raised = adaptExposure(dark, 1.0e-3f, env, 30.0f, s);
    EXPECT_NEAR(raised.evCorrection, s.maxEv, 1.0e-4f);
    EXPECT_NEAR(raised.finalExposure, env * std::exp2(s.maxEv), 1.0e-4f);

    AutoExposureState bright;
    const AutoExposureResult dropped = adaptExposure(bright, 100.0f, env, 30.0f, s);
    EXPECT_NEAR(dropped.evCorrection, -s.maxEv, 1.0e-4f);
    EXPECT_NEAR(dropped.finalExposure, env * std::exp2(-s.maxEv), 1.0e-4f);
}

TEST(AutoExp, DtZero_NoChange)
{
    AutoExposureState state;
    state.ev = 0.25f;
    const AutoExposureResult r = adaptExposure(state, 0.05f, 1.0f, 0.0f, autoSettings());
    EXPECT_FLOAT_EQ(state.ev, 0.25f);
    EXPECT_FLOAT_EQ(r.evCorrection, 0.25f);
}

TEST(AutoExp, DarkerRaisesOverTime)
{
    AutoExposureState state;
    AutoExposureSettings s = autoSettings();
    const float measured   = 0.09f; // log2(0.18/0.09) = +1
    float prev             = 0.0f;
    for (int i = 0; i < 5; ++i)
    {
        const AutoExposureResult r = adaptExposure(state, measured, 1.0f, 0.05f, s);
        EXPECT_GT(r.evCorrection, prev);
        EXPECT_LT(r.evCorrection, 1.0f);
        prev = r.evCorrection;
    }
    const AutoExposureResult arrived = adaptExposure(state, measured, 1.0f, 100.0f, s);
    EXPECT_NEAR(arrived.evCorrection, 1.0f, 1.0e-3f);
}

TEST(AutoExp, BrightFallsFasterThanDarkRises)
{
    AutoExposureSettings s = autoSettings();
    const float          dt = 1.0f / 60.0f;
    AutoExposureState    towardDark;
    AutoExposureState    towardBright;
    const AutoExposureResult dark  = adaptExposure(towardDark, 0.045f, 1.0f, dt, s); // target +2 EV
    const AutoExposureResult light = adaptExposure(towardBright, 0.72f, 1.0f, dt, s); // target -2 EV
    EXPECT_GT(dark.evCorrection, 0.0f);
    EXPECT_LT(light.evCorrection, 0.0f);
    EXPECT_LT(std::fabs(dark.evCorrection), std::fabs(light.evCorrection));
}

TEST(AutoExp, MissingSampleHolds)
{
    AutoExposureState state;
    state.ev = 0.4f;
    const AutoExposureResult r = adaptExposure(state, 0.0f, 0.8f, 0.016f, autoSettings());
    EXPECT_FLOAT_EQ(state.ev, 0.4f);
    EXPECT_NEAR(r.evCorrection, 0.4f, 1.0e-6f);
    EXPECT_NEAR(r.finalExposure, 0.8f * std::exp2(0.4f), 1.0e-5f);
    EXPECT_FLOAT_EQ(r.measuredLuma, 0.0f);
}

TEST(AutoExp, EnterAutoResets)
{
    AutoExposureState state;
    state.ev = 1.2f;
    resetAutoExposure(state);
    EXPECT_FLOAT_EQ(state.ev, 0.0f);
}

TEST(AutoExp, LogAverage)
{
    const float expect = std::sqrt(0.18f * 0.72f);
    const LumaSample pair[] = { { 1.0f, 0.18f }, { 1.0f, 0.72f } };
    const LumaSample withZero[] = { { 1.0f, 0.18f }, { 1.0f, 0.72f }, { 0.0f, 100.0f } };
    EXPECT_NEAR(logAverageLuma(pair, 2), expect, 1.0e-5f);
    EXPECT_NEAR(logAverageLuma(withZero, 3), logAverageLuma(pair, 2), 1.0e-6f);

    const float logSum = std::log2(0.18f) + std::log2(0.72f);
    EXPECT_NEAR(measuredLumaFromSums(logSum, 2.0f), expect, 1.0e-5f);
    EXPECT_NEAR(measuredLumaFromSums(logSum / 4.0f, 2.0f / 4.0f), expect, 1.0e-5f);
    EXPECT_FLOAT_EQ(measuredLumaFromSums(1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(measuredLumaFromSums(1.0f, 1.0e-7f), 0.0f);
}

TEST(AutoExp, BloomThreshold)
{
    EXPECT_FLOAT_EQ(bloomThresholdForExposure(1.0f), Dark::BloomPipeline::kThreshold);
    EXPECT_FLOAT_EQ(bloomKneeForExposure(1.0f), Dark::BloomPipeline::kKnee);
    EXPECT_NEAR(bloomThresholdForExposure(2.0f), 0.5f, 1.0e-6f);
    EXPECT_NEAR(bloomKneeForExposure(2.0f), 0.25f, 1.0e-6f);
}

TEST(AutoExp, PipelineCreates)
{
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        GTEST_SKIP() << "no D3D12 device";

    Dark::AutoExposurePipeline pipeline;
    ASSERT_TRUE(pipeline.create(device.Get(), 1280, 720));
    EXPECT_TRUE(pipeline.isValid());
    EXPECT_TRUE(pipeline.resize(device.Get(), 1280, 720));
    EXPECT_FLOAT_EQ(pipeline.readMeasuredLuma(0), 0.0f);
    pipeline.invalidateReadback();
    EXPECT_FLOAT_EQ(pipeline.readMeasuredLuma(1), 0.0f);

    ASSERT_TRUE(pipeline.resize(device.Get(), 800, 600));
    EXPECT_TRUE(pipeline.isValid());
    EXPECT_FLOAT_EQ(pipeline.readMeasuredLuma(0), 0.0f);
}

TEST(AutoExp, ShaderCompiles)
{
    const D3D_SHADER_MACRO reduceMacros[] = {
        { "AE_REDUCE", "1" },
        { nullptr, nullptr },
    };
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> reduce;
    ComPtr<ID3DBlob> down;
    ASSERT_TRUE(compileShaderFromContent("shaders/AutoExposure.hlsl", "VSMain", "vs_5_0", vs));
    ASSERT_TRUE(compileShaderFromContent("shaders/AutoExposure.hlsl", "PSReduce", "ps_5_0", reduce, reduceMacros));
    ASSERT_TRUE(compileShaderFromContent("shaders/AutoExposure.hlsl", "PSDownsample", "ps_5_0", down));
    EXPECT_NE(vs.Get(), nullptr);
    EXPECT_NE(reduce.Get(), nullptr);
    EXPECT_NE(down.Get(), nullptr);
}
