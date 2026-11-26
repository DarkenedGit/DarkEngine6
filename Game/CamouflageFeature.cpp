#include "Game/CamouflageFeature.h"

#include "Core/Log.h"
#include "Render/FrameFeatures.h"
#include "Render/HdrDrawInputs.h"
#include "Render/HdrPassViews.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

#include <utility>

namespace Dark
{

    bool CamouflageFeature::create(Renderer& renderer, const char* tag)
    {
        if (m_pipeline.create(renderer.device(), renderer.sceneColorFormat()))
            return true;
        DE_LOG_ERROR(LogCategory::Render, "{}: CamouflagePipeline create failed — camouflage disabled", tag);
        return false;
    }

    void CamouflageFeature::release()
    {
        m_pipeline.release();
    }

    void CamouflageFeature::addPasses(FeatureBuilder& b, Renderer& renderer, const WorldDrawContext& draws)
    {
        const Draw draw = std::exchange(m_draw, Draw{});
        if (!draw.fn || !m_pipeline.isValid())
            return;
        Renderer* const         rr   = &renderer;
        const Rg::TextureHandle copy = m_pipeline.importCopy(b.graph(), renderer);
        if (!copy.valid() || !addHdrCopyDepthWritePasses(
                                 b, "Camouflage.Copy", "Camouflage", copy, [](Rg::PassContext& c, const HdrCopyTargets& d) { copyHdr(c, d); },
                                 [rr, draw, draws](Rg::PassContext& c, const HdrCopyTargets& d)
                                 {
                                     bindHdrDepthWrite(c, *rr, d.hdr, d.depth);
                                     draw(c, rgSrvTable(c, { d.copy }), draws);
                                 }))
            b.opaque("Camouflage", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Camouflage"); });
    }

} // namespace Dark
