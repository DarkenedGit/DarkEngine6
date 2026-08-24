#include "Network/NetSockets.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

namespace Dark
{

    namespace
    {
        int g_wsaRefs = 0;
    }

    bool NetSockets::startup()
    {
        if (g_wsaRefs > 0)
        {
            ++g_wsaRefs;
            return true;
        }

        WSADATA wsa{};
        const int err = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (err != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "WSAStartup failed ({})", err);
            return false;
        }
        g_wsaRefs = 1;
        return true;
    }

    void NetSockets::shutdown()
    {
        if (g_wsaRefs <= 0)
            return;
        --g_wsaRefs;
        if (g_wsaRefs == 0)
            WSACleanup();
    }

} // namespace Dark
