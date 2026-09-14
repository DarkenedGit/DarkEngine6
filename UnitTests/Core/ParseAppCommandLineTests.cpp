#include <gtest/gtest.h>

#include "Core/Application.h"
#include "Network/NetTypes.h"

#include <cstdlib>
#include <stdlib.h>
#include <string>

using namespace Dark;

namespace
{
    struct EnvRestore
    {
        std::string name;
        std::string oldValue;
        bool        had = false;

        explicit EnvRestore(const char* varName)
            : name(varName)
        {
            char*  env = nullptr;
            size_t len = 0;
            if (_dupenv_s(&env, &len, varName) == 0 && env)
            {
                had      = true;
                oldValue = env;
                free(env);
            }
        }

        ~EnvRestore()
        {
            _putenv_s(name.c_str(), had ? oldValue.c_str() : "");
        }

        EnvRestore(const EnvRestore&)            = delete;
        EnvRestore& operator=(const EnvRestore&) = delete;
    };

    void setEnv(const char* name, const char* value)
    {
        _putenv_s(name, value);
    }
} // namespace

TEST(ParseAppCommandLine, EmptyAndNullLeaveFlags)
{
    AppConfig a{};
    EXPECT_TRUE(parseAppCommandLine(nullptr, a));
    EXPECT_FALSE(a.cliNoSplash);
    EXPECT_FALSE(a.cliSplash);
    EXPECT_FALSE(a.cliNoMenu);
    EXPECT_FALSE(a.cliMenu);
    EXPECT_FALSE(a.cliForward);
    EXPECT_EQ(a.scenePath, ScenePath::SwapChainForward);
    EXPECT_FALSE(a.showSplash);
    EXPECT_FALSE(a.showMainMenu);

    AppConfig b{};
    EXPECT_TRUE(parseAppCommandLine("", b));
    EXPECT_FALSE(b.cliNoSplash);
    EXPECT_FALSE(b.cliSplash);

    AppConfig c{};
    EXPECT_TRUE(parseAppCommandLine("   \t\n  ", c));
    EXPECT_FALSE(c.cliNoSplash);
    EXPECT_FALSE(c.cliSplash);
}

TEST(ParseAppCommandLine, NoSplash)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-no-splash", cfg));
    EXPECT_TRUE(cfg.cliNoSplash);
    EXPECT_FALSE(cfg.cliSplash);
}

TEST(ParseAppCommandLine, Splash)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-splash", cfg));
    EXPECT_TRUE(cfg.cliSplash);
    EXPECT_FALSE(cfg.cliNoSplash);
}

TEST(ParseAppCommandLine, BothFlagsRecorded)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-splash -no-splash", cfg));
    EXPECT_TRUE(cfg.cliSplash);
    EXPECT_TRUE(cfg.cliNoSplash);
}

TEST(ParseAppCommandLine, MixedWithNetTokens)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-host 26160 -splash -foo", cfg));
    EXPECT_TRUE(cfg.cliSplash);
    EXPECT_FALSE(cfg.cliNoSplash);
    EXPECT_FALSE(cfg.netHost);
}

TEST(ParseAppCommandLine, DoesNotTouchNetFields)
{
    AppConfig cfg{};
    cfg.netHost     = true;
    cfg.netHostPort = 1234;
    cfg.netJoin.ipv4 = 1;
    cfg.netJoin.port = 2;
    ASSERT_TRUE(parseAppCommandLine("-no-splash -splash", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, 1234);
    EXPECT_EQ(cfg.netJoin.ipv4, 1u);
    EXPECT_EQ(cfg.netJoin.port, 2);
    EXPECT_TRUE(cfg.cliNoSplash);
    EXPECT_TRUE(cfg.cliSplash);
}

TEST(ParseAppCommandLine, NoMenu)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-no-menu", cfg));
    EXPECT_TRUE(cfg.cliNoMenu);
    EXPECT_FALSE(cfg.cliMenu);
}

TEST(ParseAppCommandLine, Menu)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-menu", cfg));
    EXPECT_TRUE(cfg.cliMenu);
    EXPECT_FALSE(cfg.cliNoMenu);
}

TEST(ParseAppCommandLine, ForwardFlag)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-forward", cfg));
    EXPECT_TRUE(cfg.cliForward);
    EXPECT_EQ(cfg.scenePath, ScenePath::SwapChainForward);
}

TEST(ApplyDeferredScenePath, MapsForwardFlag)
{
    AppConfig on{};
    on.cliForward = true;
    applyDeferredScenePath(on, ScenePath::HybridDeferred);
    EXPECT_EQ(on.scenePath, ScenePath::SwapChainForward);

    AppConfig off{};
    applyDeferredScenePath(off, ScenePath::HybridDeferred);
    EXPECT_EQ(off.scenePath, ScenePath::HybridDeferred);
}

TEST(ApplyDeferredScenePath, ParseThenMap)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseAppCommandLine("-forward", cfg));
    applyDeferredScenePath(cfg, ScenePath::HybridDeferred);
    EXPECT_TRUE(cfg.cliForward);
    EXPECT_EQ(cfg.scenePath, ScenePath::SwapChainForward);
}

TEST(ParseAppCommandLine, TogetherWithParseNet)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-host -splash", cfg));
    ASSERT_TRUE(parseAppCommandLine("-host -splash", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, kNetDefaultPort);
    EXPECT_TRUE(cfg.cliSplash);
    EXPECT_FALSE(cfg.cliNoSplash);
}

TEST(ShouldShowSplash, EnvBeatsCliSplash)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "1");

    AppConfig cfg{};
    cfg.cliSplash  = true;
    cfg.showSplash = true;
    EXPECT_FALSE(shouldShowSplash(cfg, true));
}

TEST(ShouldShowSplash, EmptyEnvDoesNotDisable)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.cliSplash = true;
    EXPECT_TRUE(shouldShowSplash(cfg, false));
}

TEST(ShouldShowSplash, CliNoSplashBeatsCliSplash)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.cliNoSplash = true;
    cfg.cliSplash   = true;
    cfg.showSplash  = true;
    EXPECT_FALSE(shouldShowSplash(cfg, true));
}

TEST(ShouldShowSplash, CliSplashBeatsShowSplashFalseAndJson)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.cliSplash  = true;
    cfg.showSplash = false;
    EXPECT_TRUE(shouldShowSplash(cfg, false));
}

TEST(ShouldShowSplash, ShowSplashFalseOff)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.showSplash = false;
    EXPECT_FALSE(shouldShowSplash(cfg, true));
}

TEST(ShouldShowSplash, JsonDisabledOff)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.showSplash = true;
    EXPECT_FALSE(shouldShowSplash(cfg, false));
}

TEST(ShouldShowSplash, DefaultOnWhenEnabled)
{
    EnvRestore restore("DE_NO_SPLASH");
    setEnv("DE_NO_SPLASH", "");

    AppConfig cfg{};
    cfg.showSplash = true;
    EXPECT_TRUE(shouldShowSplash(cfg, true));
}

TEST(ShouldShowMainMenu, EnvBeatsCliMenu)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "1");

    AppConfig cfg{};
    cfg.cliMenu      = true;
    cfg.showMainMenu = true;
    EXPECT_FALSE(shouldShowMainMenu(cfg));
}

TEST(ShouldShowMainMenu, CliNoMenuBeatsCliMenu)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig cfg{};
    cfg.cliNoMenu    = true;
    cfg.cliMenu      = true;
    cfg.showMainMenu = true;
    EXPECT_FALSE(shouldShowMainMenu(cfg));
}

TEST(ShouldShowMainMenu, CliMenuBeatsShowMainMenuFalse)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig cfg{};
    cfg.cliMenu      = true;
    cfg.showMainMenu = false;
    EXPECT_TRUE(shouldShowMainMenu(cfg));
}

TEST(ShouldShowMainMenu, NetHostSkipsUnlessCliMenu)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig host{};
    host.showMainMenu = true;
    host.netHost      = true;
    EXPECT_FALSE(shouldShowMainMenu(host));

    AppConfig forced{};
    forced.showMainMenu = true;
    forced.netHost      = true;
    forced.cliMenu      = true;
    EXPECT_TRUE(shouldShowMainMenu(forced));
}

TEST(ShouldShowMainMenu, NetJoinSkips)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig cfg{};
    cfg.showMainMenu  = true;
    cfg.netJoin.ipv4  = 0x7F000001u;
    cfg.netJoin.port  = kNetDefaultPort;
    EXPECT_FALSE(shouldShowMainMenu(cfg));
}

TEST(ShouldShowMainMenu, HostFlagOn)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig cfg{};
    cfg.showMainMenu = true;
    EXPECT_TRUE(shouldShowMainMenu(cfg));
}

TEST(ShouldShowMainMenu, DefaultOff)
{
    EnvRestore restore("DE_NO_MENU");
    setEnv("DE_NO_MENU", "");

    AppConfig cfg{};
    EXPECT_FALSE(shouldShowMainMenu(cfg));
}
