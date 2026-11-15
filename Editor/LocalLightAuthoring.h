#pragma once

#include "Content/SceneObjectSpawn.h"
#include "ECS/Components.h"
#include "Scene/SceneTypes.h"

namespace Dark
{

// Shared authored fields. Cone degrees stay with the point/spot spawn site.
// The emitter site overwrites color after this call.
inline void copyAuthoredLocalLight(LocalLightComponent& light, const SceneObjectData& authored)
{
    light.intensity    = sceneLightFloat(authored, "intensity", light.intensity);
    light.range        = sceneLightFloat(authored, "range", light.range);
    light.sourceRadius = sceneLightFloat(authored, "sourceRadius", light.sourceRadius);
    light.enabled      = sceneLightBool(authored, "enabled", light.enabled);
    light.castShadow   = sceneLightBool(authored, "castShadow", light.castShadow);
}

} // namespace Dark
