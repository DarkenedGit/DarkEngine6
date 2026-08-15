#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Sky/Environment.h"

using namespace Dark::Math;
using namespace Dark::Sky;

TEST(Environment, NoonSunIsHigh)
{
    Vector3f sun, moon;
    float    elev = 0.0f;
    Environment::sunMoonDirections(12.0f, 172.0f, 47.6f, sun, moon, elev);
    EXPECT_GT(elev, 0.9f); // ~65 deg at midsummer 47N
    EXPECT_GT(sun.y, 0.75f);
}

TEST(Environment, MidnightSunIsDown)
{
    Vector3f sun, moon;
    float    elev = 0.0f;
    Environment::sunMoonDirections(0.0f, 172.0f, 47.6f, sun, moon, elev);
    EXPECT_LT(elev, 0.0f);
    EXPECT_LT(sun.y, 0.0f);
}

TEST(Environment, SunsetIsRedderThanNoon)
{
    Environment noon;
    noon.timeOfDay = 12.0f;
    noon.weather   = WeatherState::Clear();
    noon.evaluate();

    Environment dusk;
    dusk.timeOfDay = 19.6f;
    dusk.dayOfYear = 172.0f;
    dusk.weather   = WeatherState::Clear();
    dusk.evaluate();

    const Vector3f zh = noon.skyZenith();
    const Vector3f hh = dusk.skyHorizon();
    // Horizon at dusk should have more red relative to blue than noon zenith.
    const float noonRB = zh.x / Max(zh.z, 1.0e-3f);
    const float duskRB = hh.x / Max(hh.z, 1.0e-3f);
    EXPECT_GT(duskRB, noonRB);
    EXPECT_LT(dusk.lightColor().x + dusk.lightColor().y + dusk.lightColor().z,
              noon.lightColor().x + noon.lightColor().y + noon.lightColor().z);
}

TEST(Environment, OvercastDimsTheSun)
{
    Environment clear;
    clear.timeOfDay = 13.0f;
    clear.weather   = WeatherState::Clear();
    clear.evaluate();

    Environment storm;
    storm.timeOfDay = 13.0f;
    storm.weather   = WeatherState::Storm();
    storm.evaluate();

    const float c = clear.lightColor().x + clear.lightColor().y + clear.lightColor().z;
    const float s = storm.lightColor().x + storm.lightColor().y + storm.lightColor().z;
    EXPECT_GT(c, s);
    EXPECT_GT(storm.fogDensity(), clear.fogDensity());
    EXPECT_GT(storm.ambientColor().x + storm.ambientColor().y, 0.0f);
}

TEST(Environment, EvaluateSkyZenithBluerThanHorizonAtNoon)
{
    Environment env;
    env.timeOfDay = 12.0f;
    env.weather   = WeatherState::Clear();
    env.evaluate();

    const Vector3f zen = env.evaluateSky(Vector3f(0.0f, 1.0f, 0.0f));
    const Vector3f hor = env.evaluateSky(Vector3f(1.0f, 0.05f, 0.0f));
    EXPECT_GT(zen.z / Max(zen.x, 1.0e-3f), hor.z / Max(hor.x, 1.0e-3f));
}

TEST(Environment, TickAdvancesClock)
{
    Environment env;
    env.timeOfDay = 10.0f;
    env.timeScale = 2.0f; // 2 hours per second
    env.tick(0.5f);
    EXPECT_NEAR(env.timeOfDay, 11.0f, 1.0e-3f);
}

TEST(Environment, SunRadiusGrowsNearHorizon)
{
    const float high = Environment::sunAngularRadius(1.1f);  // ~63°
    const float low  = Environment::sunAngularRadius(0.02f); // on the horizon
    EXPECT_GT(low, high * 1.8f);
    EXPECT_NEAR(high, 0.0046f, 5.0e-4f);
}

TEST(Environment, LimbDarkeningCenterBrighterThanEdge)
{
    Environment env;
    env.timeOfDay = 12.0f;
    env.weather   = WeatherState::Clear();
    env.evaluate();

    const Vector3f sun = env.sunDir();
    Vector3f       axis = sun.Cross(Vector3f(0.0f, 1.0f, 0.0f));
    if (axis.Magnitude() < 1.0e-4f)
        axis = sun.Cross(Vector3f(1.0f, 0.0f, 0.0f));
    axis.Normalize();

    const float    radius = Environment::sunAngularRadius(env.sunElevation());
    const Vector3f center = env.evaluateSky(sun);
    const Vector3f limb   = env.evaluateSky(sun + axis * (radius * 0.72f));

    const float lumC = 0.2126f * center.x + 0.7152f * center.y + 0.0722f * center.z;
    const float lumL = 0.2126f * limb.x + 0.7152f * limb.y + 0.0722f * limb.z;
    EXPECT_GT(lumC, lumL * 1.05f);
}

TEST(Environment, SunsetGlowBandTowardTheSun)
{
    Environment env;
    env.timeOfDay = 19.6f;
    env.dayOfYear = 172.0f;
    env.weather   = WeatherState::Clear();
    env.evaluate();

    Vector3f sunH(env.sunDir().x, 0.0f, env.sunDir().z);
    sunH.Normalize();
    // Sample the glow off the disc so limb/sun white does not dominate the ratio.
    const Vector3f perp(-sunH.z, 0.0f, sunH.x);
    Vector3f toward = sunH * 0.55f + perp * 0.84f;
    toward.y = 0.05f;
    Vector3f away = sunH * -1.0f;
    away.y = 0.05f;
    Vector3f high = toward;
    high.y = 0.55f;

    const Vector3f band = env.evaluateSky(toward);
    const Vector3f back = env.evaluateSky(away);
    const Vector3f up   = env.evaluateSky(high);

    const float bandRB = band.x / Max(band.z, 1.0e-3f);
    const float upRB   = up.x / Max(up.z, 1.0e-3f);
    EXPECT_GT(bandRB, upRB);
    EXPECT_GT(band.x + band.y, back.x + back.y);
}
