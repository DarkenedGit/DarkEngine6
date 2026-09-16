#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Material.h"

namespace Dark
{

    inline constexpr const char* kParticleBillboardMaterialKey = "m:particle_soft_circle";
    inline constexpr const char* kParticleRibbonMaterialKey    = "m:particle_soft_streak";

    AssetRef<Material> internParticleSpriteMaterial(class AssetManager& assets, bool ribbon);

} // namespace Dark
