#pragma once

#include "Network/Transport.h"

#include <cstdint>

namespace Dark
{

    class UdpSocket : public ITransport
    {
    public:
        UdpSocket() = default;
        ~UdpSocket() override;

        UdpSocket(const UdpSocket&)            = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;

        // reuseAddr: SO_REUSEADDR before bind (browse :26161, best-effort).
        // broadcast: SO_BROADCAST (host beacons to 255.255.255.255). Failures warn; open may still succeed.
        bool open(uint16_t port, bool reuseAddr = false, bool broadcast = false);

        bool sendTo(const Address& dest, const void* data, uint32_t size) override;
        bool recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize) override;
        Address localAddress() const override { return m_local; }
        void close() override;

    private:
        uintptr_t m_socket  = static_cast<uintptr_t>(-1); // SOCKET; Winsock stays in the .cpp
        Address   m_local{};
        bool      m_started = false;
    };

} // namespace Dark
