#include <gtest/gtest.h>

#include "Editor/LocalLightAuthoring.h"

using namespace Dark;

TEST(LocalLightAuthoring, CopiesShadowFlagAndLeavesCones)
{
    LocalLightComponent light{};
    light.innerConeDeg = 12.0f;
    light.outerConeDeg = 25.0f;

    SceneObjectData authored{};
    SceneLightFields fields;
    fields.kind         = "point";
    fields.castShadow   = true;
    fields.intensity    = 10.0f;
    fields.range        = 4.0f;
    fields.sourceRadius = 0.2f;
    fields.enabled      = false;
    fields.innerDeg     = 40.0f;
    fields.outerDeg     = 70.0f;
    sceneWriteLight(authored, fields);

    copyAuthoredLocalLight(light, authored);

    EXPECT_TRUE(light.castShadow);
    EXPECT_FLOAT_EQ(light.intensity, 10.0f);
    EXPECT_FLOAT_EQ(light.range, 4.0f);
    EXPECT_FLOAT_EQ(light.sourceRadius, 0.2f);
    EXPECT_FALSE(light.enabled);
    EXPECT_FLOAT_EQ(light.innerConeDeg, 12.0f);
    EXPECT_FLOAT_EQ(light.outerConeDeg, 25.0f);
}
