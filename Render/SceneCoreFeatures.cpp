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
                DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: {} is missing a frame input; call setCoreFrame before declaring (logs once)", name());
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
        }
    }

    void registerCoreFeatures(RenderFeatureRegistry& registry, SceneRenderer& scene)
    {
        for (SceneRenderer::CoreEntry& e : scene.m_coreEntries)
            registry.add(&e);
    }

} // namespace Dark
