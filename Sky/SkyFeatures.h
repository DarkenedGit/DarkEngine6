#pragma once

#include "Sky/CloudVolumeFeature.h"
#include "Sky/SkyFeature.h"

namespace Dark
{

    // The Sky module's render features. A host owns one and registers it before SceneRenderer::init.
    struct SkyFeatures
    {
        SkyFeature         sky;
        CloudVolumeFeature clouds;
    };

    void registerSkyFeatures(RenderFeatureRegistry& registry, SkyFeatures& features);

} // namespace Dark
