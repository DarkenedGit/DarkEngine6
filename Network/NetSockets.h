#pragma once

namespace Dark
{

    struct NetSockets
    {
        // Refcounted. First success calls WSAStartup(MAKEWORD(2,2)). Last shutdown calls WSACleanup.
        static bool startup();
        static void shutdown(); // idempotent; extra calls are no-ops
    };

} // namespace Dark
