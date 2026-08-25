#pragma once

#include "Debug/DebugTypes.h"

#include <cstdint>

namespace Dark
{

    constexpr uint32_t kPerfHistory = 240;

    class PerfCounters
    {
    public:
        void beginFrame();
        void add(PerfSlot slot, float seconds);
        void endFrame(float dt, const DebugFrameStats& stats, uint64_t packetsIn, uint64_t packetsOut, float rttMs);

        const DebugPerfSnapshot& last() const { return m_last; }

        uint32_t historyCount() const { return m_histCount; }
        bool     historyAt(uint32_t i, float& frameMs) const;

    private:
        DebugPerfSnapshot m_current{};
        DebugPerfSnapshot m_last{};
        float             m_history[kPerfHistory]{};
        uint32_t          m_histWrite = 0;
        uint32_t          m_histCount = 0;
        bool              m_inFrame   = false;
    };

} // namespace Dark
