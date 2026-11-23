#include <gtest/gtest.h>

#include "Render/EnvironmentProvider.h"
#include "Sky/CloudLayer.h"
#include "Sky/Environment.h"

#include <cstddef>
#include <cstring>

// Sky::Environment is the engine's IEnvironmentProvider: what Render code reads must match the
// environment's own outputs.

using namespace Dark;
using namespace Dark::Sky;

namespace
{
    Environment stormyDusk()
    {
        Environment env;
        env.timeOfDay = 19.4f;
        env.weather   = WeatherState::Storm();
        env.evaluate();
        return env;
    }

    void expectSame(const Math::Vector3f& a, const Math::Vector3f& b)
    {
        EXPECT_FLOAT_EQ(a.x, b.x);
        EXPECT_FLOAT_EQ(a.y, b.y);
        EXPECT_FLOAT_EQ(a.z, b.z);
    }

    Environment clearNight()
    {
        Environment env;
        env.timeOfDay = 1.0f;
        env.weather   = WeatherState::Clear();
        env.evaluate();
        return env;
    }
} // namespace

TEST(SkyEnvironmentProvider, LightingIsTheEnvironmentLightSkyAndWeather)
{
    const Environment night = clearNight();
    // The moon lights the night.
    ASSERT_NE(night.lightColor().z, night.sunColor().z);
    ASSERT_NE(night.lightDir().y, night.sunDir().y);
    for (const Environment& env : { stormyDusk(), night })
    {
        const IEnvironmentProvider& p = env;
        const EnvironmentLighting   l = p.lighting();
        expectSame(l.lightDir, env.lightDir());
        expectSame(l.lightColor, env.lightColor());
        expectSame(l.ambientColor, env.ambientColor());
        expectSame(l.skyZenith, env.skyZenith());
        expectSame(l.skyHorizon, env.skyHorizon());
        EXPECT_FLOAT_EQ(l.cloudCoverage, env.weather.cloudCoverage);
        EXPECT_FLOAT_EQ(l.rain, env.weather.rain);
    }
}

TEST(SkyEnvironmentProvider, FogIsTheEnvironmentFog)
{
    Environment env = stormyDusk();
    env.fogAuto     = false;
    env.setFogColor(Math::Vector3f(0.1f, 0.2f, 0.3f));
    env.setFogDensity(0.01f);
    env.setHeightFogDensity(0.02f);
    env.setHeightFogFalloff(0.03f);
    env.setVolumetricFogDensity(0.04f);
    env.setVolumetricFogHeight(17.0f);
    const EnvironmentFog f = static_cast<const IEnvironmentProvider&>(env).fog();
    expectSame(f.color, env.fogColor());
    EXPECT_FLOAT_EQ(f.density, 0.01f);
    EXPECT_FLOAT_EQ(f.heightDensity, 0.02f);
    EXPECT_FLOAT_EQ(f.heightFalloff, 0.03f);
    EXPECT_FLOAT_EQ(f.volumetricDensity, 0.04f);
    EXPECT_FLOAT_EQ(f.volumetricHeight, 17.0f);
}

TEST(SkyEnvironmentProvider, SkyEvalCarriesSunMoonWeatherAndTheCloudDeck)
{
    const Environment env = stormyDusk();
    SkyEvalParams     out;
    std::memset(&out, 0x7f, sizeof(out));
    static_cast<const IEnvironmentProvider&>(env).skyEval(out);
    EXPECT_FLOAT_EQ(out.sunDir[1], env.sunDir().y);
    EXPECT_FLOAT_EQ(out.sunColor[0], env.sunColor().x);
    EXPECT_FLOAT_EQ(out.moonDir[2], env.moonDir().z);
    EXPECT_FLOAT_EQ(out.moonColor[1], env.moonColor().y);
    EXPECT_FLOAT_EQ(out.coverage, env.weather.cloudCoverage);
    EXPECT_FLOAT_EQ(out.turbidity, env.weather.turbidity);
    EXPECT_FLOAT_EQ(out.rain, env.weather.rain);
    EXPECT_FLOAT_EQ(out.windSpeed, env.weather.windSpeed);
    EXPECT_FLOAT_EQ(out.windDir[0], env.weather.windDir.x);
    EXPECT_FLOAT_EQ(out.windDir[1], env.weather.windDir.y);
    EXPECT_FLOAT_EQ(out.sunElevation, env.sunElevation());
    EXPECT_FLOAT_EQ(out.exposure, 1.0f);
    EXPECT_FLOAT_EQ(out.cloudTime, env.timeOfDay);
    for (float pad : out._pad)
        EXPECT_EQ(pad, 0.0f);
    CloudLayerGpu layer{};
    writeCloudLayer(layer, env);
    EXPECT_EQ(std::memcmp(reinterpret_cast<const unsigned char*>(&out) + offsetof(SkyEvalParams, clLightColor), &layer, sizeof(layer)), 0);
}
