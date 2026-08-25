#pragma once

#include "Debug/DebugProtocol.h"
#include "Debug/PerfCounters.h"
#include "Network/TcpSocket.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Dark
{

    class World;

    class DebugServer
    {
    public:
        DebugServer();
        ~DebugServer();

        DebugServer(const DebugServer&)            = delete;
        DebugServer& operator=(const DebugServer&) = delete;

        void setListener(IByteListener* listener); // non-owning; tests inject FakeTcpListener
        void setAppTitle(const char* title);

        bool listen(uint16_t port = kDebugDefaultPort);
        void shutdown();
        bool isListening() const;
        bool hasClient() const;
        Address boundAddress() const { return m_bound; }

        void poll(float dt);
        void flush(World* world, const DebugFrameStats& stats, uint64_t packetsIn, uint64_t packetsOut, float rttMs, float dt);

        PerfCounters&       perf() { return m_perf; }
        const PerfCounters& perf() const { return m_perf; }

        uint32_t subscribeMask() const { return m_subscribe; }

    private:
        static void onLogCapture(LogLevel level, LogCategory category, const char* message);

        void pushLog(LogLevel level, LogCategory category, const char* message);
        void closeClient();
        void handleMessage(const DebugHeader& hdr, const uint8_t* payload, uint32_t size);
        bool queueOpcode(DebugOpcode op, const uint8_t* payload, uint32_t n);
        void pumpSend();
        void pumpRecv();
        void collectAndSendMemory(World* world);
        void sendPerf();
        void sendPendingLogs();
        void captureProcessMemory();

        IByteListener*               m_injectedListener = nullptr;
        std::unique_ptr<TcpListener> m_ownedListener;
        IByteListener*               m_listener = nullptr;
        std::unique_ptr<IByteStream> m_client;
        DebugFramer                  m_framer;
        std::vector<uint8_t>         m_sendBuf;
        uint32_t                     m_sendPos = 0;

        char     m_title[kDebugNameBytes]{};
        Address  m_bound{};
        uint16_t m_port      = kDebugDefaultPort;
        bool     m_listening = false;
        bool     m_handshook = false;
        uint32_t m_subscribe = DebugChannelAll;
        LogLevel m_minLevel  = LogLevel::Trace;
        uint32_t m_categoryMask = 0xFFFFFFFFu;
        float    m_memoryAcc = 0.0f;
        float    m_perfAcc   = 0.0f;
        float    m_hbAcc     = 0.0f;
        float    m_recvAge   = 0.0f;
        uint32_t m_hbCounter = 0;

        std::vector<DebugLogEntry> m_logRing;
        uint32_t                   m_logHead  = 0;
        uint32_t                   m_logTail  = 0;
        uint32_t                   m_logCount = 0;
        uint16_t                   m_dropped  = 0;
        std::mutex                 m_logMutex;
        bool                       m_captureInstalled = false;

        PerfCounters m_perf;
        uint8_t      m_scratch[8192]{};
    };

} // namespace Dark
