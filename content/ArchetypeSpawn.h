#pragma once

#include "Content/ContentDb.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <string_view>

namespace Dark
{

    struct ArchetypeSpawnPose
    {
        Math::Vector3f   position{};
        Math::Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vector3f   scale{ 1.0f, 1.0f, 1.0f };
        float            color[4]{ 0.75f, 0.78f, 0.82f, 1.0f };
    };

    // Creates one entity from an archetype. On failure the entity is destroyed and the result is invalid.
    // Behaviors only attach components. They do not tick.
    Entity instantiateArchetype(const ArchetypeSpawnHost& host, std::string_view archetypeId, const ArchetypeSpawnPose& pose,
                                std::string_view overridesJson, ContentError* err = nullptr);

    // Spawns a saved archetype at pose. An unknown id logs and returns an invalid entity.
    // A scale of (0, 0, 0) becomes (1, 1, 1). Bare names such as hunter resolve to the archetype id.
    Entity respawnSavedArchetype(const ArchetypeSpawnHost& host, std::string_view archetypeId, const ArchetypeSpawnPose& pose);

} // namespace Dark
