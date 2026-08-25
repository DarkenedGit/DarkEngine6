#include "Debug/DebugClient.h"
#include "Core/Log.h"

#include <cstring>

namespace Dark
{

    DebugClient::DebugClient()
    {
        m_logs.resize(kDebugLogRing);
    }

    DebugClient::~DebugClient()
    {
        disconnect();
    }

    void DebugClient::setStream(IByteStream* stream)
    {
        disconnect();
        m_injected = stream;
        m_stream   = stream;
        if (m_stream && m_stream->isOpen())
        {
            sendHello();
            m_helloSent = true;
        }
    }

    bool DebugClient::connect(const Address& dest)
    {
        disconnect();
        m_owned = std::unique_ptr<TcpSocket>(new TcpSocket());
        if (!m_owned->connect(dest))
        {
            m_owned.reset();
            return false;
        }
        m_stream = m_owned.get();
        if (!m_stream->isConnecting())
        {
            sendHello();
            m_helloSent = true;
        }
        return true;
    }

    void DebugClient::closeStream()
    {
        if (m_owned)
        {
            m_owned->close();
            m_owned.reset();
        }
        m_stream    = nullptr;
        m_injected  = nullptr;
        m_framer.reset();
        m_sendBuf.clear();
        m_sendPos   = 0;
        m_handshook = false;
        m_helloSent = false;
        m_recvAge   = 0.0f;
        m_hbAcc     = 0.0f;
        m_hostTitle[0] = 0;
    }

    void DebugClient::disconnect()
    {
        closeStream();
    }

    bool DebugClient::isConnected() const
    {
        return m_stream && m_stream->isOpen() && !m_stream->isConnecting() && m_handshook;
    }

    bool DebugClient::isConnecting() const
    {
        if (!m_stream || !m_stream->isOpen())
            return false;
        if (m_stream->isConnecting())
            return true;
        return !m_handshook;
    }

    Address DebugClient::peerAddress() const
    {
        if (m_stream)
            return m_stream->peerAddress();
        return {};
    }

    bool DebugClient::queueOpcode(DebugOpcode op, const uint8_t* payload, uint32_t n)
    {
        uint32_t size = 0;
        if (!writeDebugFrame(m_scratch, sizeof(m_scratch), op, payload, n, size))
            return false;
        m_sendBuf.insert(m_sendBuf.end(), m_scratch, m_scratch + size);
        return true;
    }

    bool DebugClient::sendHello()
    {
        DebugHello hello{};
        copyDebugName(hello.name, "VisualDebugger");
        hello.pid      = 0;
        hello.channels = m_subscribe;
        uint8_t      pbuf[64]{};
        PacketWriter w;
        if (!w.begin(pbuf, sizeof(pbuf)) || !writeHelloPayload(w, hello))
            return false;
        return queueOpcode(DebugOpcode::Hello, pbuf, w.size());
    }

    void DebugClient::sendSubscribeAndFilter()
    {
        uint8_t      pbuf[16]{};
        PacketWriter w;
        if (w.begin(pbuf, sizeof(pbuf)) && writeSubscribePayload(w, m_subscribe))
            queueOpcode(DebugOpcode::Subscribe, pbuf, w.size());
        PacketWriter w2;
        uint8_t      pbuf2[16]{};
        if (w2.begin(pbuf2, sizeof(pbuf2)) && writeLogFilterPayload(w2, m_minLevel, m_categoryMask))
            queueOpcode(DebugOpcode::SetLogFilter, pbuf2, w2.size());
    }

    void DebugClient::setSubscribeMask(uint32_t mask)
    {
        m_subscribe = mask;
        if (m_handshook)
            sendSubscribeAndFilter();
    }

    void DebugClient::setLogFilter(LogLevel minLevel, uint32_t categoryMask)
    {
        m_minLevel      = minLevel;
        m_categoryMask  = categoryMask;
        if (m_handshook)
            sendSubscribeAndFilter();
    }

    void DebugClient::pumpSend()
    {
        if (!m_stream || m_sendPos >= m_sendBuf.size())
        {
            if (m_sendPos > 0)
            {
                m_sendBuf.clear();
                m_sendPos = 0;
            }
            return;
        }
        uint32_t written = 0;
        const uint32_t remain = static_cast<uint32_t>(m_sendBuf.size() - m_sendPos);
        if (!m_stream->write(m_sendBuf.data() + m_sendPos, remain, written))
        {
            closeStream();
            return;
        }
        m_sendPos += written;
        if (m_sendPos >= m_sendBuf.size())
        {
            m_sendBuf.clear();
            m_sendPos = 0;
        }
    }

    void DebugClient::handleMessage(const DebugHeader& hdr, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        if (size > 0)
        {
            if (!payload || !r.begin(payload, size))
                return;
        }
        else
        {
            static const uint8_t kEmpty = 0;
            if (!r.begin(&kEmpty, 0))
                return;
        }

        switch (hdr.opcode)
        {
        case DebugOpcode::HelloAck:
        {
            DebugHelloAck ack{};
            if (!readHelloAckPayload(r, ack))
                return;
            std::memcpy(m_hostTitle, ack.title, kDebugNameBytes);
            m_hostTitle[kDebugNameBytes - 1] = 0;
            m_handshook = true;
            sendSubscribeAndFilter();
            DE_LOG_INFO(LogCategory::Debug, "DebugClient: connected to '{}'", m_hostTitle);
            break;
        }
        case DebugOpcode::LogLine:
        {
            DebugLogEntry e{};
            if (!readLogLinePayload(r, e))
                return;
            if (m_logCount == kDebugLogRing)
            {
                m_logHead = (m_logHead + 1) % kDebugLogRing;
                --m_logCount;
            }
            const uint32_t tail     = (m_logHead + m_logCount) % kDebugLogRing;
            m_logs[tail] = e;
            ++m_logCount;
            break;
        }
        case DebugOpcode::MemorySnapshot:
        {
            uint32_t n = 0;
            if (!readMemorySnapshotPayload(r, m_pools, kDebugMaxPools, n))
                return;
            m_poolCount = n;
            break;
        }
        case DebugOpcode::PerfSnapshot:
        {
            if (!readPerfSnapshotPayload(r, m_perf))
                return;
            m_history[m_histWrite] = m_perf.dtMs;
            m_histWrite            = (m_histWrite + 1) % 240;
            if (m_histCount < 240)
                ++m_histCount;
            break;
        }
        case DebugOpcode::Heartbeat:
        {
            uint32_t c = 0;
            readHeartbeatPayload(r, c);
            break;
        }
        default:
            break;
        }
    }

    void DebugClient::pumpRecv()
    {
        if (!m_stream)
            return;
        uint8_t buf[2048];
        for (uint32_t frames = 0; frames < kDebugRecvBudget; ++frames)
        {
            uint32_t n = 0;
            if (!m_stream->read(buf, sizeof(buf), n))
            {
                closeStream();
                return;
            }
            if (n == 0)
                break;
            m_recvAge = 0.0f;
            if (!m_framer.pushBytes(buf, n))
            {
                closeStream();
                return;
            }
            DebugHeader          hdr{};
            std::vector<uint8_t> payload;
            while (m_framer.popMessage(hdr, payload))
                handleMessage(hdr, payload.empty() ? nullptr : payload.data(), static_cast<uint32_t>(payload.size()));
            if (!m_framer.ok())
            {
                closeStream();
                return;
            }
        }
    }

    void DebugClient::poll(float dt)
    {
        if (!m_stream)
            return;
        if (dt < 0.0f)
            dt = 0.0f;

        if (!m_stream->pump())
        {
            closeStream();
            return;
        }
        if (m_stream->isConnecting())
            return;
        if (!m_handshook && !m_helloSent)
        {
            sendHello();
            m_helloSent = true;
        }

        pumpRecv();
        if (!m_stream)
            return;

        m_recvAge += dt;
        m_hbAcc += dt;
        if (m_handshook && m_recvAge > kDebugTimeoutSec)
        {
            DE_LOG_WARN(LogCategory::Debug, "DebugClient: host timed out");
            closeStream();
            return;
        }
        if (m_handshook && m_hbAcc >= kDebugHeartbeatSec)
        {
            m_hbAcc = 0.0f;
            ++m_hbCounter;
            uint8_t      pbuf[8]{};
            PacketWriter w;
            if (w.begin(pbuf, sizeof(pbuf)) && writeHeartbeatPayload(w, m_hbCounter))
                queueOpcode(DebugOpcode::Heartbeat, pbuf, w.size());
        }
        pumpSend();
    }

    bool DebugClient::logAt(uint32_t i, DebugLogEntry& out) const
    {
        if (i >= m_logCount)
            return false;
        out = m_logs[(m_logHead + i) % kDebugLogRing];
        return true;
    }

    void DebugClient::clearLogs()
    {
        m_logHead  = 0;
        m_logCount = 0;
    }

    bool DebugClient::poolAt(uint32_t i, DebugMemoryPool& out) const
    {
        if (i >= m_poolCount)
            return false;
        out = m_pools[i];
        return true;
    }

    bool DebugClient::frameHistoryAt(uint32_t i, float& frameMs) const
    {
        if (i >= m_histCount)
            return false;
        const uint32_t oldest = (m_histCount == 240) ? m_histWrite : 0;
        frameMs               = m_history[(oldest + i) % 240];
        return true;
    }

} // namespace Dark
