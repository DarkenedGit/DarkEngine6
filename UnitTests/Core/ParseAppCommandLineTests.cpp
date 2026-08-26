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
    EXPECT_FALSE(a.showSplash);

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
