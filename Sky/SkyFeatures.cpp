#include "Sky/SkyFeatures.h"

namespace Dark
{

    void registerSkyFeatures(RenderFeatureRegistry& registry, SkyFeatures& features)
    {
        registry.add(&features.sky);
        registry.add(&features.clouds);
    }

} // namespace Dark
