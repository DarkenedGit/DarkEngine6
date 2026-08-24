#pragma once

#include "Network/Transport.h"

#include <cstdint>
#include <queue>
#include <vector>

namespace Dark
{

    class FakeTransport;

    class FakeHub
    {
    public:
        void registerEndpoint(Address addr, FakeTransport& transport);
        void unregister(Address addr);

        void send(const Address& src, const Address& dest, const void* data, uint32_t size);

        void setDropRate(float p);
        void dropNext(uint32_t n);

    private:
        bool consumeDrop();

        struct Slot
        {
            Address        addr{};
            FakeTransport* transport = nullptr;
        };

        std::vector<Slot> m_slots;
        float             m_dropRate = 0.0f;
        uint32_t          m_dropNext = 0;
        uint32_t          m_rng      = 1;
    };

    class FakeTransport : public ITransport
    {
    public:
        FakeTransport(FakeHub& hub, Address self);
        ~FakeTransport() override;

        FakeTransport(const FakeTransport&)            = delete;
        FakeTransport& operator=(const FakeTransport&) = delete;

        bool sendTo(const Address& dest, const void* data, uint32_t size) override;
        bool recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize) override;
        Address localAddress() const override { return m_self; }
        void close() override;

    private:
        friend class FakeHub;
        void deliver(const Address& src, const void* data, uint32_t size);

        struct Datagram
        {
            Address              src{};
            std::vector<uint8_t> bytes;
        };

        FakeHub*            m_hub = nullptr;
        Address             m_self{};
        std::queue<Datagram> m_inbox;
        bool                m_open = true;
    };

} // namespace Dark
