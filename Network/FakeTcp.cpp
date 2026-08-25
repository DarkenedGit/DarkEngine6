#include "Network/FakeTcp.h"

#include <cstring>

namespace Dark
{

    void FakeTcpHub::registerListener(uint16_t port, FakeTcpListener& listener)
    {
        for (Slot& s : m_slots)
        {
            if (s.port == port)
            {
                s.listener = &listener;
                return;
            }
        }
        m_slots.push_back(Slot{port, &listener});
    }

    void FakeTcpHub::unregisterListener(uint16_t port)
    {
        for (auto it = m_slots.begin(); it != m_slots.end(); ++it)
        {
            if (it->port == port)
            {
                m_slots.erase(it);
                return;
            }
        }
    }

    bool FakeTcpHub::connectClient(FakeTcpStream& client, uint16_t port)
    {
        FakeTcpListener* lis = nullptr;
        for (Slot& s : m_slots)
        {
            if (s.port == port && s.listener)
            {
                lis = s.listener;
                break;
            }
        }
        if (!lis || !lis->isOpen())
            return false;

        auto pipe   = std::make_shared<FakeTcpPipe>();
        auto server = std::unique_ptr<FakeTcpStream>(new FakeTcpStream());

        Address clientAddr{};
        clientAddr.ipv4 = 0x7F000001u;
        clientAddr.port = 1;
        Address serverAddr{};
        serverAddr.ipv4 = 0x7F000001u;
        serverAddr.port = port;

        client.attach(pipe, true, clientAddr, serverAddr);
        server->attach(pipe, false, serverAddr, clientAddr);
        lis->enqueue(std::move(server));
        return true;
    }

    FakeTcpStream::~FakeTcpStream()
    {
        close();
    }

    void FakeTcpStream::attach(std::shared_ptr<FakeTcpPipe> pipe, bool isA, Address local, Address peer)
    {
        close();
        m_pipe  = std::move(pipe);
        m_isA   = isA;
        m_local = local;
        m_peer  = peer;
    }

    bool FakeTcpStream::write(const void* data, uint32_t size, uint32_t& written)
    {
        written = 0;
        if (!isOpen())
            return false;
        if (size == 0)
            return true;
        if (!data)
            return false;

        auto& q = m_isA ? m_pipe->aToB : m_pipe->bToA;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        q.insert(q.end(), p, p + size);
        written = size;
        return true;
    }

    bool FakeTcpStream::read(void* dst, uint32_t capacity, uint32_t& outSize)
    {
        outSize = 0;
        if (!m_pipe)
            return false;
        auto& q = m_isA ? m_pipe->bToA : m_pipe->aToB;
        if (!m_pipe->open && q.empty())
        {
            close();
            return false;
        }
        if (!dst || capacity == 0)
            return m_pipe->open;

        const uint32_t n = static_cast<uint32_t>(q.size()) < capacity ? static_cast<uint32_t>(q.size()) : capacity;
        if (n == 0)
            return m_pipe->open;
        auto* out = static_cast<uint8_t*>(dst);
        for (uint32_t i = 0; i < n; ++i)
        {
            out[i] = q.front();
            q.pop_front();
        }
        outSize = n;
        return true;
    }

    void FakeTcpStream::close()
    {
        if (m_pipe)
        {
            m_pipe->open = false;
            m_pipe.reset();
        }
        m_local = {};
        m_peer  = {};
    }

    bool FakeTcpStream::isOpen() const
    {
        return m_pipe && m_pipe->open;
    }

    FakeTcpListener::FakeTcpListener(FakeTcpHub& hub)
        : m_hub(&hub)
    {
    }

    FakeTcpListener::~FakeTcpListener()
    {
        close();
    }

    bool FakeTcpListener::listen(uint16_t port)
    {
        close();
        if (!m_hub)
            return false;
        m_local.ipv4 = 0x7F000001u;
        m_local.port = port;
        m_open       = true;
        m_hub->registerListener(port, *this);
        return true;
    }

    bool FakeTcpListener::tryAccept(std::unique_ptr<IByteStream>& out)
    {
        out.reset();
        if (!m_open || m_pending.empty())
            return false;
        out = std::move(m_pending.front());
        m_pending.pop();
        return true;
    }

    void FakeTcpListener::close()
    {
        if (m_hub && m_open)
            m_hub->unregisterListener(m_local.port);
        while (!m_pending.empty())
            m_pending.pop();
        m_local = {};
        m_open  = false;
    }

    void FakeTcpListener::enqueue(std::unique_ptr<FakeTcpStream> stream)
    {
        m_pending.push(std::unique_ptr<IByteStream>(stream.release()));
    }

} // namespace Dark
