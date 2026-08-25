#pragma once

#include "Debug/DebugProtocol.h"
#include "Network/TcpSocket.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Dark
{

    class DebugClient
    {
    public:
        DebugClient();
        ~DebugClient();

        DebugClient(const DebugClient&)            = delete;
        DebugClient& operator=(const DebugClient&) = delete;

        void setStream(IByteStream* stream); // non-owning; tests inject FakeTcpStream
        bool connect(const Address& dest);
        void disconnect();

        bool    isConnected() const;
        bool    isConnecting() const;
        Address peerAddress() const;

        void poll(float dt);

        uint32_t subscribeMask() const { return m_subscribe; }
        void     setSubscribeMask(uint32_t mask);
        void     setLogFilter(LogLevel minLevel, uint32_t categoryMask);

        uint32_t logCount() const { return m_logCount; }
        bool     logAt(uint32_t i, DebugLogEntry& out) const;
        void     clearLogs();

        uint32_t poolCount() const { return m_poolCount; }
        bool     poolAt(uint32_t i, DebugMemoryPool& out) const;

        const DebugPerfSnapshot& lastPerf() const { return m_perf; }
        uint32_t                 frameHistoryCount() const { return m_histCount; }
        bool                     frameHistoryAt(uint32_t i, float& frameMs) const;

        const char* hostTitle() const { return m_hostTitle; }
        bool        handshakeOk() const { return m_handshook; }

    private:
        bool queueOpcode(DebugOpcode op, const uint8_t* payload, uint32_t n);
        void pumpSend();
        void pumpRecv();
        void handleMessage(const DebugHeader& hdr, const uint8_t* payload, uint32_t size);
        void closeStream();
        bool sendHello();
        void sendSubscribeAndFilter();

        IByteStream*               m_injected = nullptr;
        std::unique_ptr<TcpSocket> m_owned;
        IByteStream*               m_stream = nullptr;
        DebugFramer                m_framer;
        std::vector<uint8_t>       m_sendBuf;
        uint32_t                   m_sendPos = 0;

        bool     m_handshook = false;
        bool     m_helloSent = false;
        uint32_t m_subscribe = DebugChannelAll;
        LogLevel m_minLevel  = LogLevel::Trace;
        uint32_t m_categoryMask = 0xFFFFFFFFu;
        float    m_recvAge   = 0.0f;
        float    m_hbAcc     = 0.0f;
        uint32_t m_hbCounter = 0;
        char     m_hostTitle[kDebugNameBytes]{};

        std::vector<DebugLogEntry> m_logs;
        uint32_t                   m_logHead  = 0;
        uint32_t                   m_logCount = 0;
        DebugMemoryPool  m_pools[kDebugMaxPools]{};
        uint32_t         m_poolCount = 0;
        DebugPerfSnapshot m_perf{};
        float            m_history[240]{};
        uint32_t         m_histWrite = 0;
        uint32_t         m_histCount = 0;
        uint8_t          m_scratch[8192]{};
    };

} // namespace Dark
