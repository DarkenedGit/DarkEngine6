#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/Awareness.h"

#include <string>

using namespace Dark::AI;

namespace
{
    // Bit positions match the field table in AI/Awareness.cpp.
    constexpr uint64_t kFillNearBit        = 1ull << 0;
    constexpr uint64_t kAlertShareBit      = 1ull << 27;
    constexpr uint64_t kWallScaleBit       = 1ull << 30;
    constexpr uint64_t kStanceSprintBit    = 1ull << 35;
    constexpr uint64_t kBodyStandMidBit    = 1ull << 43;
    constexpr uint64_t kBodyWeightBit      = 1ull << 48;

    void expectNearBuiltIn(const AwarenessSettings& s)
    {
        const AwarenessSettings d{};
        EXPECT_NEAR(s.fillNear, d.fillNear, 1.0e-4f);
        EXPECT_NEAR(s.falloff, d.falloff, 1.0e-4f);
        EXPECT_NEAR(s.maxRange, d.maxRange, 1.0e-4f);
        EXPECT_NEAR(s.focusHalfDeg, d.focusHalfDeg, 1.0e-4f);
        EXPECT_NEAR(s.sideHalfDeg, d.sideHalfDeg, 1.0e-4f);
        EXPECT_NEAR(s.sideRange, d.sideRange, 1.0e-4f);
        EXPECT_NEAR(s.sideStill, d.sideStill, 1.0e-4f);
        EXPECT_NEAR(s.sideMoving, d.sideMoving, 1.0e-4f);
        EXPECT_NEAR(s.nearRange, d.nearRange, 1.0e-4f);
        EXPECT_NEAR(s.clearRate, d.clearRate, 1.0e-4f);
        EXPECT_NEAR(s.clearExposure, d.clearExposure, 1.0e-4f);
        EXPECT_NEAR(s.clearBody, d.clearBody, 1.0e-4f);
        EXPECT_NEAR(s.instantRange, d.instantRange, 1.0e-4f);
        EXPECT_NEAR(s.instantExposure, d.instantExposure, 1.0e-4f);
        EXPECT_NEAR(s.instantExposureWary, d.instantExposureWary, 1.0e-4f);
        EXPECT_NEAR(s.proximityRange, d.proximityRange, 1.0e-4f);
        EXPECT_NEAR(s.proximityRate, d.proximityRate, 1.0e-4f);
        EXPECT_NEAR(s.lookBoost, d.lookBoost, 1.0e-4f);
        EXPECT_NEAR(s.evidenceGrace, d.evidenceGrace, 1.0e-4f);
        EXPECT_NEAR(s.drainCalm, d.drainCalm, 1.0e-4f);
        EXPECT_NEAR(s.drainCurious, d.drainCurious, 1.0e-4f);
        EXPECT_NEAR(s.suspiciousFillScale, d.suspiciousFillScale, 1.0e-4f);
        EXPECT_NEAR(s.investigateAfter, d.investigateAfter, 1.0e-4f);
        EXPECT_NEAR(s.investigateSeconds, d.investigateSeconds, 1.0e-4f);
        EXPECT_NEAR(s.alertLoseSeconds, d.alertLoseSeconds, 1.0e-4f);
        EXPECT_NEAR(s.searchSeconds, d.searchSeconds, 1.0e-4f);
        EXPECT_NEAR(s.searchFillScale, d.searchFillScale, 1.0e-4f);
        EXPECT_NEAR(s.alertShareRadius, d.alertShareRadius, 1.0e-4f);
        EXPECT_NEAR(s.reactMeter, d.reactMeter, 1.0e-4f);
        EXPECT_NEAR(s.concealShown, d.concealShown, 1.0e-4f);
        EXPECT_NEAR(s.wallScale, d.wallScale, 1.0e-4f);
        EXPECT_NEAR(s.noiseBase, d.noiseBase, 1.0e-4f);
        EXPECT_NEAR(s.noiseFalloff, d.noiseFalloff, 1.0e-4f);
        EXPECT_NEAR(s.stanceCrouchWalk, d.stanceCrouchWalk, 1.0e-4f);
        EXPECT_NEAR(s.stanceCrouchStill, d.stanceCrouchStill, 1.0e-4f);
        EXPECT_NEAR(s.stanceSprint, d.stanceSprint, 1.0e-4f);
        EXPECT_NEAR(s.stanceWalk, d.stanceWalk, 1.0e-4f);
        EXPECT_NEAR(s.stanceStand, d.stanceStand, 1.0e-4f);
        EXPECT_NEAR(s.stanceCamouflage, d.stanceCamouflage, 1.0e-4f);
        EXPECT_NEAR(s.strideCrouch, d.strideCrouch, 1.0e-4f);
        EXPECT_NEAR(s.strideSprint, d.strideSprint, 1.0e-4f);
        EXPECT_NEAR(s.strideWalk, d.strideWalk, 1.0e-4f);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(s.bodyStandHeight[i], d.bodyStandHeight[i], 1.0e-4f);
            EXPECT_NEAR(s.bodyCrouchHeight[i], d.bodyCrouchHeight[i], 1.0e-4f);
            EXPECT_NEAR(s.bodyWeight[i], d.bodyWeight[i], 1.0e-4f);
        }
    }
}

TEST(AwarenessSettings, OverlayKeepsOmittedKeys)
{
    AwarenessSettings settings{};
    settings.fillNear = 9.0f;
    uint64_t mask = 0;
    const char* text = R"({
        "alertShareRadius": 3.5,
        "wallScale": 0.25,
        "stanceSprint": 1.7,
        "bodyWeight": [0.1, 0.2, 0.7],
        "bodyStandHeight": [0.75, 1.2]
    })";
    ASSERT_TRUE(readAwarenessSettings(text, settings, &mask));
    EXPECT_NEAR(settings.fillNear, 9.0f, 1.0e-5f);
    EXPECT_NEAR(settings.alertShareRadius, 3.5f, 1.0e-4f);
    EXPECT_NEAR(settings.wallScale, 0.25f, 1.0e-4f);
    EXPECT_NEAR(settings.stanceSprint, 1.7f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyWeight[0], 0.1f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyWeight[1], 0.2f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyWeight[2], 0.7f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyStandHeight[0], 0.75f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyStandHeight[1], 1.2f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyStandHeight[2], -0.2f, 1.0e-4f);
    EXPECT_EQ(mask & kFillNearBit, 0ull);
    EXPECT_NE(mask & kAlertShareBit, 0ull);
    EXPECT_NE(mask & kWallScaleBit, 0ull);
    EXPECT_NE(mask & kStanceSprintBit, 0ull);
    EXPECT_NE(mask & kBodyStandMidBit, 0ull);
    EXPECT_EQ(mask & (kBodyStandMidBit << 1), 0ull);
    EXPECT_EQ(mask & (kBodyWeightBit | (kBodyWeightBit << 1) | (kBodyWeightBit << 2)),
        kBodyWeightBit | (kBodyWeightBit << 1) | (kBodyWeightBit << 2));
}

TEST(AwarenessSettings, PartialArrayAndInteger)
{
    AwarenessSettings settings{};
    settings.bodyWeight[0] = 0.1f;
    settings.bodyWeight[1] = 0.2f;
    settings.bodyWeight[2] = 0.3f;
    uint64_t mask = 0;
    ASSERT_TRUE(readAwarenessSettings(R"({"bodyWeight":[0.55],"alertShareRadius":4})", settings, &mask));
    EXPECT_NEAR(settings.bodyWeight[0], 0.55f, 1.0e-4f);
    EXPECT_NEAR(settings.bodyWeight[1], 0.2f, 1.0e-5f);
    EXPECT_NEAR(settings.bodyWeight[2], 0.3f, 1.0e-5f);
    EXPECT_NEAR(settings.alertShareRadius, 4.0f, 1.0e-5f);
    EXPECT_EQ(mask, kBodyWeightBit | kAlertShareBit);
}

TEST(AwarenessSettings, BadVersionLeavesValues)
{
    AwarenessSettings settings{};
    settings.fillNear          = 4.0f;
    settings.alertShareRadius  = 8.0f;
    ASSERT_FALSE(readAwarenessSettings(R"({"version":2,"fillNear":1,"alertShareRadius":1})", settings, nullptr));
    EXPECT_NEAR(settings.fillNear, 4.0f, 1.0e-5f);
    EXPECT_NEAR(settings.alertShareRadius, 8.0f, 1.0e-5f);
}

TEST(AwarenessSettings, EmptyObjectAndRejectedText)
{
    AwarenessSettings settings{};
    settings.fillNear = 4.0f;
    uint64_t mask = 99;
    ASSERT_TRUE(readAwarenessSettings("{}", settings, &mask));
    EXPECT_EQ(mask, 0ull);
    EXPECT_NEAR(settings.fillNear, 4.0f, 1.0e-5f);

    ASSERT_FALSE(readAwarenessSettings("", settings, nullptr));
    ASSERT_FALSE(readAwarenessSettings("[]", settings, nullptr));
    ASSERT_FALSE(readAwarenessSettings("{", settings, nullptr));
    EXPECT_NEAR(settings.fillNear, 4.0f, 1.0e-5f);
}

TEST(AwarenessSettings, SanitizeClampsRatesAndKeepsHeights)
{
    AwarenessSettings settings{};
    settings.alertShareRadius = -3.0f;
    settings.reactMeter       = 1.4f;
    settings.concealShown     = -0.2f;
    settings.wallScale        = -1.0f;
    settings.noiseBase        = -0.5f;
    settings.stanceSprint     = -2.0f;
    settings.strideWalk       = -1.0f;
    settings.clearExposure    = 2.0f;
    settings.bodyWeight[1]    = -0.5f;
    settings.bodyStandHeight[2] = -0.4f;
    sanitizeAwarenessSettings(settings);
    EXPECT_FLOAT_EQ(settings.alertShareRadius, 0.0f);
    EXPECT_FLOAT_EQ(settings.reactMeter, 1.0f);
    EXPECT_FLOAT_EQ(settings.concealShown, 0.0f);
    EXPECT_FLOAT_EQ(settings.wallScale, 0.0f);
    EXPECT_FLOAT_EQ(settings.noiseBase, 0.0f);
    EXPECT_FLOAT_EQ(settings.stanceSprint, 0.0f);
    EXPECT_FLOAT_EQ(settings.strideWalk, 0.0f);
    EXPECT_FLOAT_EQ(settings.clearExposure, 1.0f);
    EXPECT_FLOAT_EQ(settings.bodyWeight[1], 0.0f);
    EXPECT_FLOAT_EQ(settings.bodyStandHeight[2], -0.4f);
}

TEST(AwarenessSettings, MaskCopiesOnlyMarkedFields)
{
    AwarenessSettings dst{};
    AwarenessSettings src{};
    src.fillNear            = 9.0f;
    src.alertShareRadius    = 3.5f;
    src.bodyStandHeight[1]  = 1.2f;
    applyAwarenessMask(dst, src, kAlertShareBit | kBodyStandMidBit);
    EXPECT_NEAR(dst.fillNear, 1.8f, 1.0e-5f);
    EXPECT_NEAR(dst.alertShareRadius, 3.5f, 1.0e-5f);
    EXPECT_NEAR(dst.bodyStandHeight[0], 0.75f, 1.0e-5f);
    EXPECT_NEAR(dst.bodyStandHeight[1], 1.2f, 1.0e-5f);
    EXPECT_NEAR(dst.bodyStandHeight[2], -0.2f, 1.0e-5f);
}

TEST(AwarenessSettings, WriteRoundTrip)
{
    AwarenessSettings settings{};
    settings.wallScale    = 0.25f;
    settings.stanceSprint = 1.7f;
    std::string text;
    ASSERT_TRUE(writeAwarenessSettings(settings, kWallScaleBit | kStanceSprintBit, text));

    AwarenessSettings out{};
    out.wallScale    = 1.0f;
    out.stanceSprint = 1.0f;
    out.fillNear     = 9.0f;
    uint64_t mask = 0;
    ASSERT_TRUE(readAwarenessSettings(text, out, &mask));
    EXPECT_NEAR(out.wallScale, 0.25f, 1.0e-5f);
    EXPECT_NEAR(out.stanceSprint, 1.7f, 1.0e-5f);
    EXPECT_NEAR(out.fillNear, 9.0f, 1.0e-5f);
    EXPECT_EQ(mask, kWallScaleBit | kStanceSprintBit);
}

TEST(AwarenessSettings, ContentFileMatchesBuiltInDefaults)
{
    AwarenessSettings settings{};
    settings.fillNear         = 99.0f;
    settings.alertShareRadius = 99.0f;
    settings.stanceSprint     = 99.0f;
    settings.bodyWeight[2]    = 99.0f;
    settings.bodyCrouchHeight[0] = 99.0f;
    ASSERT_TRUE(loadAwarenessSettings(settings));
    expectNearBuiltIn(settings);
}

TEST(AwarenessSettings, SetterSanitizesAndBlocksReload)
{
    Dark::AiSystem ai;
    AwarenessSettings settings{};
    settings.alertShareRadius = -4.0f;
    settings.reactMeter       = 2.0f;
    ai.setAwarenessSettings(settings);
    EXPECT_FLOAT_EQ(ai.awarenessSettings().alertShareRadius, 0.0f);
    EXPECT_FLOAT_EQ(ai.awarenessSettings().reactMeter, 1.0f);
    ai.ensureAwarenessSettings();
    EXPECT_FLOAT_EQ(ai.awarenessSettings().alertShareRadius, 0.0f);
    EXPECT_FLOAT_EQ(ai.awarenessSettings().reactMeter, 1.0f);
}
