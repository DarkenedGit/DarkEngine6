#pragma once

#include "Core/Log.h"

#include <cstdint>

namespace Dark
{

    constexpr uint32_t kDebugMagic           = 0x47424444u; // "DDBG"
    constexpr uint8_t  kDebugProtocolVersion = 1;
    constexpr uint8_t  kDebugHeaderBytes     = 12;
    constexpr uint32_t kDebugMaxPayload      = 65536;
    constexpr uint16_t kDebugDefaultPort     = 26162;
    constexpr uint32_t kDebugMaxPools        = 64;
    constexpr uint32_t kDebugMaxLogText      = 512;
    constexpr uint32_t kDebugNameBytes       = 32;
    constexpr uint32_t kDebugPerfSlotCount   = 8;
    constexpr uint32_t kDebugLogRing         = 4096;
    constexpr uint32_t kDebugRecvBudget      = 32;
    constexpr uint32_t kDebugLogFlushBudget  = 64;
    constexpr uint32_t kDebugSendCapBytes    = 65536;
    constexpr float    kDebugMemoryHz        = 2.0f;
    constexpr float    kDebugPerfHz          = 10.0f;
    constexpr float    kDebugHeartbeatSec    = 1.0f;
    constexpr float    kDebugTimeoutSec      = 2.0f;

    enum class DebugOpcode : uint8_t
    {
        Invalid       = 0,
        Hello         = 1,
        HelloAck      = 2,
        Subscribe     = 3,
        SetLogFilter  = 4,
        LogLine       = 5,
        MemorySnapshot = 6,
        PerfSnapshot  = 7,
        Heartbeat     = 8
    };

    enum DebugChannel : uint32_t
    {
        DebugChannelLog    = 1u << 0,
        DebugChannelMemory = 1u << 1,
        DebugChannelPerf   = 1u << 2,
        DebugChannelAll    = (1u << 0) | (1u << 1) | (1u << 2)
    };

    enum class PerfSlot : uint8_t
    {
        Frame      = 0,
        NetPoll    = 1,
        Update     = 2,
        Audio      = 3,
        NetFlush   = 4,
        DebugFlush = 5,
        Render     = 6,
        Present    = 7,
        Count      = 8
    };

    static_assert(static_cast<uint32_t>(PerfSlot::Count) == kDebugPerfSlotCount, "PerfSlot count matches snapshot array");

    struct DebugFrameStats
    {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
    };

    struct DebugHello
    {
        char     name[kDebugNameBytes]{};
        uint32_t pid      = 0;
        uint32_t channels = DebugChannelAll;
    };

    struct DebugHelloAck
    {
        uint8_t  protocolVersion = kDebugProtocolVersion;
        uint16_t listenPort      = 0;
        char     title[kDebugNameBytes]{};
    };

    struct DebugLogEntry
    {
        uint64_t    timestampMs = 0;
        LogLevel    level       = LogLevel::Info;
        LogCategory category    = LogCategory::Core;
        uint16_t    dropped     = 0;
        char        text[kDebugMaxLogText + 1]{};
    };

    struct DebugMemoryPool
    {
        char     name[kDebugNameBytes]{};
        uint64_t used     = 0;
        uint64_t capacity = 0;
        uint32_t count    = 0;
    };

    struct DebugPerfSnapshot
    {
        float    dtMs      = 0.0f;
        float    fps       = 0.0f;
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
        float    phaseMs[kDebugPerfSlotCount]{};
        uint64_t packetsIn  = 0;
        uint64_t packetsOut = 0;
        float    rttMs      = 0.0f;
    };

} // namespace Dark
