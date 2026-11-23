#pragma once

#include "Particles/ParticleInputs.h"
#include "Render/HdrPassViews.h"
#include "Render/RgViews.h"

#include <utility>

namespace Dark
{

    class Renderer;

    // The Particles pass (PassId::Particles): `draw(Rg::PassContext&, D3D12_CPU_DESCRIPTOR_HANDLE
    // depthSrv, D3D12_CPU_DESCRIPTOR_HANDLE attribSrv)` with HDR + read-only depth bound. Without the
    // scene targets the entry only logs.
    template <class Draw> void addParticlesPasses(FeatureBuilder& b, Renderer& renderer, Draw&& draw)
    {
        Renderer* const rr = &renderer;
        if (!addHdrDrawPass(b, "Particles", particlesUses(),
                            [rr, draw = std::forward<Draw>(draw)](Rg::PassContext& c, const HdrDrawTargets& d)
                            {
                                bindHdrDepthRead(c, *rr, d.hdr, d.depth);
                                draw(c, rgSrvCpu(c, d.depth), rgSrvCpu(c, d.attrib));
                            }))
            b.opaque("Particles", {}, {}, [](Rg::PassContext&) { logMissingSceneTargets("Particles"); });
    }

} // namespace Dark
