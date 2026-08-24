#include <gtest/gtest.h>

#include "Core/Application.h"
#include "Network/NetTypes.h"

using namespace Dark;

TEST(ParseNetCommandLine, EmptyAndNullLeaveIdle)
{
    AppConfig a{};
    EXPECT_TRUE(parseNetCommandLine(nullptr, a));
    EXPECT_FALSE(a.netHost);
    EXPECT_EQ(a.netHostPort, kNetDefaultPort);
    EXPECT_EQ(a.netJoin.ipv4, 0u);
    EXPECT_EQ(a.netJoin.port, 0);

    AppConfig b{};
    EXPECT_TRUE(parseNetCommandLine("", b));
    EXPECT_FALSE(b.netHost);
    EXPECT_EQ(b.netJoin.ipv4, 0u);
    EXPECT_EQ(b.netJoin.port, 0);

    AppConfig c{};
    EXPECT_TRUE(parseNetCommandLine("   \t\n  ", c));
    EXPECT_FALSE(c.netHost);
    EXPECT_EQ(c.netJoin.ipv4, 0u);
}

TEST(ParseNetCommandLine, HostDefaultPort)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-host", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, kNetDefaultPort);
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
    EXPECT_EQ(cfg.netJoin.port, 0);
}

TEST(ParseNetCommandLine, HostSpacePort)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-host 26160", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, 26160);
}

TEST(ParseNetCommandLine, HostColonPort)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-host:27015", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, 27015);
}

TEST(ParseNetCommandLine, JoinIpDefaultPort)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-join 127.0.0.1", cfg));
    EXPECT_FALSE(cfg.netHost);
    EXPECT_EQ(cfg.netJoin.ipv4, 0x7F000001u);
    EXPECT_EQ(cfg.netJoin.port, kNetDefaultPort);
}

TEST(ParseNetCommandLine, JoinIpPort)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-join 127.0.0.1:26160", cfg));
    EXPECT_FALSE(cfg.netHost);
    EXPECT_EQ(cfg.netJoin.ipv4, 0x7F000001u);
    EXPECT_EQ(cfg.netJoin.port, 26160);
}

TEST(ParseNetCommandLine, JoinColonForm)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-join:10.0.0.2:1234", cfg));
    EXPECT_EQ(cfg.netJoin.ipv4, (10u << 24) | 2u);
    EXPECT_EQ(cfg.netJoin.port, 1234);
}

TEST(ParseNetCommandLine, BothFlagsLeaveIdle)
{
    AppConfig cfg{};
    cfg.netHost     = true;
    cfg.netHostPort = 1;
    cfg.netJoin.ipv4 = 1;
    cfg.netJoin.port = 1;
    EXPECT_FALSE(parseNetCommandLine("-host -join 127.0.0.1", cfg));
    EXPECT_FALSE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, kNetDefaultPort);
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
    EXPECT_EQ(cfg.netJoin.port, 0);
}

TEST(ParseNetCommandLine, BothFlagsJoinFirstLeaveIdle)
{
    AppConfig cfg{};
    EXPECT_FALSE(parseNetCommandLine("-join 127.0.0.1:26160 -host:26160", cfg));
    EXPECT_FALSE(cfg.netHost);
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
    EXPECT_EQ(cfg.netJoin.port, 0);
}

TEST(ParseNetCommandLine, GarbageIgnored)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-foo bar --help /host not-an-ip", cfg));
    EXPECT_FALSE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, kNetDefaultPort);
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
    EXPECT_EQ(cfg.netJoin.port, 0);
}

TEST(ParseNetCommandLine, HostWithGarbageStillHosts)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("noise -host 26160 -foo", cfg));
    EXPECT_TRUE(cfg.netHost);
    EXPECT_EQ(cfg.netHostPort, 26160);
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
}

TEST(ParseNetCommandLine, InvalidJoinDoesNotSet)
{
    AppConfig cfg{};
    ASSERT_TRUE(parseNetCommandLine("-join not-an-ip", cfg));
    EXPECT_EQ(cfg.netJoin.ipv4, 0u);
    EXPECT_EQ(cfg.netJoin.port, 0);
    EXPECT_FALSE(cfg.netHost);
}

TEST(ParseNetCommandLine, DoesNotTouchNonNetFields)
{
    AppConfig cfg{};
    cfg.title  = "KeepMe";
    cfg.width  = 100;
    cfg.height = 200;
    cfg.vsync  = false;
    ASSERT_TRUE(parseNetCommandLine("-host", cfg));
    EXPECT_STREQ(cfg.title, "KeepMe");
    EXPECT_EQ(cfg.width, 100u);
    EXPECT_EQ(cfg.height, 200u);
    EXPECT_FALSE(cfg.vsync);
    EXPECT_TRUE(cfg.netHost);
}
