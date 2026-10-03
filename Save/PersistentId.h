#pragma once

#include "Core/UUID.h"
#include "ECS/Entity.h"

#include <cstdint>
#include <string_view>

namespace Dark
{
    class World;

    enum class PersistOrigin : uint8_t
    {
        Authored = 0,
        Procedural,
        Spawned
    };

    struct PersistentIdComponent
    {
        static constexpr const char* kTypeName = "PersistentId";

        UUID          id{ 0ull };
        PersistOrigin origin = PersistOrigin::Authored;
        char          archetype[24]{};
    };

    UUID makeProceduralId(std::string_view key);

    PersistentIdComponent& stampPersistentId(World& world, Entity e, UUID id, PersistOrigin origin, std::string_view archetype);
    PersistentIdComponent& stampProceduralId(World& world, Entity e, std::string_view key, std::string_view archetype = {});
    PersistentIdComponent& stampAuthoredId(World& world, Entity e, std::string_view scenePath, int index, std::string_view type);
    PersistentIdComponent& stampSpawned(World& world, Entity e, std::string_view archetype);

    const char* persistOriginName(PersistOrigin origin);
    bool        tryParsePersistOrigin(std::string_view text, PersistOrigin& out);

} // namespace Dark
