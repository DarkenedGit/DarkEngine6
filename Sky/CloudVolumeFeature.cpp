#include "Sky/CloudVolumeFeature.h"

#include "Core/Log.h"
#include "Render/FrameView.h"
#include "Render/HdrDrawInputs.h"
#include "Render/HdrPassViews.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

namespace Dark
{

    bool CloudVolumeFeature::create(const FeatureCreateInfo& info)
    {
        if (!info.renderer)
            return false;
        m_renderer            = info.renderer;
        const char* const tag = info.logTag ? info.logTag : "Clouds";
        if (!m_pipeline.create(info.renderer->device(), info.renderer->sceneColorFormat()))
            DE_LOG_ERROR(LogCategory::Render, "{}: CloudVolumePipeline create failed — cloud volumes disabled", tag);
        else if (!m_gpuList.create(info.renderer->device()))
            DE_LOG_ERROR(LogCategory::Render, "{}: CloudVolumeGpuList create failed — cloud volumes disabled", tag);
        if (!enabled())
        {
            m_pipeline = CloudVolumePipeline{};
            m_gpuList  = CloudVolumeGpuList{};
        }
        return true;
    }

    void CloudVolumeFeature::destroy()
    {
        m_pipeline = CloudVolumePipeline{};
        m_gpuList  = CloudVolumeGpuList{};
        m_renderer = nullptr;
        m_inputs   = {};
    }

    void CloudVolumeFeature::addPasses(FeatureBuilder& b, const FrameView& view)
    {
        if (!enabled() || view.setup.disabled.has(EnginePass::Clouds))
            return;
        Renderer* const rr = m_renderer;
        if (!addHdrDrawPass(b, "Clouds", false,
                            [this, rr](Rg::PassContext& c, const HdrDrawTargets& d)
                            {
                                bindHdr(c, *rr, d.hdr);
                                draw(c, rgSrvTable(c, { d.depth }));
                            }))
            b.opaque("Clouds", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Clouds"); });
    }

    void CloudVolumeFeature::draw(Rg::PassContext& c, D3D12_GPU_DESCRIPTOR_HANDLE depthTable)
    {
        if (!m_inputs.world || !m_inputs.camera || !m_inputs.viewProj)
            return;
        m_pipeline.draw(c.cmd(), depthTable, *m_renderer, *m_inputs.world, m_gpuList, *m_inputs.camera, *m_inputs.viewProj, m_inputs.frame);
    }

} // namespace Dark
