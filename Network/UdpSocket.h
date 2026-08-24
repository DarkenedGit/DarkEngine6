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

        bool open(uint16_t port);

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
