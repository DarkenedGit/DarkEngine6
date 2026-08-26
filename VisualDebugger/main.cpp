#include "DebuggerApp.h"
#include "Core/Log.h"
#include "Network/NetTypes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>

namespace
{
    bool isAsciiSpace(unsigned char c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }

    bool parseDebugJoin(const char* lpCmdLine, Dark::Address& out)
    {
        out = {};
        const char* p = lpCmdLine ? lpCmdLine : "";
        while (*p)
        {
            while (*p && isAsciiSpace(static_cast<unsigned char>(*p)))
                ++p;
            if (*p == 0)
                break;

            char tok[96]{};
            uint32_t n = 0;
            while (*p && !isAsciiSpace(static_cast<unsigned char>(*p)))
            {
                if (n + 1 < sizeof(tok))
                    tok[n++] = *p;
                ++p;
            }
            tok[n] = 0;

            const char* addrStr = nullptr;
            if (std::strcmp(tok, "-join") == 0)
            {
                while (*p && isAsciiSpace(static_cast<unsigned char>(*p)))
                    ++p;
                if (*p == 0)
                    return false;
                char next[96]{};
                uint32_t m = 0;
                while (*p && !isAsciiSpace(static_cast<unsigned char>(*p)))
                {
                    if (m + 1 < sizeof(next))
                        next[m++] = *p;
                    ++p;
                }
                next[m]  = 0;
                addrStr  = next;
            }
            else if (std::strncmp(tok, "-join:", 6) == 0)
                addrStr = tok + 6;

            if (addrStr)
            {
                Dark::Address addr{};
                addr.port = Dark::kDebugDefaultPort;
                if (!Dark::parseIPv4(addrStr, addr))
                    return false;
                if (addr.port == 0)
                    addr.port = Dark::kDebugDefaultPort;
                out = addr;
                return true;
            }
        }
        return true;
    }
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Visual Debugger";
    cfg.width  = 1600;
    cfg.height = 900;
    cfg.vsync  = true;

    Dark::Address join{};
    if (!parseDebugJoin(lpCmdLine, join))
        DE_LOG_ERROR(Dark::LogCategory::Debug, "VisualDebugger: invalid -join address");

    DebuggerApp app{cfg, join};
    if (!app.initOk())
    {
        DE_LOG_FATAL("Failed to start");
        return 1;
    }
    app.run();
    return 0;
}
