#pragma once

#include "Network/NetTypes.h"

#include <cstdint>
#include <memory>

namespace Dark
{

    // Byte stream (TCP or FakeTcp). Public headers stay Winsock-free.
    class IByteStream
    {
    public:
        virtual ~IByteStream() = default;

        // Pump connecting sockets. Default: still-open check.
        virtual bool pump() { return isOpen(); }

        // true: wrote 0..size bytes (0 = would-block). false: closed/error.
        virtual bool write(const void* data, uint32_t size, uint32_t& written) = 0;
        // true: read 0..capacity bytes (0 = would-block). false: closed/error.
        virtual bool read(void* dst, uint32_t capacity, uint32_t& outSize) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;
        virtual bool isConnecting() const { return false; }
        virtual Address localAddress() const = 0;
        virtual Address peerAddress() const = 0;
    };

    class IByteListener
    {
    public:
        virtual ~IByteListener() = default;

        virtual bool listen(uint16_t port) = 0;
        virtual bool tryAccept(std::unique_ptr<IByteStream>& out) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;
        virtual Address localAddress() const = 0;
    };

    class TcpSocket final : public IByteStream
    {
    public:
        TcpSocket() = default;
        ~TcpSocket() override;

        TcpSocket(const TcpSocket&)            = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;

        // Non-blocking connect. Returns true if the socket was created (may still be connecting).
        bool connect(const Address& dest);

        bool        pump() override;
        bool        write(const void* data, uint32_t size, uint32_t& written) override;
        bool        read(void* dst, uint32_t capacity, uint32_t& outSize) override;
        void        close() override;
        bool        isOpen() const override;
        bool        isConnecting() const override { return m_connecting; }
        Address     localAddress() const override { return m_local; }
        Address     peerAddress() const override { return m_peer; }

        static std::unique_ptr<TcpSocket> adopt(uintptr_t sock, Address local, Address peer);

    private:
        bool finishConnect();
        bool refreshLocal();

        uintptr_t m_socket     = static_cast<uintptr_t>(-1);
        Address   m_local{};
        Address   m_peer{};
        bool      m_started    = false;
        bool      m_connecting = false;
        bool      m_open       = false;
    };

    class TcpListener final : public IByteListener
    {
    public:
        TcpListener() = default;
        ~TcpListener() override;

        TcpListener(const TcpListener&)            = delete;
        TcpListener& operator=(const TcpListener&) = delete;

        bool    listen(uint16_t port) override;
        bool    tryAccept(std::unique_ptr<IByteStream>& out) override;
        void    close() override;
        bool    isOpen() const override { return m_open; }
        Address localAddress() const override { return m_local; }

    private:
        uintptr_t m_socket  = static_cast<uintptr_t>(-1);
        Address   m_local{};
        bool      m_started = false;
        bool      m_open    = false;
    };

} // namespace Dark
