#include "Game/VisionModesFeature.h"

#include "Core/Log.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

#include <utility>

namespace Dark
{

    VisionModesFeature::VisionModesFeature() :
        m_heatLut(makeHeatLut())
    {
    }

    bool VisionModesFeature::create(Renderer& renderer, const char* tag)
    {
        if (m_pipeline.create(renderer.device(), renderer.sceneColorFormat()))
            return true;
        DE_LOG_WARN(LogCategory::Render, "{}: VisionModesPipeline create failed — infrared disabled", tag);
        return false;
    }

    void VisionModesFeature::release()
    {
        m_pipeline.release();
        m_decision = {};
    }

    void VisionModesFeature::setupFrame(const FrameView& view, FrameSetup& setup)
    {
        (void)view;
        m_decision = decideVision(m_frame.modes, m_pipeline.isValid() && m_frame.camera != nullptr);
        applyVisionModes(m_decision, m_heatLut, setup);
    }

    void VisionModesFeature::addPasses(FeatureBuilder& b, Renderer& renderer, const WorldDrawContext& draws)
    {
        (void)draws;
        // Only a frame whose setup asked for heat draws it: that setup turned SurfaceTag on.
        const VisionDecision decision = std::exchange(m_decision, VisionDecision{});
        if (decision.picture != VisionPicture::Heat || !m_pipeline.isValid() || !m_frame.camera)
            return;
        VisionModesPipeline* const pipeline = &m_pipeline;
        Renderer* const            rr       = &renderer;
        const Camera3D* const      camera   = m_frame.camera;
        const Math::Vector3f       sun      = m_frame.sunDir;
        const float*               lut      = m_heatLut.data();
        const float                strength = decision.strength;
        const bool                 added    = declareVisionHeatPass(b,
                                                                    [pipeline, rr, camera, sun, lut, strength](Rg::PassContext& c, const VisionHeatUses& d)
                                                                    {
                                                     VisionHeatConstants k{};
                                                     camera->GetViewProj().Inverse().store(k.invViewProj);
                                                     const Math::Vector3f& pos               = camera->GetPosition();
                                                     k.cameraPos[0]                          = pos.x;
                                                     k.cameraPos[1]                          = pos.y;
                                                     k.cameraPos[2]                          = pos.z;
                                                     k.sunDir[0]                             = sun.x;
                                                     k.sunDir[1]                             = sun.y;
                                                     k.sunDir[2]                             = sun.z;
                                                     const D3D12_GPU_DESCRIPTOR_HANDLE table = rgSrvTable(c, { d.albedo, d.attrib, d.depth, d.surfaceTag });
                                                     pipeline->drawHeat(c.cmd(), *rr, table, rgRtv(c, d.hdr), k, lut, VisionModesPipeline::kLutEntries, strength);
                                                 });
        if (!added && !m_loggedMissing)
        {
            m_loggedMissing = true;
            DE_LOG_ERROR(LogCategory::Render, "VisionModesFeature: a scene target is missing; the heat pass is skipped (logs once)");
        }
    }

} // namespace Dark
