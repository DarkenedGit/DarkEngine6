#pragma once

#include "Network/TcpSocket.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <queue>
#include <vector>

namespace Dark
{

    class FakeTcpStream;
    class FakeTcpListener;

    struct FakeTcpPipe
    {
        std::deque<uint8_t> aToB;
        std::deque<uint8_t> bToA;
        bool                open = true;
    };

    class FakeTcpHub
    {
    public:
        void registerListener(uint16_t port, FakeTcpListener& listener);
        void unregisterListener(uint16_t port);

        bool connectClient(FakeTcpStream& client, uint16_t port);

    private:
        struct Slot
        {
            uint16_t         port     = 0;
            FakeTcpListener* listener = nullptr;
        };
        std::vector<Slot> m_slots;
    };

    class FakeTcpStream final : public IByteStream
    {
    public:
        FakeTcpStream() = default;
        ~FakeTcpStream() override;

        FakeTcpStream(const FakeTcpStream&)            = delete;
        FakeTcpStream& operator=(const FakeTcpStream&) = delete;

        void attach(std::shared_ptr<FakeTcpPipe> pipe, bool isA, Address local, Address peer);

        bool    write(const void* data, uint32_t size, uint32_t& written) override;
        bool    read(void* dst, uint32_t capacity, uint32_t& outSize) override;
        void    close() override;
        bool    isOpen() const override;
        Address localAddress() const override { return m_local; }
        Address peerAddress() const override { return m_peer; }

    private:
        std::shared_ptr<FakeTcpPipe> m_pipe;
        bool                         m_isA  = true;
        Address                      m_local{};
        Address                      m_peer{};
    };

    class FakeTcpListener final : public IByteListener
    {
    public:
        explicit FakeTcpListener(FakeTcpHub& hub);
        ~FakeTcpListener() override;

        FakeTcpListener(const FakeTcpListener&)            = delete;
        FakeTcpListener& operator=(const FakeTcpListener&) = delete;

        bool    listen(uint16_t port) override;
        bool    tryAccept(std::unique_ptr<IByteStream>& out) override;
        void    close() override;
        bool    isOpen() const override { return m_open; }
        Address localAddress() const override { return m_local; }

        void enqueue(std::unique_ptr<FakeTcpStream> stream);

    private:
        FakeTcpHub*                           m_hub = nullptr;
        Address                               m_local{};
        bool                                  m_open = false;
        std::queue<std::unique_ptr<IByteStream>> m_pending;
    };

} // namespace Dark
