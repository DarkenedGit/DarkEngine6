#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <gtest/gtest.h>

#include "Network/UdpSocket.h"
#include "Network/Transport.h"

#include <chrono>
#include <cstdint>
#include <thread>

using namespace Dark;

namespace
{
    bool recvWait(UdpSocket& sock, Address& src, void* buf, uint32_t cap, uint32_t& n)
    {
        for (int i = 0; i < 50; ++i)
        {
            if (sock.recvFrom(src, buf, cap, n))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
}

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
    uint32_t n = 0;
    ASSERT_TRUE(recvWait(receiver, src, buf, sizeof(buf), n));
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

TEST(UdpSocket, OversizeRecvDropsAndDequeues)
{
    UdpSocket receiver;
    if (!receiver.open(0))
    {
        SUCCEED();
        return;
    }

    const SOCKET raw = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (raw == INVALID_SOCKET)
    {
        SUCCEED();
        return;
    }
    struct RawCloser
    {
        SOCKET s;
        ~RawCloser()
        {
            if (s != INVALID_SOCKET)
                ::closesocket(s);
        }
    } closer{raw};

    Address dest = receiver.localAddress();
    dest.ipv4    = 0x7F000001u;

    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_port        = htons(dest.port);
    to.sin_addr.s_addr = htonl(dest.ipv4);

    uint8_t big[kNetMaxPayload + 64];
    for (uint8_t& b : big)
        b = 0xAB;
    if (::sendto(raw, reinterpret_cast<const char*>(big), static_cast<int>(sizeof(big)), 0, reinterpret_cast<const sockaddr*>(&to),
                 sizeof(to))
        == SOCKET_ERROR)
    {
        SUCCEED();
        return;
    }

    const uint8_t small[] = {1, 2, 3};
    if (::sendto(raw, reinterpret_cast<const char*>(small), static_cast<int>(sizeof(small)), 0, reinterpret_cast<const sockaddr*>(&to),
                 sizeof(to))
        == SOCKET_ERROR)
    {
        SUCCEED();
        return;
    }

    Address  src{};
    uint8_t  buf[64]{};
    uint32_t n = 99;
    ASSERT_TRUE(recvWait(receiver, src, buf, sizeof(buf), n));
    EXPECT_EQ(n, 0u);

    ASSERT_TRUE(recvWait(receiver, src, buf, sizeof(buf), n));
    ASSERT_EQ(n, sizeof(small));
    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[1], 2);
    EXPECT_EQ(buf[2], 3);
}
