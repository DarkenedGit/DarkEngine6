#include <gtest/gtest.h>

#include "Core/UiPalette.h"

using namespace Dark;

TEST(UiPalette, Rgb8MatchesHexSamples)
{
    EXPECT_NEAR(UiPalette::kVoid.r, 8.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(UiPalette::kVoid.g, 16.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(UiPalette::kVoid.b, 24.0f / 255.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(UiPalette::kVoid.a, 1.0f);

    EXPECT_NEAR(UiPalette::kAccentEngine.r, 64.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(UiPalette::kAccentEditor.r, 200.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(UiPalette::kAccentSandbox.g, 200.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(UiPalette::kAccentSandbox2D.r, 240.0f / 255.0f, 1.0e-6f);
}

TEST(UiPalette, AccentLookup)
{
    EXPECT_EQ(UiPalette::accent(UiAccent::Engine), UiPalette::kAccentEngine);
    EXPECT_EQ(UiPalette::accent(UiAccent::Editor), UiPalette::kAccentEditor);
    EXPECT_EQ(UiPalette::accent(UiAccent::Sandbox), UiPalette::kAccentSandbox);
    EXPECT_EQ(UiPalette::accent(UiAccent::Sandbox2D), UiPalette::kAccentSandbox2D);
}

TEST(UiPalette, HealthFillBands)
{
    EXPECT_EQ(UiPalette::healthFill(1.0f), UiPalette::kOk);
    EXPECT_EQ(UiPalette::healthFill(0.51f), UiPalette::kOk);
    EXPECT_EQ(UiPalette::healthFill(0.5f), UiPalette::kWarn);
    EXPECT_EQ(UiPalette::healthFill(0.26f), UiPalette::kWarn);
    EXPECT_EQ(UiPalette::healthFill(0.25f), UiPalette::kDanger);
    EXPECT_EQ(UiPalette::healthFill(0.0f), UiPalette::kDanger);
}

TEST(UiPalette, WithAlphaAndStore)
{
    const UiColor faded = UiPalette::kText.withAlpha(0.5f);
    EXPECT_FLOAT_EQ(faded.r, UiPalette::kText.r);
    EXPECT_FLOAT_EQ(faded.a, 0.5f);

    float rgba[4]{};
    UiPalette::kOk.store(rgba);
    EXPECT_FLOAT_EQ(rgba[0], UiPalette::kOk.r);
    EXPECT_FLOAT_EQ(rgba[1], UiPalette::kOk.g);
    EXPECT_FLOAT_EQ(rgba[2], UiPalette::kOk.b);
    EXPECT_FLOAT_EQ(rgba[3], UiPalette::kOk.a);
}

TEST(UiPalette, SemanticAliasesMatchHostAccents)
{
    EXPECT_EQ(UiPalette::kOk, UiPalette::kAccentSandbox);
    EXPECT_EQ(UiPalette::kWarn, UiPalette::kAccentSandbox2D);
    EXPECT_EQ(UiPalette::kInfo, UiPalette::kAccentEngine);
}
