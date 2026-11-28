#include "Sky/SkyFeature.h"

#include "Core/Log.h"
#include "Render/FrameView.h"
#include "Render/HdrDrawInputs.h"
#include "Render/HdrPassViews.h"
#include "Render/Renderer.h"

namespace Dark
{

    bool SkyFeature::create(const FeatureCreateInfo& info)
    {
        if (!info.renderer)
            return false;
        m_renderer = info.renderer;
        if (!m_pipeline.create(m_renderer->device(), m_renderer->sceneColorFormat()))
        {
            DE_LOG_ERROR(LogCategory::Render, "{}: SkyPipeline create failed (no sky or sun disc)", info.logTag ? info.logTag : "Sky");
            m_pipeline = SkyPipeline{};
            return false;
        }
        return true;
    }

    void SkyFeature::destroy()
    {
        m_pipeline = SkyPipeline{};
        m_renderer = nullptr;
        m_inputs   = {};
    }

    void SkyFeature::addPasses(FeatureBuilder& b, const FrameView& view)
    {
        if (view.setup.disabled.has(EnginePass::Sky))
            return;
        m_skyRemap = view.setup.skyRemap;
        m_fogRemap = view.setup.fogRemap;
        Renderer* const rr = m_renderer;
        if (!rr || !addHdrDrawPass(b, "Sky", skyUses(),
                                   [this, rr](Rg::PassContext& c, const HdrDrawTargets& d)
                                   {
                                       bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                                       draw(c, shadowTable(c, *rr, d.shadow));
                                   }))
            b.opaque("Sky", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Sky"); });
    }

    void SkyFeature::draw(Rg::PassContext& c, D3D12_GPU_DESCRIPTOR_HANDLE shadowTable)
    {
        if (!m_inputs.camera || !m_inputs.env)
            return;
        const uint32_t frameIndex = m_renderer->frameIndex();
        m_pipeline.upload(frameIndex, *m_inputs.camera, *m_inputs.env, m_inputs.exposure, m_inputs.waterLevel, m_inputs.fogScale, m_skyRemap, m_fogRemap);
        m_pipeline.draw(c.cmd(), frameIndex, shadowTable, m_inputs.shadows);
    }

} // namespace Dark
