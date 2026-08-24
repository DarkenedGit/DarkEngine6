#include "Network/NetTypes.h"

namespace Dark
{

    bool parseIPv4(const char* s, Address& out)
    {
        if (!s)
            return false;

        uint32_t octets[4] = {};
        uint32_t octetIx   = 0;
        uint32_t value     = 0;
        uint32_t digits    = 0;
        const char* p      = s;

        while (*p && *p != ':')
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c > 127)
                return false;
            if (c >= '0' && c <= '9')
            {
                const uint32_t d = static_cast<uint32_t>(c - '0');
                if (value > (255u - d) / 10u)
                    return false;
                value = value * 10u + d;
                ++digits;
                ++p;
                continue;
            }
            if (c == '.')
            {
                if (digits == 0 || octetIx >= 3)
                    return false;
                octets[octetIx++] = value;
                value             = 0;
                digits            = 0;
                ++p;
                continue;
            }
            return false;
        }

        if (digits == 0 || octetIx != 3)
            return false;
        octets[3] = value;

        bool     havePort = false;
        uint16_t port     = 0;
        if (*p == ':')
        {
            ++p;
            if (*p == 0)
                return false;
            uint32_t portVal = 0;
            while (*p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c > 127 || c < '0' || c > '9')
                    return false;
                const uint32_t d = static_cast<uint32_t>(c - '0');
                if (portVal > (65535u - d) / 10u)
                    return false;
                portVal = portVal * 10u + d;
                ++p;
            }
            port     = static_cast<uint16_t>(portVal);
            havePort = true;
        }

        out.ipv4 = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
        if (havePort)
            out.port = port;
        return true;
    }

} // namespace Dark
