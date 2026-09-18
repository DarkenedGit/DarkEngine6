#include "Render/MaterialSurface.h"
#include "Assets/Material.h"

#include <cstring>

namespace Dark
{

    namespace
    {
        float rec709Luma(const float* rgb)
        {
            return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
        }

        float premultipliedEmissive(const Material& mat)
        {
            return mat.emissive() * rec709Luma(mat.emissiveColor());
        }

        float alphaModeMask(const Material& mat)
        {
            return (mat.alphaMode() == MaterialAlphaMode::Mask) ? 1.0f : 0.0f;
        }
    } // namespace

    void applyMaterialSurface(const Material& mat, float color[4])
    {
        std::memcpy(color, mat.baseColor(), 4 * sizeof(float));
    }

    void applyMaterialSurface(const Material& mat, MeshFrameConstants& cb)
    {
        applyMaterialSurface(mat, cb.color);
        cb.normalScale    = mat.normalScale();
        cb.ao             = mat.ao();
        cb.alphaCutoff    = mat.alphaCutoff();
        cb.alphaModeMask  = alphaModeMask(mat);
        cb.emissive       = premultipliedEmissive(mat);
    }

    void applyMaterialSurface(const Material& mat, MeshGBufferConstants& cb)
    {
        const float* c = mat.baseColor();
        cb.color[0]      = c[0];
        cb.color[1]      = c[1];
        cb.color[2]      = c[2];
        cb.color[3]      = premultipliedEmissive(mat);
        cb.roughness     = mat.roughness();
        cb.metallic      = mat.metallic();
        cb.ao            = mat.ao();
        cb.normalScale   = mat.normalScale();
        cb.alphaCutoff   = mat.alphaCutoff();
        cb.alphaModeMask = alphaModeMask(mat);
    }

} // namespace Dark
