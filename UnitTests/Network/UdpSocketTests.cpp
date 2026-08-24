#include <gtest/gtest.h>

#include "Network/UdpSocket.h"
#include "Network/Transport.h"

#include <chrono>
#include <cstdint>
#include <thread>

using namespace Dark;

TEST(UdpSocket, LoopbackEcho)
{
    UdpSocket sender;
    UdpSocket receiver;
    if (!receiver.open(0) || !sender.open(0))
    {
        SUCCEED();
        return;
    }

    Address dest = receiver.localAddress();
    dest.ipv4    = 0x7F000001u;

    const uint8_t payload[] = {1, 2, 3, 4, 5};
    ASSERT_TRUE(sender.sendTo(dest, payload, sizeof(payload)));

    Address  src{};
    uint8_t  buf[64]{};
    uint32_t n   = 0;
    bool     got = false;
    for (int i = 0; i < 50; ++i)
    {
        if (receiver.recvFrom(src, buf, sizeof(buf), n))
        {
            got = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(got);
    ASSERT_EQ(n, sizeof(payload));
    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[4], 5);

    EXPECT_FALSE(sender.sendTo(dest, payload, kNetMaxPayload + 1));
}

TEST(UdpSocket, RecvWouldBlock)
{
    UdpSocket s;
    if (!s.open(0))
    {
        SUCCEED();
        return;
    }
    Address  src{};
    uint8_t  buf[16]{};
    uint32_t n = 99;
    EXPECT_FALSE(s.recvFrom(src, buf, sizeof(buf), n));
}
