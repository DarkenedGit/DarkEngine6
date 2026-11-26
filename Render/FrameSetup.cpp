#include "Render/FrameSetup.h"

#include "Core/Log.h"
#include "Render/FrameView.h"

#include <algorithm>

namespace Dark
{

    const char* enginePassName(EnginePass pass)
    {
        switch (pass)
        {
        case EnginePass::DirectionalShadows:
            return "DirectionalShadows";
        case EnginePass::LocalShadows:
            return "LocalShadows";
        case EnginePass::Decals:
            return "Decals";
        case EnginePass::Gtao:
            return "Gtao";
        case EnginePass::Ssr:
            return "Ssr";
        case EnginePass::DeferredLighting:
            return "DeferredLighting";
        case EnginePass::LocalLights:
            return "LocalLights";
        case EnginePass::Sky:
            return "Sky";
        case EnginePass::Clouds:
            return "Clouds";
        case EnginePass::SsrCapture:
            return "SsrCapture";
        case EnginePass::Underwater:
            return "Underwater";
        case EnginePass::Bloom:
            return "Bloom";
        case EnginePass::Taa:
            return "Taa";
        case EnginePass::MotionBlur:
            return "MotionBlur";
        case EnginePass::AutoExposure:
            return "AutoExposure";
        case EnginePass::Count:
            break;
        }
        return "?";
    }

    bool FrameSetupHooks::add(IFrameSetupHook* hook)
    {
        if (!hook || std::any_of(m_hooks.begin(), m_hooks.end(), [hook](const Entry& e) { return e.hook == hook; }))
        {
            DE_LOG_ERROR(LogCategory::Render, "FrameSetupHooks: frame setup hook is null or already added");
            return false;
        }
        m_hooks.push_back({ hook, false });
        return true;
    }

    void FrameSetupHooks::remove(IFrameSetupHook* hook)
    {
        std::erase_if(m_hooks, [hook](const Entry& e) { return e.hook == hook; });
    }

    void FrameSetupHooks::clear()
    {
        m_hooks.clear();
    }

    FrameSetup FrameSetupHooks::resolve(const FrameView& view, double seconds)
    {
        FrameSetup setup = view.setup;
        for (Entry& e : m_hooks)
        {
            const char* name = e.hook->name();
            if (!name || !*name)
            {
                if (!e.loggedUnnamed)
                {
                    e.loggedUnnamed = true;
                    DE_LOG_ERROR(LogCategory::Render, "FrameSetupHooks: a frame setup hook has no name; it is skipped (logs once)");
                }
                continue;
            }
            FrameSetup mine = setup;
            e.hook->setupFrame(view, mine);
            setup.disabled.bits |= mine.disabled.bits;
        }
        const uint32_t dropped = setup.disabled.bits & ~kEnginePassesHonoured;
        for (uint32_t i = 0; i < kEnginePassCount; ++i)
        {
            const uint32_t b = 1u << i;
            if ((dropped & b) && !(m_loggedDropped & b))
            {
                m_loggedDropped |= b;
                DE_LOG_WARN(LogCategory::Render, "FrameSetupHooks: turning {} off is not supported yet; it stays on (logs once)", enginePassName(static_cast<EnginePass>(i)));
            }
        }
        setup.disabled.bits &= kEnginePassesHonoured;
        noteMask(setup.disabled, seconds);
        return setup;
    }

    void FrameSetupHooks::noteMask(const EnginePassMask& mask, double seconds)
    {
        const bool changed = m_haveLastMask && !(mask == m_lastMask);
        m_lastMask         = mask;
        m_haveLastMask     = true;
        if (!changed)
            return;
        constexpr uint32_t kRing             = kMaxMaskChangesPerSecond + 1;
        m_changeTimes[m_changeCount % kRing] = seconds;
        ++m_changeCount;
        // The ring holds the last kRing changes; the oldest sits at the next write slot.
        if (m_changeCount < kRing || m_maskChangeWarned)
            return;
        if (seconds - m_changeTimes[m_changeCount % kRing] < 1.0)
        {
            m_maskChangeWarned = true;
            DE_LOG_WARN(LogCategory::Render, "FrameSetupHooks: the disabled passes changed more than {} times in a second; each change rebuilds the frame's plan (logs once)",
                        kMaxMaskChangesPerSecond);
        }
    }

} // namespace Dark
