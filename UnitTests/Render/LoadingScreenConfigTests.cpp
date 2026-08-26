#include <gtest/gtest.h>

#include "Core/Application.h"
#include "Render/LoadingScreen.h"
#include "Render/LoadingScreenConfig.h"

#include <filesystem>
#include <string>

using namespace Dark;

TEST(LoadingScreenConfig, Defaults)
{
    const LoadingScreenConfig cfg{};
    EXPECT_EQ(cfg.schemaVersion, 1);
    EXPECT_TRUE(cfg.enabled);
    EXPECT_TRUE(cfg.skipOnKey);
    EXPECT_FALSE(cfg.reducedMotion);
    EXPECT_NEAR(cfg.background[0], 0.05f, 1.0e-6f);
    EXPECT_NEAR(cfg.background[2], 0.07f, 1.0e-6f);
    EXPECT_NEAR(cfg.spinnerColor[0], 0.25f, 1.0e-6f);
    EXPECT_EQ(cfg.animation, "ring");
    EXPECT_EQ(cfg.engine.image, "textures/loading/engine_logo.png");
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 2.0f);
    EXPECT_EQ(cfg.engine.title, "DarkEngine6");
    EXPECT_TRUE(cfg.host.image.empty());
    EXPECT_FLOAT_EQ(cfg.host.minSeconds, 1.5f);
    EXPECT_TRUE(cfg.legal.showCopyright);
    EXPECT_TRUE(cfg.versionText.showEngine);
    EXPECT_TRUE(cfg.versionText.showHost);
    EXPECT_TRUE(cfg.versionText.showGit);
    EXPECT_EQ(cfg.versionText.anchor, "bottom-left");
}

TEST(LoadingScreenConfig, ClampMinSeconds)
{
    LoadingScreenConfig cfg{};
    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "engine": { "minSeconds": 99 }, "host": { "minSeconds": -5 } })"));
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 30.0f);
    EXPECT_FLOAT_EQ(cfg.host.minSeconds, 0.0f);

    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "engine": { "minSeconds": 2 } })"));
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 2.0f);

    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "host": { "minSeconds": 2.0 } })"));
    EXPECT_FLOAT_EQ(cfg.host.minSeconds, 2.0f);
}

TEST(LoadingScreenConfig, DiscardedJsonLeavesPrevious)
{
    LoadingScreenConfig cfg{};
    cfg.enabled = false;
    cfg.engine.minSeconds = 4.0f;

    EXPECT_FALSE(mergeLoadingScreenConfigJson(cfg, "{"));
    EXPECT_FALSE(mergeLoadingScreenConfigJson(cfg, "not json"));
    EXPECT_FALSE(mergeLoadingScreenConfigJson(cfg, "null"));
    EXPECT_FALSE(mergeLoadingScreenConfigJson(cfg, "[]"));
    EXPECT_FALSE(cfg.enabled);
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 4.0f);
}

TEST(LoadingScreenConfig, InvalidTypesKeepPrevious)
{
    LoadingScreenConfig cfg{};
    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "engine": { "minSeconds": "fast" }, "background": "red" })"));
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 2.0f);
    EXPECT_NEAR(cfg.background[0], 0.05f, 1.0e-6f);
    EXPECT_NEAR(cfg.background[1], 0.05f, 1.0e-6f);
    EXPECT_NEAR(cfg.background[2], 0.07f, 1.0e-6f);
    EXPECT_NEAR(cfg.background[3], 1.0f, 1.0e-6f);
}

TEST(LoadingScreenConfig, AnimationNoneAndReducedMotion)
{
    LoadingScreenConfig cfg{};
    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "animation": "none", "reducedMotion": true })"));
    EXPECT_EQ(cfg.animation, "none");
    EXPECT_TRUE(cfg.reducedMotion);
}

TEST(LoadingScreenConfig, HostImageOverlayDoesNotWipeMinSeconds)
{
    LoadingScreenConfig cfg{};
    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "host": { "image": "textures/loading/sandbox_logo.png" } })"));
    EXPECT_EQ(cfg.host.image, "textures/loading/sandbox_logo.png");
    EXPECT_FLOAT_EQ(cfg.host.minSeconds, 1.5f);
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 2.0f);
    EXPECT_EQ(cfg.engine.image, "textures/loading/engine_logo.png");
}

TEST(LoadingScreenConfig, SchemaVersionAndVersionTextKeys)
{
    LoadingScreenConfig cfg{};
    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({
        "schemaVersion": 1,
        "version": 99,
        "versionText": { "showGit": false, "anchor": "bottom-right", "showEngine": true, "showHost": false }
    })"));
    EXPECT_EQ(cfg.schemaVersion, 1);
    EXPECT_FALSE(cfg.versionText.showGit);
    EXPECT_TRUE(cfg.versionText.showEngine);
    EXPECT_FALSE(cfg.versionText.showHost);
    EXPECT_EQ(cfg.versionText.anchor, "bottom-right");
    EXPECT_TRUE(cfg.enabled);

    ASSERT_TRUE(mergeLoadingScreenConfigJson(cfg, R"({ "schemaVersion": 2, "enabled": false })"));
    EXPECT_EQ(cfg.schemaVersion, 2);
    EXPECT_FALSE(cfg.enabled);
}

TEST(LoadingScreenConfig, ResolveSplashAssetRejectsAbsoluteAndDotDot)
{
    std::filesystem::path out;
    EXPECT_FALSE(resolveSplashAsset("", out));
    EXPECT_TRUE(out.empty());

    EXPECT_FALSE(resolveSplashAsset(R"(C:\Windows\notepad.exe)", out));
    EXPECT_TRUE(out.empty());

    EXPECT_FALSE(resolveSplashAsset("C:/Windows/notepad.exe", out));
    EXPECT_TRUE(out.empty());

    EXPECT_FALSE(resolveSplashAsset("../secret.png", out));
    EXPECT_FALSE(resolveSplashAsset("..\\secret.png", out));
    EXPECT_FALSE(resolveSplashAsset("textures/../loading/engine_logo.png", out));
    EXPECT_FALSE(resolveSplashAsset("textures/loading/../../engine_logo.png", out));
}

TEST(LoadingScreenConfig, TryLoadConfigIsCpuOnly)
{
    LoadingScreen ls;
    AppConfig     app{};
    app.hostId   = "sandbox";
    app.hostName = "Sandbox";
    EXPECT_TRUE(ls.tryLoadConfig(app));
    EXPECT_TRUE(ls.config().enabled);
    EXPECT_FLOAT_EQ(ls.config().engine.minSeconds, 2.0f);
    EXPECT_FLOAT_EQ(ls.config().host.minSeconds, 1.5f);
    EXPECT_FALSE(ls.isReady());
}

TEST(LoadingScreenConfig, LoadLayersDeepMergeHostImage)
{
    AppConfig app{};
    app.hostId = "sandbox";
    LoadingScreenConfig cfg;
    ASSERT_TRUE(loadLoadingScreenConfig(app, cfg));
    EXPECT_FLOAT_EQ(cfg.engine.minSeconds, 2.0f);
    EXPECT_FLOAT_EQ(cfg.host.minSeconds, 1.5f);

    std::filesystem::path hostJson;
    if (resolveSplashAsset("loading/sandbox.json", hostJson))
        EXPECT_EQ(cfg.host.image, "textures/loading/sandbox_logo.png");
}
