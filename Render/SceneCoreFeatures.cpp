#include "Render/SceneRenderer.h"

#include "Core/Log.h"
#include "Render/Renderer.h"
#include "Render/ShadowFeature.h"

#include <algorithm>

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
        case CoreEntryKind::Game:
            return FeatureStage::Transparent;
        case CoreEntryKind::DebugTiles:
            return FeatureStage::Overlay;
        case CoreEntryKind::Post:
            return FeatureStage::Post;
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
        case CoreEntryKind::Post:
            return PassId::Post;
        case CoreEntryKind::Game:
            return PassId::Game;
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
        case CoreEntryKind::DebugTiles:
        case CoreEntryKind::Game:
            return true;
        case CoreEntryKind::Post:
            return in.post && in.viewProj;
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
            s.addGBufferPasses(b, *m_renderer, [hook = in.gbuffer, &s](Rg::PassContext& c) { hook(c, s.worldDraws()); });
            break;
        case CoreEntryKind::SsrCapture:
        {
            // The hook's pass needs a name; without one only the hook is skipped, never the capture.
            if (const CorePassHook before = in.beforeSsrCapture; before.fn && in.beforeSsrCaptureName)
                b.opaque(in.beforeSsrCaptureName, {}, {}, [before](Rg::PassContext& c) { before(c); });
            else if (before.fn && !m_loggedMissing)
            {
                m_loggedMissing = true;
                DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: SsrCapture's beforeSsrCapture hook has no name; the hook is skipped (logs once)");
            }
            s.addSsrCapturePasses(b, *m_renderer);
            break;
        }
        case CoreEntryKind::Translucent:
            s.addTranslucentPasses(b, *m_renderer, [hook = in.translucent, &s](Rg::PassContext& c) { hook(c, s.worldDraws()); });
            break;
        case CoreEntryKind::DebugTiles:
            // Off this frame (the host shows no tiles while a movie covers the world): no pass.
            if (in.debugTiles)
                s.addDebugTilePasses(b, *m_renderer, *in.debugTiles, in.debugTileSize);
            break;
        case CoreEntryKind::Post:
            addPostPasses(b);
            break;
        case CoreEntryKind::Game:
            // An empty slot adds no pass.
            for (IGameFeature* const feature : s.m_gameFeatures)
                feature->addPasses(b, *m_renderer, s.worldDraws());
            break;
        }
    }

    void SceneRenderer::CoreEntry::addPostPasses(FeatureBuilder& b)
    {
        SceneRenderer&         s       = m_owner;
        const CoreFrameInputs& in      = s.m_coreFrame;
        TonemapSettings        tonemap = *in.post;
        bool                   meter   = false;
        if (const CoreAutoExposure* ae = in.autoExposure)
        {
            float exposure            = ae->envExposure;
            *ae->result               = {};
            ae->result->finalExposure = exposure;
            const bool deferredMeter  = s.m_autoExposure.isValid();
            if (deferredMeter)
            {
                // Resize drops the 1x1. Zero the correction before this frame can apply a stale sample.
                if (s.syncAutoExposureSize(*m_renderer))
                    resetAutoExposure(*ae->state);
                const float measured = (ae->settings->mode == ExposureMode::Auto && !ae->hold) ? s.m_autoExposure.readMeasuredLuma(m_renderer->frameIndex()) : 0.0f;
                *ae->result          = adaptExposure(*ae->state, measured, exposure, ae->dt, *ae->settings);
                exposure             = ae->result->finalExposure;
            }
            tonemap.exposure = exposure;
            meter            = deferredMeter && ae->settings->mode == ExposureMode::Auto && !ae->hold;
        }
        // The hook's pass needs a name; without one only the hook is skipped, never Post.
        if (const CorePassHook before = in.beforePost; before.fn && in.beforePostName)
            b.opaque(in.beforePostName, { RenderResource::Hdr, RenderResource::Depth }, { RenderResource::Hdr }, [before](Rg::PassContext& c) { before(c); });
        else if (before.fn && !m_loggedMissing)
        {
            m_loggedMissing = true;
            DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: Post's beforePost hook has no name; the hook is skipped (logs once)");
        }
        s.addPostPasses(b, *m_renderer, *in.viewProj, tonemap, meter);
    }

    bool SceneRenderer::addGameFeature(IGameFeature* feature)
    {
        if (!feature || std::find(m_gameFeatures.begin(), m_gameFeatures.end(), feature) != m_gameFeatures.end())
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: game feature is null or already added");
            return false;
        }
        m_gameFeatures.push_back(feature);
        return true;
    }

    void SceneRenderer::removeGameFeature(IGameFeature* feature)
    {
        m_gameFeatures.erase(std::remove(m_gameFeatures.begin(), m_gameFeatures.end(), feature), m_gameFeatures.end());
    }

    void registerCoreFeatures(RenderFeatureRegistry& registry, SceneRenderer& scene)
    {
        for (SceneRenderer::CoreEntry& e : scene.m_coreEntries)
            registry.add(&e);
    }

    void registerShadowFeature(SceneRenderer& scene, ShadowFeature& feature)
    {
        feature.bindShadowMaps(&scene.m_shadows, &scene.m_localShadows, &scene.m_skinnedShadowPipeline, &scene.m_skinRing);
        registerShadowFeature(scene.m_features, feature);
    }

} // namespace Dark
