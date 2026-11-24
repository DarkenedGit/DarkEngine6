#include "Render/SceneRenderer.h"

#include "Core/Log.h"

namespace Dark
{

    const char* SceneRenderer::CoreEntry::name() const
    {
        return passName(pass());
    }

    FeatureStage SceneRenderer::CoreEntry::stage() const
    {
        switch (m_kind)
        {
        case CoreEntryKind::Lighting:
        case CoreEntryKind::LocalVolumes:
            return FeatureStage::Lighting;
        case CoreEntryKind::GBuffer:
            return FeatureStage::GBuffer;
        case CoreEntryKind::SsrCapture:
        case CoreEntryKind::Translucent:
            return FeatureStage::Transparent;
        case CoreEntryKind::DebugTiles:
            return FeatureStage::Overlay;
        default:
            return FeatureStage::PostGBuffer;
        }
    }

    PassId SceneRenderer::CoreEntry::pass() const
    {
        switch (m_kind)
        {
        case CoreEntryKind::Gtao:
            return PassId::Gtao;
        case CoreEntryKind::Ssr:
            return PassId::Ssr;
        case CoreEntryKind::Lighting:
            return PassId::Lighting;
        case CoreEntryKind::LocalVolumes:
            return PassId::LocalVolumes;
        case CoreEntryKind::GBuffer:
            return PassId::GBuffer;
        case CoreEntryKind::SsrCapture:
            return PassId::SsrCapture;
        case CoreEntryKind::Translucent:
            return PassId::Translucent;
        case CoreEntryKind::DebugTiles:
            return PassId::Overlay;
        default:
            return PassId::Decals;
        }
    }

    bool SceneRenderer::CoreEntry::create(const FeatureCreateInfo& info)
    {
        m_renderer = info.renderer;
        return m_renderer != nullptr;
    }

    void SceneRenderer::CoreEntry::destroy()
    {
        m_renderer = nullptr;
    }

    bool SceneRenderer::CoreEntry::inputsOk() const
    {
        const CoreFrameInputs& in = m_owner.m_coreFrame;
        switch (m_kind)
        {
        case CoreEntryKind::Gtao:
            return in.camera && in.gtao;
        case CoreEntryKind::Ssr:
            return in.camera && in.ssr;
        case CoreEntryKind::Lighting:
            return in.resolve != nullptr;
        case CoreEntryKind::LocalVolumes:
            return in.world && in.camera && in.resolve;
        case CoreEntryKind::GBuffer:
            return in.gbuffer.fn != nullptr;
        case CoreEntryKind::Translucent:
            return in.translucent.fn != nullptr;
        case CoreEntryKind::SsrCapture:
            return !in.beforeSsrCapture.fn || in.beforeSsrCaptureName;
        case CoreEntryKind::DebugTiles:
            return true;
        default:
            return in.camera && in.resolve;
        }
    }

    void SceneRenderer::CoreEntry::addPasses(FeatureBuilder& b, const FrameView&)
    {
        if (!inputsOk())
        {
            if (!m_loggedMissing)
            {
                m_loggedMissing = true;
                DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: {} is missing a frame input; set it with setCoreFrame before the frame executes (logs once)", name());
            }
            // The entry keeps its place in the list (the order check) with nothing to run.
            b.opaque(name(), {}, {}, [](Rg::PassContext&) {});
            return;
        }
        SceneRenderer&         s  = m_owner;
        const CoreFrameInputs& in = s.m_coreFrame;
        switch (m_kind)
        {
        case CoreEntryKind::Decals:
            s.addDecalsPasses(b, *m_renderer, *in.camera, *in.resolve);
            break;
        case CoreEntryKind::Gtao:
            s.addGtaoPasses(b, *m_renderer, *in.camera, *in.gtao);
            break;
        case CoreEntryKind::Ssr:
            s.addSsrPasses(b, *m_renderer, *in.camera, *in.ssr, in.ssrEnv);
            break;
        case CoreEntryKind::Lighting:
            s.addLightingPasses(b, *m_renderer, *in.resolve);
            break;
        case CoreEntryKind::LocalVolumes:
            s.addLocalVolumesPasses(b, *m_renderer, *in.world, *in.camera, *in.resolve);
            break;
        case CoreEntryKind::GBuffer:
            s.addGBufferPasses(b, *m_renderer, in.gbuffer);
            break;
        case CoreEntryKind::SsrCapture:
        {
            if (const CorePassHook before = in.beforeSsrCapture; before.fn)
                b.opaque(in.beforeSsrCaptureName, {}, {}, [before](Rg::PassContext& c) { before(c); });
            s.addSsrCapturePasses(b, *m_renderer);
            break;
        }
        case CoreEntryKind::Translucent:
            s.addTranslucentPasses(b, *m_renderer, in.translucent);
            break;
        case CoreEntryKind::DebugTiles:
            // Off this frame (the host shows no tiles while a movie covers the world): no pass.
            if (in.debugTiles)
                s.addDebugTilePasses(b, *m_renderer, *in.debugTiles, in.debugTileSize);
            break;
        }
    }

    void registerCoreFeatures(RenderFeatureRegistry& registry, SceneRenderer& scene)
    {
        for (SceneRenderer::CoreEntry& e : scene.m_coreEntries)
            registry.add(&e);
    }

} // namespace Dark
