#pragma once

#include "Content/ArchetypeSpawn.h"
#include "Particles/ParticleTypes.h"
#include "Scene/SceneTypes.h"

#include <string>
#include <string_view>

namespace Dark
{

    // Archetype id stored on the scene record. Empty when the record has none.
    std::string sceneArchetypeId(const SceneObjectData& object);

    // Component params for one placement. Explicit object.overridesJson is applied later and wins.
    std::string sceneComponentOverrides(const SceneObjectData& object);

    // Copies an editor particle desc onto the scene record so the override builder can see it.
    void assignSceneParticle(SceneObjectData& object, const ParticleEmitterDesc& desc);

    // True when the loaded archetype lists the component. An unknown id is false.
    bool sceneHasComponent(const SceneObjectData& object, std::string_view component);
    bool sceneHasBehavior(const SceneObjectData& object, std::string_view behavior);

    // "", "point", "spot", "ambient", or "directional". Uses lightKind, then the archetype.
    std::string sceneLightKind(const SceneObjectData& object);

    // "cube", "sphere", or "" from the archetype Mesh primitive, then the bare archetype id.
    std::string sceneMeshPrimitive(const SceneObjectData& object);

    // Cube and sphere props that block walking. Platforms are not obstacles. primitive is "cube" or "sphere".
    bool sceneWalkObstacle(const SceneObjectData& object, std::string& primitive);

    // One spawn. Builds component overrides from the scene record and calls instantiateArchetype.
    Entity instantiateSceneObject(const ArchetypeSpawnHost& host, const SceneObjectData& object, ContentError* err = nullptr);

} // namespace Dark
