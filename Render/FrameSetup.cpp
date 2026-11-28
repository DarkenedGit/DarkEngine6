#include "Render/FrameSetup.h"

#include "Core/Log.h"
#include "Render/DebugRenderState.h"
#include "Render/FrameView.h"

#include <algorithm>
#include <cstring>

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


    namespace
    {
        float clamp01(float v)
        {
            if (v < 0.0f)
                return 0.0f;
            if (v > 1.0f)
                return 1.0f;
            return v;
        }
    } // namespace

    void applyDebugToggles(FrameSetup& setup, const DebugRenderState& debug)
    {
        if (!debug.shadows)
        {
            setup.disabled.set(EnginePass::DirectionalShadows);
            setup.disabled.set(EnginePass::LocalShadows);
        }
        if (!debug.ssaoEnabled)
            setup.disabled.set(EnginePass::Gtao);
        if (!debug.ssrEnabled)
            setup.disabled.set(EnginePass::Ssr);
        if (!debug.decalsEnabled)
            setup.disabled.set(EnginePass::Decals);
        if (!debug.clouds)
            setup.disabled.set(EnginePass::Clouds);
        if (!debug.bloom)
            setup.disabled.set(EnginePass::Bloom);
        if (!debug.taa)
            setup.disabled.set(EnginePass::Taa);
        if (!debug.motionBlur)
            setup.disabled.set(EnginePass::MotionBlur);
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
        if (view.debug)
            applyDebugToggles(setup, *view.debug);

        uint32_t writtenScalars = 0;
        uint32_t writtenRemaps  = 0;
        bool     lutWritten     = false;
        auto     lowerScalar    = [&](float& dst, float proposed, uint32_t bit, const char* field)
        {
            if (proposed == dst)
                return;
            if ((writtenScalars & bit) && !(m_loggedTwoWriters & bit))
            {
                m_loggedTwoWriters |= bit;
                DE_LOG_WARN(LogCategory::Render, "FrameSetupHooks: two hooks wrote FrameSetup::{}; the lower value is kept (logs once)", field);
            }
            writtenScalars |= bit;
            const float v = clamp01(proposed);
            if (v < dst)
                dst = v;
        };
        auto takeRemap = [&](ColorRemap& dst, const ColorRemap& proposed, uint32_t bit, const char* field)
        {
            if (proposed == dst)
                return;
            if ((writtenRemaps & bit) && !(m_loggedTwoRemaps & bit))
            {
                m_loggedTwoRemaps |= bit;
                DE_LOG_WARN(LogCategory::Render, "FrameSetupHooks: two hooks wrote FrameSetup::{}; the last write is kept (logs once)", field);
            }
            writtenRemaps |= bit;
            dst          = proposed;
            dst.strength = clamp01(dst.strength);
        };

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
            lowerScalar(setup.shadowStrength, mine.shadowStrength, 1u << 0, "shadowStrength");
            lowerScalar(setup.aoStrength, mine.aoStrength, 1u << 1, "aoStrength");
            lowerScalar(setup.ssrStrength, mine.ssrStrength, 1u << 2, "ssrStrength");
            lowerScalar(setup.emissiveGain, mine.emissiveGain, 1u << 3, "emissiveGain");
            takeRemap(setup.albedoRemap, mine.albedoRemap, 1u << 0, "albedoRemap");
            takeRemap(setup.fogRemap, mine.fogRemap, 1u << 1, "fogRemap");
            takeRemap(setup.waterRemap, mine.waterRemap, 1u << 2, "waterRemap");
            takeRemap(setup.skyRemap, mine.skyRemap, 1u << 3, "skyRemap");
            if (mine.writeSurfaceTag)
                setup.writeSurfaceTag = true;
            if (mine.surfaceTagLut != setup.surfaceTagLut || mine.surfaceTagLutSize != setup.surfaceTagLutSize)
            {
                if (lutWritten && !m_loggedTwoLuts)
                {
                    m_loggedTwoLuts = true;
                    DE_LOG_WARN(LogCategory::Render, "FrameSetupHooks: two hooks wrote FrameSetup::surfaceTagLut; the last write is kept (logs once)");
                }
                lutWritten              = true;
                setup.surfaceTagLut     = mine.surfaceTagLut;
                setup.surfaceTagLutSize = mine.surfaceTagLutSize;
            }
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

    ColorRemapGpu packColorRemap(const ColorRemap& src)
    {
        ColorRemapGpu out{};
        std::memcpy(out.m0, src.m[0], sizeof(out.m0));
        std::memcpy(out.m1, src.m[1], sizeof(out.m1));
        std::memcpy(out.m2, src.m[2], sizeof(out.m2));
        out.aux      = src.aux;
        const float s = src.strength;
        out.strength  = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
        return out;
    }

    void applyColorRemap(float dst[3], const float src[3], const ColorRemap& remap)
    {
        // Mirrors ColorRemap.hlsli ApplyColorRemap, including the strength <= 0 early-out.
        if (remap.strength <= 0.0f)
        {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            return;
        }
        const ColorRemapGpu g = packColorRemap(remap);
        const float         v[4] = { src[0], src[1], src[2], g.aux };
        float mapped[3];
        for (int i = 0; i < 3; ++i)
        {
            const float* row = (i == 0) ? g.m0 : (i == 1) ? g.m1 : g.m2;
            mapped[i]        = row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3] * v[3];
        }
        const float s = g.strength; // already clamped in packColorRemap
        dst[0]        = src[0] + (mapped[0] - src[0]) * s;
        dst[1]        = src[1] + (mapped[1] - src[1]) * s;
        dst[2]        = src[2] + (mapped[2] - src[2]) * s;
    }

} // namespace Dark
