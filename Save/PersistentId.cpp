#include "Save/PersistentId.h"

#include "ECS/World.h"
#include "Save/SaveTypes.h"

#include <cstring>
#include <format>
#include <string>

namespace Dark
{
    namespace
    {
        void copyArchetype(char (&dst)[24], std::string_view src)
        {
            const size_t n = src.size() < 23 ? src.size() : 23;
            if (n > 0)
                std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        }
    } // namespace

    UUID makeProceduralId(std::string_view key)
    {
        uint64_t hash = Save::fnv1a64(key.data(), key.size());
        hash |= (1ull << 63);
        return UUID{ hash };
    }

    PersistentIdComponent& stampPersistentId(World& world, Entity e, UUID id, PersistOrigin origin, std::string_view archetype)
    {
        PersistentIdComponent* existing = world.get<PersistentIdComponent>(e);
        PersistentIdComponent& slot     = existing ? *existing : world.emplace<PersistentIdComponent>(e);
        slot.id                         = id;
        slot.origin                     = origin;
        copyArchetype(slot.archetype, archetype);
        return slot;
    }

    PersistentIdComponent& stampProceduralId(World& world, Entity e, std::string_view key, std::string_view archetype)
    {
        return stampPersistentId(world, e, makeProceduralId(key), PersistOrigin::Procedural, archetype);
    }

    PersistentIdComponent& stampAuthoredId(World& world, Entity e, std::string_view scenePath, int index, std::string_view type)
    {
        const std::string key = std::format("scene/{}/{}/{}", scenePath, index, type);
        return stampPersistentId(world, e, makeProceduralId(key), PersistOrigin::Authored, type);
    }

    PersistentIdComponent& stampSpawned(World& world, Entity e, std::string_view archetype)
    {
        return stampPersistentId(world, e, UUID{}, PersistOrigin::Spawned, archetype);
    }

    const char* persistOriginName(PersistOrigin origin)
    {
        switch (origin)
        {
        case PersistOrigin::Authored:    return "authored";
        case PersistOrigin::Procedural:  return "procedural";
        case PersistOrigin::Spawned:     return "spawned";
        }
        return "authored";
    }

    bool tryParsePersistOrigin(std::string_view text, PersistOrigin& out)
    {
        if (text == "authored")
        {
            out = PersistOrigin::Authored;
            return true;
        }
        if (text == "procedural")
        {
            out = PersistOrigin::Procedural;
            return true;
        }
        if (text == "spawned")
        {
            out = PersistOrigin::Spawned;
            return true;
        }
        return false;
    }

} // namespace Dark
