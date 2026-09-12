#include "Render/MaterialSurface.h"
#include "Render/Material.h"

#include <cstring>

namespace Dark
{

    void applyMaterialSurface(const Material& mat, float color[4])
    {
        std::memcpy(color, mat.baseColor(), 4 * sizeof(float));
    }

    void applyMaterialSurface(const Material& mat, MeshFrameConstants& cb)
    {
        applyMaterialSurface(mat, cb.color);
    }

    void applyMaterialSurface(const Material& mat, MeshGBufferConstants& cb)
    {
        const float* c = mat.baseColor();
        cb.color[0]    = c[0];
        cb.color[1]    = c[1];
        cb.color[2]    = c[2];
        cb.color[3]    = 0.0f;
        cb.roughness   = mat.roughness();
        cb.metallic    = mat.metallic();
    }

} // namespace Dark
