#pragma once

#include "Render/MeshConstants.h"

namespace Dark
{

    class Material;

    void applyMaterialSurface(const Material& mat, float color[4]);
    void applyMaterialSurface(const Material& mat, MeshFrameConstants& cb);
    // G-buffer RT0.a is emissive — tint RGB only, write 0 into a.
    void applyMaterialSurface(const Material& mat, MeshGBufferConstants& cb);

} // namespace Dark
