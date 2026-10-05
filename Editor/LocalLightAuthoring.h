#pragma once

#include "ECS/Components.h"
#include "Scene/SceneTypes.h"

namespace Dark
{

// Shared authored fields. Cone degrees stay with the point/spot spawn site.
// The emitter site overwrites color after this call. fillDefaultLocalLight does not call it.
inline void copyAuthoredLocalLight(LocalLightComponent& light, const SceneObjectData& authored)
{
    light.intensity    = authored.lightIntensity;
    light.range        = authored.lightRange;
    light.sourceRadius = authored.lightSourceRadius;
    light.enabled      = authored.lightEnabled;
    light.castShadow   = authored.lightCastShadow;
}

} // namespace Dark
