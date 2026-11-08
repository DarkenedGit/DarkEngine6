#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Material.h"

namespace Dark
{

    inline constexpr const char* kParticleBillboardMaterialKey = "m:particle_soft_circle";
    inline constexpr const char* kParticleRibbonMaterialKey    = "m:particle_soft_streak";

    // CPU quad tint and the GPU billboard CB both multiply by this. Render does not include this header.
    inline constexpr float kParticleEmissiveGain = 4.0f;

    AssetRef<Material> internParticleSpriteMaterial(class AssetManager& assets, bool ribbon);

} // namespace Dark
