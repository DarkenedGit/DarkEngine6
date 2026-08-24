#pragma once

#include <cstdint>

namespace Dark
{

    struct Address
    {
        uint32_t ipv4 = 0; // host byte order
        uint16_t port = 0; // host byte order
        bool operator==(const Address&) const = default;
    };

    // Dotted-quad, optional ":port". "127.0.0.1" leaves port unchanged.
    bool parseIPv4(const char* s, Address& out);

} // namespace Dark
