#include "Network/UdpSocket.h"
#include "Network/NetSockets.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include <cstring>

namespace Dark
{

    namespace
    {
        constexpr uintptr_t kInvalidSocket = static_cast<uintptr_t>(-1);

        SOCKET asSocket(uintptr_t h)
        {
            return static_cast<SOCKET>(h);
        }

        bool isIcmpReset(int err)
        {
            return err == WSAECONNRESET || err == WSAENETRESET || err == WSAECONNREFUSED;
        }
    }

    UdpSocket::~UdpSocket()
    {
        close();
    }

    bool UdpSocket::open(uint16_t port, bool reuseAddr, bool broadcast)
    {
        close();
        if (!NetSockets::startup())
            return false;
        m_started = true;

        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
        {
            DE_LOG_ERROR(LogCategory::Networking, "UdpSocket: socket() failed ({})", WSAGetLastError());
            close();
            return false;
        }
        m_socket = static_cast<uintptr_t>(s);

        // Unconnected UDP: ICMP Port Unreachable must not fail later recvfrom (WSAECONNRESET).
        BOOL  disableConnReset = FALSE;
        DWORD ioctlBytes       = 0;
        if (::WSAIoctl(s, SIO_UDP_CONNRESET, &disableConnReset, sizeof(disableConnReset), nullptr, 0, &ioctlBytes, nullptr, nullptr) != 0)
            DE_LOG_WARN(LogCategory::Networking, "UdpSocket: SIO_UDP_CONNRESET failed ({})", WSAGetLastError());

        u_long nonblock = 1;
        if (::ioctlsocket(s, FIONBIO, &nonblock) != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "UdpSocket: FIONBIO failed ({})", WSAGetLastError());
            close();
            return false;
        }

        if (reuseAddr)
        {
            BOOL opt = TRUE;
            if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0)
                DE_LOG_WARN(LogCategory::Networking, "UdpSocket: SO_REUSEADDR failed ({})", WSAGetLastError());
        }

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "UdpSocket: bind({}) failed ({})", port, WSAGetLastError());
            close();
            return false;
        }

        if (broadcast)
        {
            BOOL opt = TRUE;
            if (::setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0)
                DE_LOG_WARN(LogCategory::Networking, "UdpSocket: SO_BROADCAST failed ({}); typed IP / CLI still work", WSAGetLastError());
        }

        sockaddr_in bound{};
        int         boundLen = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
        {
            m_local.ipv4 = ntohl(bound.sin_addr.s_addr);
            m_local.port = ntohs(bound.sin_port);
        }
        else
        {
            DE_LOG_WARN(LogCategory::Networking, "UdpSocket: getsockname failed ({})", WSAGetLastError());
            m_local.port = port;
        }

        DE_LOG_INFO(LogCategory::Networking, "UdpSocket: bound port {}", m_local.port);
        return true;
    }

    bool UdpSocket::sendTo(const Address& dest, const void* data, uint32_t size)
    {
        if (m_socket == kInvalidSocket)
            return false;
        if (size > kNetMaxPayload)
        {
            DE_LOG_WARN(LogCategory::Networking, "UdpSocket: sendTo payload {} exceeds {}", size, kNetMaxPayload);
            return false;
        }
        if (size > 0 && !data)
            return false;

        sockaddr_in to{};
        to.sin_family      = AF_INET;
        to.sin_port        = htons(dest.port);
        to.sin_addr.s_addr = htonl(dest.ipv4);

        const int sent = ::sendto(asSocket(m_socket), static_cast<const char*>(data), static_cast<int>(size), 0,
                                  reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        if (sent == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || isIcmpReset(err))
                return false;
            DE_LOG_ERROR(LogCategory::Networking, "UdpSocket: sendto failed ({})", err);
            return false;
        }
        return true;
    }

    bool UdpSocket::recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize)
    {
        outSize = 0;
        if (m_socket == kInvalidSocket)
            return false;

        uint8_t scratch[2048];
        for (;;)
        {
            sockaddr_in from{};
            int         fromLen = sizeof(from);
            const int n = ::recvfrom(asSocket(m_socket), reinterpret_cast<char*>(scratch), static_cast<int>(sizeof(scratch)), 0,
                                     reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n == SOCKET_ERROR)
            {
                const int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                    return false;
                if (isIcmpReset(err))
                    continue;
                if (err == WSAEMSGSIZE)
                {
                    DE_LOG_WARN(LogCategory::Networking, "UdpSocket: dropped oversize datagram");
                    src.ipv4 = ntohl(from.sin_addr.s_addr);
                    src.port = ntohs(from.sin_port);
                    outSize  = 0;
                    return true;
                }
                DE_LOG_ERROR(LogCategory::Networking, "UdpSocket: recvfrom failed ({})", err);
                return false;
            }

            src.ipv4 = ntohl(from.sin_addr.s_addr);
            src.port = ntohs(from.sin_port);

            // n == 0 (empty datagram) is dequeued but not delivered, same as oversize.
            const uint32_t bytes = static_cast<uint32_t>(n);
            if (bytes == 0 || bytes > kNetMaxPayload || bytes > capacity || !buffer)
            {
                if (bytes != 0)
                    DE_LOG_WARN(LogCategory::Networking, "UdpSocket: dropped oversize datagram ({} bytes)", bytes);
                outSize = 0;
                return true;
            }

            std::memcpy(buffer, scratch, bytes);
            outSize = bytes;
            return true;
        }
    }

    void UdpSocket::close()
    {
        if (m_socket != kInvalidSocket)
        {
            const SOCKET s = asSocket(m_socket);
            m_socket       = kInvalidSocket;
            if (::closesocket(s) == SOCKET_ERROR)
                DE_LOG_WARN(LogCategory::Networking, "UdpSocket: closesocket failed ({})", WSAGetLastError());
        }
        m_local = {};
        if (m_started)
        {
            m_started = false;
            NetSockets::shutdown();
        }
    }

} // namespace Dark
