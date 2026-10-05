#include <gtest/gtest.h>

#include "Editor/LocalLightAuthoring.h"

using namespace Dark;

TEST(LocalLightAuthoring, CopiesShadowFlagAndLeavesCones)
{
    LocalLightComponent light{};
    light.innerConeDeg = 12.0f;
    light.outerConeDeg = 25.0f;

    SceneObjectData authored{};
    authored.lightCastShadow   = true;
    authored.lightIntensity    = 10.0f;
    authored.lightRange        = 4.0f;
    authored.lightSourceRadius = 0.2f;
    authored.lightEnabled      = false;
    authored.lightInnerDeg     = 40.0f;
    authored.lightOuterDeg     = 70.0f;

    copyAuthoredLocalLight(light, authored);

    EXPECT_TRUE(light.castShadow);
    EXPECT_FLOAT_EQ(light.intensity, 10.0f);
    EXPECT_FLOAT_EQ(light.range, 4.0f);
    EXPECT_FLOAT_EQ(light.sourceRadius, 0.2f);
    EXPECT_FALSE(light.enabled);
    EXPECT_FLOAT_EQ(light.innerConeDeg, 12.0f);
    EXPECT_FLOAT_EQ(light.outerConeDeg, 25.0f);
}
