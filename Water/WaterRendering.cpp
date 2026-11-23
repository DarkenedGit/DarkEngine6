#include "Water/WaterRendering.h"

#include "Core/Log.h"
#include "Render/LightingViews.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

namespace Dark
{

    bool WaterRendering::create(Renderer& renderer, const char* logTag, bool waterRequired)
    {
        const char* const tag = logTag ? logTag : "WaterRendering";
        if (!m_underwater.create(renderer.device()))
            DE_LOG_WARN(LogCategory::Render, "{}: UnderwaterPipeline create failed — underwater view disabled", tag);
        if (!m_water.create(renderer.device(), renderer.sceneColorFormat()))
        {
            if (waterRequired)
            {
                DE_LOG_FATAL("{}: WaterPipeline create failed", tag);
                return false;
            }
            DE_LOG_ERROR(LogCategory::Render, "{}: WaterPipeline create failed — no water in this host", tag);
        }
        return true;
    }

    void WaterRendering::release()
    {
        m_underwater.release();
        m_water = WaterPipeline{};
    }

    WaterTables WaterRendering::waterTables(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle depth, Rg::TextureHandle sceneColor, Rg::TextureHandle shadow) const
    {
        // No capture this frame: the SSR slots hold the pipeline's dummies and ssrReady is false.
        const LightingViews v = renderer.lightingViews();
        WaterTables         t;
        t.sceneColor = sceneColor.valid();
        t.table =
            rgTable(c, { v.height, shadowSrv(c, v, shadow), t.sceneColor ? rgSrvCpu(c, sceneColor) : m_water.ssrDummyColorCpu(), t.sceneColor ? rgSrvCpu(c, depth) : m_water.ssrDummyDepthCpu() });
        return t;
    }

    Rg::TextureHandle WaterRendering::importUnderwaterCopy(FeatureBuilder& b, Renderer& renderer)
    {
        if (!m_underwater.isValid() || !m_underwater.ensureCopy(renderer))
            return {};
        return importTracked(b.graph(), "DE.Underwater.HdrCopy", m_underwater.copyResource(), textureViews(m_underwater.copySrv()));
    }

    void WaterRendering::drawUnderwater(Rg::PassContext& c, const Renderer& renderer, const UnderwaterTargets& d, const UnderwaterFrame& frame) const
    {
        bindHdr(c, renderer, d.hdr);
        m_underwater.draw(c.cmd(), rgSrvTable(c, { d.copy, d.depth }), frame);
    }

} // namespace Dark
