#include "Network/TcpSocket.h"
#include "Network/NetSockets.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>

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

        bool setNonBlocking(SOCKET s)
        {
            u_long nonblock = 1;
            if (::ioctlsocket(s, FIONBIO, &nonblock) != 0)
            {
                DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: FIONBIO failed ({})", WSAGetLastError());
                return false;
            }
            return true;
        }

        Address fromSockaddr(const sockaddr_in& a)
        {
            Address out{};
            out.ipv4 = ntohl(a.sin_addr.s_addr);
            out.port = ntohs(a.sin_port);
            return out;
        }

        void toSockaddr(const Address& addr, sockaddr_in& out)
        {
            out               = {};
            out.sin_family    = AF_INET;
            out.sin_port      = htons(addr.port);
            out.sin_addr.s_addr = htonl(addr.ipv4);
        }
    } // namespace

    TcpSocket::~TcpSocket()
    {
        close();
    }

    std::unique_ptr<TcpSocket> TcpSocket::adopt(uintptr_t sock, Address local, Address peer)
    {
        if (sock == kInvalidSocket)
            return nullptr;
        auto s          = std::unique_ptr<TcpSocket>(new TcpSocket());
        s->m_socket     = sock;
        s->m_local      = local;
        s->m_peer       = peer;
        s->m_started    = true;
        s->m_connecting = false;
        s->m_open       = true;
        return s;
    }

    bool TcpSocket::connect(const Address& dest)
    {
        close();
        if (dest.ipv4 == 0 || dest.port == 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: connect missing address");
            return false;
        }
        if (!NetSockets::startup())
            return false;
        m_started = true;

        const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: socket() failed ({})", WSAGetLastError());
            close();
            return false;
        }
        m_socket = static_cast<uintptr_t>(s);
        if (!setNonBlocking(s))
        {
            close();
            return false;
        }

        sockaddr_in to{};
        toSockaddr(dest, to);
        const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        if (rc == 0)
        {
            m_peer       = dest;
            m_connecting = false;
            m_open       = true;
            refreshLocal();
            return true;
        }

        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
        {
            m_peer       = dest;
            m_connecting = true;
            m_open       = true;
            return true;
        }

        DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: connect failed ({})", err);
        close();
        return false;
    }

    bool TcpSocket::finishConnect()
    {
        if (!m_connecting)
            return m_open;
        if (m_socket == kInvalidSocket)
            return false;

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(asSocket(m_socket), &writeSet);
        fd_set exceptSet;
        FD_ZERO(&exceptSet);
        FD_SET(asSocket(m_socket), &exceptSet);
        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 0;
        const int n = ::select(0, nullptr, &writeSet, &exceptSet, &tv);
        if (n == 0)
            return true;
        if (n == SOCKET_ERROR)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: select failed ({})", WSAGetLastError());
            close();
            return false;
        }

        int       soErr = 0;
        int       len   = sizeof(soErr);
        if (::getsockopt(asSocket(m_socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: getsockopt(SO_ERROR) failed ({})", WSAGetLastError());
            close();
            return false;
        }
        if (soErr != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpSocket: connect SO_ERROR ({})", soErr);
            close();
            return false;
        }

        m_connecting = false;
        refreshLocal();
        return true;
    }

    bool TcpSocket::refreshLocal()
    {
        if (m_socket == kInvalidSocket)
            return false;
        sockaddr_in bound{};
        int         boundLen = sizeof(bound);
        if (::getsockname(asSocket(m_socket), reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
        {
            m_local = fromSockaddr(bound);
            return true;
        }
        return false;
    }

    bool TcpSocket::pump()
    {
        if (!m_open)
            return false;
        if (m_connecting)
            return finishConnect();
        return true;
    }

    bool TcpSocket::write(const void* data, uint32_t size, uint32_t& written)
    {
        written = 0;
        if (!pump())
            return false;
        if (m_connecting)
            return true;
        if (size == 0)
            return true;
        if (!data)
            return false;

        const int n = ::send(asSocket(m_socket), static_cast<const char*>(data), static_cast<int>(size), 0);
        if (n == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return true;
            DE_LOG_WARN(LogCategory::Networking, "TcpSocket: send failed ({})", err);
            close();
            return false;
        }
        written = static_cast<uint32_t>(n);
        return true;
    }

    bool TcpSocket::read(void* dst, uint32_t capacity, uint32_t& outSize)
    {
        outSize = 0;
        if (!pump())
            return false;
        if (m_connecting)
            return true;
        if (capacity == 0)
            return true;
        if (!dst)
            return false;

        const int n = ::recv(asSocket(m_socket), static_cast<char*>(dst), static_cast<int>(capacity), 0);
        if (n == 0)
        {
            close();
            return false;
        }
        if (n == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return true;
            DE_LOG_WARN(LogCategory::Networking, "TcpSocket: recv failed ({})", err);
            close();
            return false;
        }
        outSize = static_cast<uint32_t>(n);
        return true;
    }

    bool TcpSocket::isOpen() const
    {
        return m_open;
    }

    void TcpSocket::close()
    {
        if (m_socket != kInvalidSocket)
        {
            const SOCKET s = asSocket(m_socket);
            m_socket       = kInvalidSocket;
            ::shutdown(s, SD_BOTH);
            if (::closesocket(s) == SOCKET_ERROR)
                DE_LOG_WARN(LogCategory::Networking, "TcpSocket: closesocket failed ({})", WSAGetLastError());
        }
        m_local      = {};
        m_peer       = {};
        m_connecting = false;
        m_open       = false;
        if (m_started)
        {
            m_started = false;
            NetSockets::shutdown();
        }
    }

    TcpListener::~TcpListener()
    {
        close();
    }

    bool TcpListener::listen(uint16_t port)
    {
        close();
        if (!NetSockets::startup())
            return false;
        m_started = true;

        const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpListener: socket() failed ({})", WSAGetLastError());
            close();
            return false;
        }
        m_socket = static_cast<uintptr_t>(s);
        if (!setNonBlocking(s))
        {
            close();
            return false;
        }

        BOOL reuse = TRUE;
        if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) != 0)
            DE_LOG_WARN(LogCategory::Networking, "TcpListener: SO_REUSEADDR failed ({})", WSAGetLastError());

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpListener: bind({}) failed ({})", port, WSAGetLastError());
            close();
            return false;
        }
        if (::listen(s, 1) != 0)
        {
            DE_LOG_ERROR(LogCategory::Networking, "TcpListener: listen failed ({})", WSAGetLastError());
            close();
            return false;
        }

        sockaddr_in bound{};
        int         boundLen = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0)
            m_local = fromSockaddr(bound);
        else
            m_local.port = port;

        m_open = true;
        DE_LOG_INFO(LogCategory::Networking, "TcpListener: listening port {}", m_local.port);
        return true;
    }

    bool TcpListener::tryAccept(std::unique_ptr<IByteStream>& out)
    {
        out.reset();
        if (!m_open || m_socket == kInvalidSocket)
            return false;

        sockaddr_in from{};
        int         fromLen = sizeof(from);
        const SOCKET c      = ::accept(asSocket(m_socket), reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (c == INVALID_SOCKET)
        {
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return false;
            DE_LOG_WARN(LogCategory::Networking, "TcpListener: accept failed ({})", err);
            return false;
        }
        if (!setNonBlocking(c))
        {
            ::closesocket(c);
            return false;
        }
        if (!NetSockets::startup())
        {
            ::closesocket(c);
            return false;
        }

        const Address peer  = fromSockaddr(from);
        Address       local = m_local;
        sockaddr_in   loc{};
        int           locLen = sizeof(loc);
        if (::getsockname(c, reinterpret_cast<sockaddr*>(&loc), &locLen) == 0)
            local = fromSockaddr(loc);

        out = TcpSocket::adopt(static_cast<uintptr_t>(c), local, peer);
        if (!out)
        {
            ::closesocket(c);
            NetSockets::shutdown();
            return false;
        }
        return true;
    }

    void TcpListener::close()
    {
        if (m_socket != kInvalidSocket)
        {
            const SOCKET s = asSocket(m_socket);
            m_socket       = kInvalidSocket;
            if (::closesocket(s) == SOCKET_ERROR)
                DE_LOG_WARN(LogCategory::Networking, "TcpListener: closesocket failed ({})", WSAGetLastError());
        }
        m_local = {};
        m_open  = false;
        if (m_started)
        {
            m_started = false;
            NetSockets::shutdown();
        }
    }

} // namespace Dark
