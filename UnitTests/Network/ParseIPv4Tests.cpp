#include <gtest/gtest.h>

#include "Network/NetTypes.h"

using namespace Dark;

TEST(ParseIPv4, HostOnlyLeavesPort)
{
    Address a{};
    a.port = 9;
    ASSERT_TRUE(parseIPv4("127.0.0.1", a));
    EXPECT_EQ(a.ipv4, 0x7F000001u);
    EXPECT_EQ(a.port, 9);
}

TEST(ParseIPv4, HostAndPort)
{
    Address a{};
    a.port = 1;
    ASSERT_TRUE(parseIPv4("127.0.0.1:26160", a));
    EXPECT_EQ(a.ipv4, 0x7F000001u);
    EXPECT_EQ(a.port, 26160);
}

TEST(ParseIPv4, TypicalLan)
{
    Address a{};
    ASSERT_TRUE(parseIPv4("192.168.1.50:65535", a));
    EXPECT_EQ(a.ipv4, (192u << 24) | (168u << 16) | (1u << 8) | 50u);
    EXPECT_EQ(a.port, 65535);
}

TEST(ParseIPv4, RejectsOverflowAndJunk)
{
    Address a{};
    EXPECT_FALSE(parseIPv4(nullptr, a));
    EXPECT_FALSE(parseIPv4("", a));
    EXPECT_FALSE(parseIPv4("256.0.0.1", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1:65536", a));
    EXPECT_FALSE(parseIPv4("127.0.0", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1.2", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1:", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1:26160foo", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1 ", a));
    EXPECT_FALSE(parseIPv4(" 127.0.0.1", a));
    EXPECT_FALSE(parseIPv4("127.0.0.1:port", a));
    EXPECT_FALSE(parseIPv4("1.2.3.-4", a));
}

TEST(ParseIPv4, RejectsNonAscii)
{
    Address a{};
    const char bad[] = {'1', '2', '7', '.', '0', '.', '0', '.', static_cast<char>(static_cast<unsigned char>(0x80)), 0};
    EXPECT_FALSE(parseIPv4(bad, a));
}

TEST(ParseIPv4, BroadcastAndAny)
{
    Address a{};
    ASSERT_TRUE(parseIPv4("255.255.255.255", a));
    EXPECT_EQ(a.ipv4, 0xFFFFFFFFu);
    ASSERT_TRUE(parseIPv4("0.0.0.0:0", a));
    EXPECT_EQ(a.ipv4, 0u);
    EXPECT_EQ(a.port, 0);
}
