#pragma once

#include "Network/NetTypes.h"

#include <cstdint>

namespace Dark
{

    constexpr uint32_t kNetMaxPayload = 1200;

    class ITransport
    {
    public:
        virtual ~ITransport() = default;

        virtual bool sendTo(const Address& dest, const void* data, uint32_t size) = 0;

        // true: a datagram was dequeued. outSize is 1..capacity on success, or 0 if
        // dequeued but not delivered (oversize, empty, or capacity too small).
        // false: would-block or socket error (already logged).
        virtual bool recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize) = 0;

        virtual Address localAddress() const = 0;
        virtual void close() = 0;
    };

} // namespace Dark
