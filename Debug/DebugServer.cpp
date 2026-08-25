#include "Debug/DebugServer.h"
#include "Core/MemoryTracker.h"
#include "ECS/World.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>

#include <chrono>
#include <cstring>

namespace Dark
{

    namespace
    {
        DebugServer* g_logTarget = nullptr;

        uint64_t nowMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        bool categoryBitOn(uint32_t mask, LogCategory cat)
        {
            const uint32_t i = static_cast<uint32_t>(cat);
            if (i >= 32)
                return true;
            return (mask & (1u << i)) != 0;
        }

        bool shouldSendLog(LogLevel level, LogCategory cat, LogLevel minLevel, uint32_t mask)
        {
            if (static_cast<int>(level) < static_cast<int>(minLevel))
            {
                if (level != LogLevel::Error && level != LogLevel::Fatal)
                    return false;
            }
            if (level == LogLevel::Error || level == LogLevel::Fatal)
                return true;
            return categoryBitOn(mask, cat);
        }
    } // namespace

    DebugServer::DebugServer()
    {
        m_logRing.resize(kDebugLogRing);
    }

    DebugServer::~DebugServer()
    {
        shutdown();
    }

    void DebugServer::setListener(IByteListener* listener)
    {
        m_injectedListener = listener;
    }

    void DebugServer::setAppTitle(const char* title)
    {
        copyDebugName(m_title, title ? title : "DarkEngine6");
    }

    bool DebugServer::listen(uint16_t port)
    {
        shutdown();
        m_port = port != 0 ? port : kDebugDefaultPort;
        if (m_title[0] == 0)
            copyDebugName(m_title, "DarkEngine6");

        if (m_injectedListener)
        {
            m_listener = m_injectedListener;
            if (!m_listener->listen(m_port))
            {
                m_listener = nullptr;
                return false;
            }
        }
        else
        {
            m_ownedListener = std::unique_ptr<TcpListener>(new TcpListener());
            if (!m_ownedListener->listen(m_port))
            {
                m_ownedListener.reset();
                return false;
            }
            m_listener = m_ownedListener.get();
        }

        m_bound      = m_listener->localAddress();
        m_listening  = true;
        Log::addCapture(&DebugServer::onLogCapture);
        m_captureInstalled = true;
        g_logTarget        = this;
        DE_LOG_INFO(LogCategory::Debug, "DebugServer: listening on TCP {}", m_bound.port);
        return true;
    }

    void DebugServer::shutdown()
    {
        closeClient();
        if (m_captureInstalled)
        {
            Log::removeCapture(&DebugServer::onLogCapture);
            m_captureInstalled = false;
        }
        if (g_logTarget == this)
            g_logTarget = nullptr;
        if (m_listener)
            m_listener->close();
        m_listener = nullptr;
        m_ownedListener.reset();
        m_listening = false;
        m_bound     = {};
    }

    bool DebugServer::isListening() const
    {
        return m_listening;
    }

    bool DebugServer::hasClient() const
    {
        return m_client && m_client->isOpen();
    }

    void DebugServer::onLogCapture(LogLevel level, LogCategory category, const char* message)
    {
        if (g_logTarget)
            g_logTarget->pushLog(level, category, message);
    }

    void DebugServer::pushLog(LogLevel level, LogCategory category, const char* message)
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        if (m_logCount == kDebugLogRing)
        {
            m_logHead = (m_logHead + 1) % kDebugLogRing;
            --m_logCount;
            if (m_dropped < 0xFFFFu)
                ++m_dropped;
        }
        DebugLogEntry& e = m_logRing[m_logTail];
        e                = {};
        e.timestampMs    = nowMs();
        e.level          = level;
        e.category       = category;
        e.dropped        = m_dropped;
        m_dropped        = 0;
        if (message)
        {
            uint32_t n = 0;
            while (message[n] && n < kDebugMaxLogText)
            {
                e.text[n] = message[n];
                ++n;
            }
            e.text[n] = 0;
        }
        m_logTail = (m_logTail + 1) % kDebugLogRing;
        ++m_logCount;
    }

    void DebugServer::closeClient()
    {
        if (m_client)
        {
            m_client->close();
            m_client.reset();
        }
        m_framer.reset();
        m_sendBuf.clear();
        m_sendPos   = 0;
        m_handshook = false;
        m_recvAge   = 0.0f;
        m_hbAcc     = 0.0f;
        m_subscribe = DebugChannelAll;
    }

    bool DebugServer::queueOpcode(DebugOpcode op, const uint8_t* payload, uint32_t n)
    {
        uint32_t size = 0;
        if (!writeDebugFrame(m_scratch, sizeof(m_scratch), op, payload, n, size))
            return false;
        const uint32_t pending = static_cast<uint32_t>(m_sendBuf.size() > m_sendPos ? m_sendBuf.size() - m_sendPos : 0);
        if (pending + size > kDebugSendCapBytes * 4)
            return false;
        m_sendBuf.insert(m_sendBuf.end(), m_scratch, m_scratch + size);
        return true;
    }

    void DebugServer::pumpSend()
    {
        if (!m_client || m_sendPos >= m_sendBuf.size())
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
        if (!m_client->write(m_sendBuf.data() + m_sendPos, remain, written))
        {
            closeClient();
            return;
        }
        m_sendPos += written;
        if (m_sendPos >= m_sendBuf.size())
        {
            m_sendBuf.clear();
            m_sendPos = 0;
        }
        else if (m_sendPos > 4096)
        {
            m_sendBuf.erase(m_sendBuf.begin(), m_sendBuf.begin() + static_cast<std::ptrdiff_t>(m_sendPos));
            m_sendPos = 0;
        }
    }

    void DebugServer::handleMessage(const DebugHeader& hdr, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        if (size > 0)
        {
            if (!payload || !r.begin(payload, size))
                return;
        }
        else if (!r.begin(reinterpret_cast<const uint8_t*>(""), 0))
            return;

        switch (hdr.opcode)
        {
        case DebugOpcode::Hello:
        {
            DebugHello hello{};
            if (!readHelloPayload(r, hello))
                return;
            m_handshook = true;
            m_subscribe = hello.channels != 0 ? hello.channels : DebugChannelAll;
            DebugHelloAck ack{};
            ack.protocolVersion = kDebugProtocolVersion;
            ack.listenPort      = m_bound.port;
            std::memcpy(ack.title, m_title, kDebugNameBytes);
            uint8_t pbuf[64]{};
            PacketWriter w;
            if (!w.begin(pbuf, sizeof(pbuf)) || !writeHelloAckPayload(w, ack))
                return;
            queueOpcode(DebugOpcode::HelloAck, pbuf, w.size());
            DE_LOG_INFO(LogCategory::Debug, "DebugServer: debugger connected ({})", hello.name);
            break;
        }
        case DebugOpcode::Subscribe:
        {
            uint32_t ch = 0;
            if (readSubscribePayload(r, ch))
                m_subscribe = ch;
            break;
        }
        case DebugOpcode::SetLogFilter:
        {
            LogLevel lv = LogLevel::Trace;
            uint32_t mask = 0;
            if (readLogFilterPayload(r, lv, mask))
            {
                m_minLevel      = lv;
                m_categoryMask  = mask;
            }
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

    void DebugServer::pumpRecv()
    {
        if (!m_client)
            return;
        uint8_t  buf[2048];
        uint32_t frames = 0;
        for (; frames < kDebugRecvBudget; ++frames)
        {
            uint32_t n = 0;
            if (!m_client->read(buf, sizeof(buf), n))
            {
                closeClient();
                return;
            }
            if (n == 0)
                break;
            m_recvAge = 0.0f;
            if (!m_framer.pushBytes(buf, n))
            {
                DE_LOG_WARN(LogCategory::Debug, "DebugServer: framing error");
                closeClient();
                return;
            }
            DebugHeader          hdr{};
            std::vector<uint8_t> payload;
            while (m_framer.popMessage(hdr, payload))
                handleMessage(hdr, payload.empty() ? nullptr : payload.data(), static_cast<uint32_t>(payload.size()));
            if (!m_framer.ok())
            {
                DE_LOG_WARN(LogCategory::Debug, "DebugServer: bad debug frame");
                closeClient();
                return;
            }
        }
    }

    void DebugServer::poll(float dt)
    {
        if (!m_listening || !m_listener)
            return;
        if (dt < 0.0f)
            dt = 0.0f;

        if (!m_client)
        {
            std::unique_ptr<IByteStream> incoming;
            if (m_listener->tryAccept(incoming) && incoming)
            {
                m_client    = std::move(incoming);
                m_framer.reset();
                m_sendBuf.clear();
                m_sendPos   = 0;
                m_handshook = false;
                m_recvAge   = 0.0f;
                m_hbAcc     = 0.0f;
            }
        }
        else
        {
            std::unique_ptr<IByteStream> extra;
            if (m_listener->tryAccept(extra) && extra)
            {
                extra->close();
                DE_LOG_INFO(LogCategory::Debug, "DebugServer: extra debugger rejected (one client)");
            }
            if (!m_client->pump())
            {
                closeClient();
                return;
            }
            pumpRecv();
            if (m_client)
            {
                m_recvAge += dt;
                if (m_recvAge > kDebugTimeoutSec && m_handshook)
                {
                    DE_LOG_WARN(LogCategory::Debug, "DebugServer: debugger timed out");
                    closeClient();
                }
            }
        }
        pumpSend();
    }

    void DebugServer::sendPendingLogs()
    {
        if ((m_subscribe & DebugChannelLog) == 0)
            return;
        DebugLogEntry batch[kDebugLogFlushBudget];
        uint32_t      n = 0;
        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            while (m_logCount > 0 && n < kDebugLogFlushBudget)
            {
                batch[n++] = m_logRing[m_logHead];
                m_logHead  = (m_logHead + 1) % kDebugLogRing;
                --m_logCount;
            }
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            if (!shouldSendLog(batch[i].level, batch[i].category, m_minLevel, m_categoryMask))
                continue;
            uint8_t      pbuf[kDebugMaxLogText + 32]{};
            PacketWriter w;
            if (!w.begin(pbuf, sizeof(pbuf)) || !writeLogLinePayload(w, batch[i]))
                continue;
            queueOpcode(DebugOpcode::LogLine, pbuf, w.size());
        }
    }

    void DebugServer::captureProcessMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        {
            MemoryTracker::set("Process/WorkingSet", static_cast<uint64_t>(pmc.WorkingSetSize), static_cast<uint64_t>(pmc.PeakWorkingSetSize), 1);
            MemoryTracker::set("Process/Private", static_cast<uint64_t>(pmc.PrivateUsage), static_cast<uint64_t>(pmc.PrivateUsage), 1);
        }
    }

    void DebugServer::collectAndSendMemory(World* world)
    {
        if ((m_subscribe & DebugChannelMemory) == 0)
            return;
        captureProcessMemory();

        DebugMemoryPool pools[kDebugMaxPools]{};
        uint32_t        count = 0;

        if (world)
        {
            world->forEachPool([&](const IComponentPool& pool) {
                if (count >= kDebugMaxPools)
                    return;
                copyDebugName(pools[count].name, pool.typeName());
                pools[count].used     = pool.bytesUsed();
                pools[count].capacity = pool.bytesCapacity();
                pools[count].count    = pool.count();
                ++count;
            });
        }

        DebugMemoryPool tracked[kDebugMaxPools]{};
        uint32_t        trackedN = 0;
        MemoryTracker::snapshot(tracked, kDebugMaxPools, trackedN);
        for (uint32_t i = 0; i < trackedN && count < kDebugMaxPools; ++i)
            pools[count++] = tracked[i];

        uint8_t      pbuf[4096]{};
        PacketWriter w;
        if (!w.begin(pbuf, sizeof(pbuf)) || !writeMemorySnapshotPayload(w, pools, count))
            return;
        queueOpcode(DebugOpcode::MemorySnapshot, pbuf, w.size());
    }

    void DebugServer::sendPerf()
    {
        if ((m_subscribe & DebugChannelPerf) == 0)
            return;
        uint8_t      pbuf[256]{};
        PacketWriter w;
        if (!w.begin(pbuf, sizeof(pbuf)) || !writePerfSnapshotPayload(w, m_perf.last()))
            return;
        queueOpcode(DebugOpcode::PerfSnapshot, pbuf, w.size());
    }

    void DebugServer::flush(World* world, const DebugFrameStats& stats, uint64_t packetsIn, uint64_t packetsOut, float rttMs, float dt)
    {
        (void)stats;
        if (!m_listening)
            return;
        if (dt < 0.0f)
            dt = 0.0f;

        if (m_client && m_handshook)
        {
            sendPendingLogs();
            m_memoryAcc += dt;
            m_perfAcc += dt;
            m_hbAcc += dt;
            if (m_memoryAcc >= 1.0f / kDebugMemoryHz)
            {
                m_memoryAcc = 0.0f;
                collectAndSendMemory(world);
            }
            if (m_perfAcc >= 1.0f / kDebugPerfHz)
            {
                m_perfAcc = 0.0f;
                sendPerf();
            }
            if (m_hbAcc >= kDebugHeartbeatSec)
            {
                m_hbAcc = 0.0f;
                ++m_hbCounter;
                uint8_t      pbuf[8]{};
                PacketWriter w;
                if (w.begin(pbuf, sizeof(pbuf)) && writeHeartbeatPayload(w, m_hbCounter))
                    queueOpcode(DebugOpcode::Heartbeat, pbuf, w.size());
            }
        }
        pumpSend();
        (void)packetsIn;
        (void)packetsOut;
        (void)rttMs;
    }

} // namespace Dark
