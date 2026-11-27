#pragma once

#include "Render/FrameFeatures.h"
#include "Render/FrameSetup.h"
#include "Render/HdrDrawInputs.h"
#include "Render/HdrPassViews.h"
#include "Water/UnderwaterPipeline.h"
#include "Water/WaterPipeline.h"

#include <utility>

namespace Dark
{

    class Renderer;

    // The water surface and underwater view passes (PassId::Water, PassId::Underwater) and their
    // pipelines. The host owns one, creates it after SceneRenderer::init and draws its water bodies
    // through the callables it passes to addWaterPasses / addUnderwaterPasses.
    class WaterRendering
    {
    public:
        WaterRendering()                                 = default;
        WaterRendering(const WaterRendering&)            = delete;
        WaterRendering& operator=(const WaterRendering&) = delete;

        // The underwater view is optional (a failure is logged and the view is off). The water
        // pipeline is required when `waterRequired` (a failure is fatal and create returns false);
        // otherwise a failure is logged and the host has no water.
        bool create(Renderer& renderer, const char* logTag, bool waterRequired);
        void release();

        WaterPipeline& pipeline()
        {
            return m_water;
        }
        const WaterPipeline& pipeline() const
        {
            return m_water;
        }
        UnderwaterPipeline& underwater()
        {
            return m_underwater;
        }
        const UnderwaterPipeline& underwater() const
        {
            return m_underwater;
        }

        // Water and Underwater draw into HDR with read-only depth (Render/HdrDrawInputs.h) and sample
        // the shadow map; without scene targets the entry is an opaque pass that logs once.
        // Water: `draw(Rg::PassContext&, const WaterTables&)`, HDR + read-only depth bound. The tables
        // carry this frame's SSR capture when SsrCapture published one.
        template <class Draw> void addWaterPasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
        {
            WaterRendering* const self = this;
            Renderer* const       rr   = &renderer;
            if (!addHdrDrawPass(b, "Water", waterUses(),
                                [self, rr, draw = std::forward<Draw>(draw)](Rg::PassContext& c, const HdrDrawTargets& d)
                                {
                                    bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                                    const WaterTables tables = self->waterTables(c, *rr, d.depth, d.sceneColor, d.shadow);
                                    draw(c, tables);
                                }))
                b.opaque("Water", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Water"); });
        }
        // Underwater: copies HDR, draws the fog from the copy with `frame(Rg::PassContext&)` (an
        // UnderwaterFrame, read at execute), then `ceiling(Rg::PassContext&, const WaterTables&)` with HDR
        // + read-only depth bound. Without the copy (or its pipeline) only the ceiling draws.
        template <class Frame, class Ceiling> void addUnderwaterPasses(FeatureBuilder& b, Renderer& renderer, Frame&& frame, Ceiling&& ceiling, const FrameSetup& setup = {})
        {
            if (setup.disabled.has(EnginePass::Underwater))
                return;
            WaterRendering* const   self = this;
            Renderer* const         rr   = &renderer;
            const Rg::TextureHandle copy = importUnderwaterCopy(b, renderer);
            if (copy.valid() && b.handle(RenderResource::Hdr).valid() && b.handle(RenderResource::Depth).valid())
            {
                Dark::addUnderwaterPasses(
                    b, copy, [](Rg::PassContext& c, const UnderwaterTargets& d) { copyHdr(c, d); },
                    [self, rr, frame = std::forward<Frame>(frame), ceiling = std::forward<Ceiling>(ceiling)](Rg::PassContext& c, const UnderwaterTargets& d)
                    {
                        self->drawUnderwater(c, *rr, d, frame(c));
                        bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                        const WaterTables tables = self->waterTables(c, *rr, d.depth, d.sceneColor, d.shadow);
                        ceiling(c, tables);
                    });
                return;
            }
            if (!addHdrDrawPass(b, "Underwater", waterUses(),
                                [self, rr, ceiling = std::forward<Ceiling>(ceiling)](Rg::PassContext& c, const HdrDrawTargets& d)
                                {
                                    bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                                    const WaterTables tables = self->waterTables(c, *rr, d.depth, d.sceneColor, d.shadow);
                                    ceiling(c, tables);
                                }))
                b.opaque("Underwater", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Underwater"); });
        }

    private:
        WaterTables       waterTables(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle depth, Rg::TextureHandle sceneColor, Rg::TextureHandle shadow) const;
        Rg::TextureHandle importUnderwaterCopy(FeatureBuilder& b, Renderer& renderer);
        void              drawUnderwater(Rg::PassContext& c, const Renderer& renderer, const UnderwaterTargets& d, const UnderwaterFrame& frame) const;

        WaterPipeline      m_water;
        UnderwaterPipeline m_underwater;
    };

} // namespace Dark
