#include "Render/TemporalHistory.h"

#include "Core/Log.h"

#include <algorithm>
#include <cstdio>

namespace Dark::Rg
{

    HistoryTargets HistoryTargetSet::request(RenderGraph& g, RenderTargetPool& pool, const char* name, const TargetDesc& desc)
    {
        HistoryTargets out;
        TargetExtent   extent;
        if (!name || !resolveTargetExtent(desc, pool.outputWidth(), pool.outputHeight(), extent))
            return out;

        TextureDesc td;
        td.sizeMode   = SizeMode::Absolute;
        td.width      = extent.width;
        td.height     = extent.height;
        td.mips       = desc.mips;
        td.arraySize  = desc.arraySize;
        td.format     = desc.format;
        td.viewFormat = desc.srvFormat;
        out.pair      = g.history(name, td);
        if (!out.pair.curr.valid())
            return out;

        const HistorySlots slots = g.historySlots(name);
        if (slots.curr == kNoPass || slots.prev == kNoPass)
            return out;

        TargetDesc absolute = desc;
        absolute.size       = TargetSize::Absolute;
        absolute.width      = extent.width;
        absolute.height     = extent.height;
        absolute.divisor    = 1;
        out.curr            = slotTarget(pool, name, slots.curr, absolute);
        out.prev            = slotTarget(pool, name, slots.prev, absolute);
        out.prevValid       = slots.prevValid && out.valid();
        return out;
    }

    TargetId HistoryTargetSet::slotTarget(RenderTargetPool& pool, const char* name, uint32_t id, const TargetDesc& desc)
    {
        for (const Slot& s : m_slots)
            if (s.id == id)
                return s.target;
        char label[160];
        std::snprintf(label, sizeof(label), "DE.History.%s#%u", name, id);
        const TargetId target = pool.registerTexture(label, desc);
        if (!target.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "HistoryTargetSet: could not create '{}'", label);
            return target;
        }
        m_slots.push_back({ id, target });
        return target;
    }

    void HistoryTargetSet::collect(const RenderGraph& g, RenderTargetPool& pool)
    {
        g.liveHistorySlots(m_live);
        for (size_t i = 0; i < m_slots.size();)
        {
            if (std::find(m_live.begin(), m_live.end(), m_slots[i].id) != m_live.end())
            {
                ++i;
                continue;
            }
            pool.unregisterTexture(m_slots[i].target);
            m_slots[i] = m_slots.back();
            m_slots.pop_back();
        }
    }

    void HistoryTargetSet::clear(RenderTargetPool& pool)
    {
        for (const Slot& s : m_slots)
            pool.unregisterTexture(s.target);
        m_slots.clear();
    }

} // namespace Dark::Rg
