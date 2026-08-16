#pragma once

#include "Particles/ParticleTypes.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    uint32_t clampRibbonCount(uint32_t count);

    // Alive particles of one ribbon, oldest (lowest seq) first. Size/color are age-lerped.
    void collectRibbonNodes(const std::vector<Particle>& particles, uint32_t ribbonId, std::vector<RibbonNode>& out);

    // Camera-facing strip. Each consecutive pair becomes one quad (6 verts).
    // U runs 0..uvScale along the path; V is 0 on the left edge and 1 on the right.
    uint32_t appendCameraFacingRibbon(
        const RibbonNode* nodes,
        uint32_t count,
        const Math::Vector3f& cameraPos,
        float uvScale,
        std::vector<ParticleVertex>& out);

} // namespace Dark
