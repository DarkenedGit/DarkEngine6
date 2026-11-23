#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/EnvironmentProvider.h"
#include "Render/Fog.h"
#include "Water/WaterPipeline.h"
#include "Math/MathHelper.h"

#include <cstring>

// Render code reads the sky and weather only through IEnvironmentProvider: fog, the water
// constants and the analytic-sky block come from whatever the provider returns.

using namespace Dark;
using namespace Dark::Math;

namespace
{
    struct FakeEnvironment final : IEnvironmentProvider
    {
        EnvironmentLighting lit;
        EnvironmentFog      fogState;
        mutable int         skyEvalCalls = 0;

        FakeEnvironment()
        {
            lit.lightDir      = Vector3f(0.0f, 0.6f, 0.8f);
            lit.lightColor    = Vector3f(0.9f, 0.5f, 0.25f);
            lit.ambientColor  = Vector3f(0.05f, 0.06f, 0.07f);
            lit.skyZenith     = Vector3f(0.1f, 0.2f, 0.3f);
            lit.skyHorizon    = Vector3f(0.4f, 0.5f, 0.6f);
            lit.cloudCoverage = 0.5f;
            lit.rain          = 0.4f;

            fogState.color             = Vector3f(0.3f, 0.35f, 0.4f);
            fogState.density           = 0.011f;
            fogState.heightDensity     = 0.022f;
            fogState.heightFalloff     = 0.033f;
            fogState.volumetricDensity = 0.044f;
            fogState.volumetricHeight  = 21.0f;
        }

        EnvironmentLighting lighting() const override
        {
            return lit;
        }
        EnvironmentFog fog() const override
        {
            return fogState;
        }
        void skyEval(SkyEvalParams& out) const override
        {
            ++skyEvalCalls;
            out = {};
            for (uint32_t i = 0; i < sizeof(SkyEvalParams) / sizeof(float); ++i)
                reinterpret_cast<float*>(&out)[i] = 1.0f + static_cast<float>(i);
        }
    };

    Camera3D testCamera()
    {
        Camera3D cam;
        cam.SetPosition(Vector3f(0.0f, 2.0f, 0.0f));
        cam.LookAt(Vector3f(0.0f, 2.0f, 0.0f), Vector3f(0.0f, 2.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
        cam.SetLens(DegreesToRadians(60.0f), 16.0f / 9.0f, 0.18f, 2000.0f);
        return cam;
    }
} // namespace

TEST(EnvironmentProvider, FogComesFromTheProvider)
{
    const FakeEnvironment env;
    const FogGpu          fog = makeFogGpu(&env, 3.0f, true);
    EXPECT_FLOAT_EQ(fog.fogColor[0], 0.3f);
    EXPECT_FLOAT_EQ(fog.fogColor[1], 0.35f);
    EXPECT_FLOAT_EQ(fog.fogColor[2], 0.4f);
    EXPECT_FLOAT_EQ(fog.fogDensity, 0.011f);
    EXPECT_FLOAT_EQ(fog.heightFogDensity, 0.022f);
    EXPECT_FLOAT_EQ(fog.heightFogFalloff, 0.033f);
    EXPECT_FLOAT_EQ(fog.volumetricFogDensity, 0.044f);
    EXPECT_FLOAT_EQ(fog.volumetricHeight, 21.0f);
    EXPECT_FLOAT_EQ(fog.waterLevel, 3.0f);
    EXPECT_FLOAT_EQ(fog.heightFogHeight, 3.0f);
}

TEST(EnvironmentProvider, NoFogWithoutAProviderOrLighting)
{
    const FakeEnvironment env;
    for (const FogGpu& fog : { makeFogGpu(&env, 3.0f, false), makeFogGpu(nullptr, 3.0f, true) })
    {
        EXPECT_EQ(fog.fogDensity, 0.0f);
        EXPECT_EQ(fog.heightFogDensity, 0.0f);
        EXPECT_EQ(fog.volumetricFogDensity, 0.0f);
        EXPECT_EQ(fog.fogColor[0], 0.0f);
        EXPECT_FLOAT_EQ(fog.waterLevel, 3.0f);
    }
}

TEST(EnvironmentProvider, WaterConstantsUseTheProviderLightSkyAndWeather)
{
    const FakeEnvironment env;
    WaterFrameConstants   cb{};
    float                 wvp[16]{};
    float                 cam[3]{};
    float                 light[3]{ 0.0f, 1.0f, 0.0f };
    const WaterParams     params = defaultWaterParams(2.0f);
    WaterPipeline::fillConstants(cb, wvp, cam, 0.0f, light, params, &env, true);

    EXPECT_FLOAT_EQ(cb.lightDir[1], 0.6f);
    EXPECT_FLOAT_EQ(cb.lightDir[2], 0.8f);
    EXPECT_FLOAT_EQ(cb.skyZenith[2], 0.3f);
    EXPECT_FLOAT_EQ(cb.skyHorizon[0], 0.4f);
    EXPECT_FLOAT_EQ(cb.lightColor[0], 0.9f);
    EXPECT_FLOAT_EQ(cb.ambientColor[2], 0.07f);
    const float dim = 1.0f - 0.35f * 0.5f - 0.15f * 0.4f;
    EXPECT_FLOAT_EQ(cb.deepColor[0], 0.03f * dim);
    EXPECT_FLOAT_EQ(cb.deepColor[2], 0.18f * dim);
    EXPECT_FLOAT_EQ(cb.specPower, 96.0f * (1.0f - 0.55f * 0.5f));
    EXPECT_FLOAT_EQ(cb.fogDensity, 0.011f);
    EXPECT_FLOAT_EQ(cb.volumetricHeight, 21.0f);
    EXPECT_FLOAT_EQ(cb.heightFogHeight, 2.0f);

    WaterFrameConstants plain{};
    WaterPipeline::fillConstants(plain, wvp, cam, 0.0f, light, params, nullptr, true);
    EXPECT_FLOAT_EQ(plain.lightDir[1], 1.0f);
    EXPECT_FLOAT_EQ(plain.deepColor[0], 0.03f);
    EXPECT_FLOAT_EQ(plain.specPower, 96.0f);
}

TEST(EnvironmentProvider, WaterSsrCopiesTheProviderSkyEvalAndZerosWithoutOne)
{
    const FakeEnvironment env;
    const Camera3D        cam = testCamera();
    SsrSettings           settings{};
    settings.enabled = true;

    WaterFrameConstants cb{};
    WaterPipeline::fillSsr(cb, cam, &settings, true, true, &env);
    EXPECT_EQ(env.skyEvalCalls, 1);
    SkyEvalParams expected{};
    env.skyEval(expected);
    EXPECT_EQ(std::memcmp(&cb.skyEval, &expected, sizeof(expected)), 0);

    std::memset(&cb.skyEval, 0x7f, sizeof(cb.skyEval));
    WaterPipeline::fillSsr(cb, cam, &settings, true, true, nullptr);
    const SkyEvalParams zero{};
    EXPECT_EQ(std::memcmp(&cb.skyEval, &zero, sizeof(zero)), 0);
}
