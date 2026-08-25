#include "Debug/PerfCounters.h"

namespace Dark
{

    void PerfCounters::beginFrame()
    {
        m_current  = {};
        m_inFrame  = true;
    }

    void PerfCounters::add(PerfSlot slot, float seconds)
    {
        if (!m_inFrame)
            return;
        const uint32_t i = static_cast<uint32_t>(slot);
        if (i >= kDebugPerfSlotCount)
            return;
        if (seconds < 0.0f)
            seconds = 0.0f;
        m_current.phaseMs[i] += seconds * 1000.0f;
    }

    void PerfCounters::endFrame(float dt, const DebugFrameStats& stats, uint64_t packetsIn, uint64_t packetsOut, float rttMs)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        m_current.dtMs       = dt * 1000.0f;
        m_current.fps        = (dt > 1.0e-6f) ? (1.0f / dt) : 0.0f;
        m_current.drawCalls  = stats.drawCalls;
        m_current.triangles  = stats.triangles;
        m_current.packetsIn  = packetsIn;
        m_current.packetsOut = packetsOut;
        m_current.rttMs      = rttMs;
        m_current.phaseMs[static_cast<uint32_t>(PerfSlot::Frame)] = m_current.dtMs;
        m_last     = m_current;
        m_inFrame  = false;

        m_history[m_histWrite] = m_last.dtMs;
        m_histWrite            = (m_histWrite + 1) % kPerfHistory;
        if (m_histCount < kPerfHistory)
            ++m_histCount;
    }

    bool PerfCounters::historyAt(uint32_t i, float& frameMs) const
    {
        if (i >= m_histCount)
            return false;
        const uint32_t oldest = (m_histCount == kPerfHistory) ? m_histWrite : 0;
        const uint32_t idx    = (oldest + i) % kPerfHistory;
        frameMs               = m_history[idx];
        return true;
    }

} // namespace Dark
