#include "Network/FakeTransport.h"
#include "Core/Log.h"

#include <cstring>
#include <utility>

namespace Dark
{

    void FakeHub::registerEndpoint(Address addr, FakeTransport& transport)
    {
        for (Slot& slot : m_slots)
        {
            if (slot.addr == addr)
            {
                slot.transport = &transport;
                return;
            }
        }
        m_slots.push_back(Slot{addr, &transport});
    }

    void FakeHub::unregister(Address addr)
    {
        for (size_t i = 0; i < m_slots.size(); ++i)
        {
            if (m_slots[i].addr == addr)
            {
                m_slots[i] = m_slots.back();
                m_slots.pop_back();
                return;
            }
        }
    }

    void FakeHub::setDropRate(float p)
    {
        if (p < 0.0f)
            p = 0.0f;
        if (p > 1.0f)
            p = 1.0f;
        m_dropRate = p;
    }

    void FakeHub::dropNext(uint32_t n)
    {
        m_dropNext = n;
    }

    bool FakeHub::consumeDrop()
    {
        if (m_dropNext > 0)
        {
            --m_dropNext;
            return true;
        }
        if (m_dropRate <= 0.0f)
            return false;
        if (m_dropRate >= 1.0f)
            return true;
        m_rng             = m_rng * 1664525u + 1013904223u;
        const float unit = static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
        return unit < m_dropRate;
    }

    void FakeHub::send(const Address& src, const Address& dest, const void* data, uint32_t size)
    {
        if (consumeDrop())
            return;
        if (size > 0 && !data)
            return;

        const bool broadcast = (dest.ipv4 == 0xFFFFFFFFu);
        if (broadcast)
        {
            for (Slot& slot : m_slots)
            {
                if (slot.transport && !(slot.addr == src))
                    slot.transport->deliver(src, data, size);
            }
            return;
        }

        for (Slot& slot : m_slots)
        {
            if (slot.addr == dest && slot.transport)
            {
                slot.transport->deliver(src, data, size);
                return;
            }
        }
    }

    FakeTransport::FakeTransport(FakeHub& hub, Address self)
        : m_hub(&hub)
        , m_self(self)
    {
        hub.registerEndpoint(self, *this);
    }

    FakeTransport::~FakeTransport()
    {
        close();
    }

    void FakeTransport::deliver(const Address& src, const void* data, uint32_t size)
    {
        if (!m_open)
            return;
        Datagram d;
        d.src = src;
        if (size > 0 && data)
            d.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        m_inbox.push(std::move(d));
    }

    bool FakeTransport::sendTo(const Address& dest, const void* data, uint32_t size)
    {
        if (!m_open || !m_hub)
            return false;
        if (size > kNetMaxPayload)
        {
            DE_LOG_WARN(LogCategory::Networking, "FakeTransport: sendTo payload {} exceeds {}", size, kNetMaxPayload);
            return false;
        }
        if (size > 0 && !data)
            return false;
        m_hub->send(m_self, dest, data, size);
        return true;
    }

    bool FakeTransport::recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize)
    {
        outSize = 0;
        if (!m_open || m_inbox.empty())
            return false;

        Datagram d = std::move(m_inbox.front());
        m_inbox.pop();
        src = d.src;

        const uint32_t bytes = static_cast<uint32_t>(d.bytes.size());
        if (bytes == 0 || bytes > kNetMaxPayload || bytes > capacity || !buffer)
        {
            outSize = 0;
            return true;
        }
        std::memcpy(buffer, d.bytes.data(), bytes);
        outSize = bytes;
        return true;
    }

    void FakeTransport::close()
    {
        if (!m_open)
            return;
        m_open = false;
        while (!m_inbox.empty())
            m_inbox.pop();
        if (m_hub)
        {
            m_hub->unregister(m_self);
            m_hub = nullptr;
        }
    }

} // namespace Dark
