#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Frame setup (render-graph plan §3.4.2): before the features declare their passes, the host's frame
// setup hooks may turn engine passes off for one frame. A pass that is off adds no pass, requests no
// history (so its history reads as invalid once when it returns) and publishes disabled outputs
// (Rg::RenderGraph::disabledOutput); its readers bind their fallback through PassBuilder::tryRead.

namespace Dark
{

    struct FrameView;

    enum class EnginePass : uint8_t
    {
        DirectionalShadows,
        LocalShadows,
        Decals,
        Gtao,
        Ssr,
        DeferredLighting,
        LocalLights,
        Sky,
        Clouds,
        SsrCapture,
        Underwater,
        Bloom,
        Taa,
        MotionBlur,
        AutoExposure,
        Count,
    };
    inline constexpr uint32_t kEnginePassCount = static_cast<uint32_t>(EnginePass::Count);

    const char* enginePassName(EnginePass pass);

    struct EnginePassMask
    {
        uint32_t bits = 0;

        bool has(EnginePass pass) const
        {
            return (bits & bit(pass)) != 0;
        }
        void set(EnginePass pass)
        {
            bits |= bit(pass);
        }
        bool any() const
        {
            return bits != 0;
        }
        static constexpr uint32_t bit(EnginePass pass)
        {
            return 1u << static_cast<uint32_t>(pass);
        }
        bool operator==(const EnginePassMask&) const = default;
    };

    // Every EnginePass bit a hook may turn off.
    inline constexpr uint32_t kEnginePassesHonoured = (1u << kEnginePassCount) - 1u;

    // out = lerp(c, M * float4(c, aux), strength). Identity / strength 0 means unchanged
    // (render-graph plan §3.4.2). Shader ApplyColorRemap is a later slice; these fields travel with
    // the resolved setup so a hook can record them now.
    struct ColorRemap
    {
        float m[3][4]  = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
        float aux      = 0.0f;
        float strength = 0.0f;

        bool operator==(const ColorRemap&) const = default;
    };

    // One frame's setup. The default (scalars 1, remaps identity) leaves the frame as the host's
    // settings and debug state draw it. Hooks may only turn passes off and only lower a scalar.
    struct FrameSetup
    {
        EnginePassMask disabled;
        float          shadowStrength = 1.0f; // 0..1, scales the packed shadow strength (lighting, water, translucent)
        float          aoStrength     = 1.0f; // 0..1, scales GTAO intensity
        float          ssrStrength    = 1.0f; // 0..1, scales the lighting SSR blend (ssrEnabled)
        float          emissiveGain   = 1.0f; // 0..1, multiplies the host's LightingConstants::emissiveGain
        ColorRemap     albedoRemap, fogRemap, waterRemap, skyRemap;

        bool operator==(const FrameSetup&) const = default;
    };

    struct DebugRenderState;

    // OR the DebugRenderState toggles that match an EnginePass into disabled (plan D8). A toggle
    // that's off is the same bit a hook would set. lighting / localLights / showAlbedoLinear are not
    // folded: they still run their passes and only change draw constants, as they do today.
    void applyDebugToggles(FrameSetup& setup, const DebugRenderState& debug);

    // A game's per-frame setup. setupFrame runs on the main thread before any pass is declared; it
    // records no GPU work and allocates nothing. It can only turn passes off: bits it clears are
    // set again. A hook whose name() is null or empty is skipped (logged once).
    class IFrameSetupHook
    {
    public:
        virtual ~IFrameSetupHook()                                               = default;
        virtual const char* name() const                                         = 0;
        virtual void        setupFrame(const FrameView& view, FrameSetup& setup) = 0;
    };

    // The hooks a host runs each frame, in the order they were added. Non-owning: the host keeps a
    // hook alive until remove() or clear().
    class FrameSetupHooks
    {
    public:
        // More changes of the resolved mask than this within one second log one warning.
        static constexpr uint32_t kMaxMaskChangesPerSecond = 10;

        // False (logged) for null or a hook already added.
        bool     add(IFrameSetupHook* hook);
        void     remove(IFrameSetupHook* hook);
        void     clear();
        uint32_t count() const
        {
            return static_cast<uint32_t>(m_hooks.size());
        }

        // The frame's setup: start from `view.setup`, fold `view.debug` toggles (when non-null), then
        // OR each named hook's disabled bits and take the min of any scalar it lowers (clamped to
        // 0..1). Two hooks writing the same scalar or remap log once. `seconds` is a monotonic clock
        // for the mask-change warning.
        FrameSetup resolve(const FrameView& view, double seconds);
        bool       maskChangeWarned() const
        {
            return m_maskChangeWarned;
        }

    private:
        struct Entry
        {
            IFrameSetupHook* hook          = nullptr;
            bool             loggedUnnamed = false;
        };
        void noteMask(const EnginePassMask& mask, double seconds);

        std::vector<Entry>                               m_hooks;
        uint32_t                                         m_loggedDropped = 0;
        EnginePassMask                                   m_lastMask;
        bool                                             m_haveLastMask     = false;
        bool                                             m_maskChangeWarned = false;
        std::array<double, kMaxMaskChangesPerSecond + 1> m_changeTimes{};
        uint32_t                                         m_changeCount = 0; // total, the ring index
        uint32_t                                         m_loggedTwoWriters = 0;
        uint32_t                                         m_loggedTwoRemaps  = 0;
    };

} // namespace Dark
