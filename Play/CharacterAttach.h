#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

#include <cstdint>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    namespace Terrain
    {
        class TerrainGround;
    }

    // Services a character attach needs. Physics, inventory, and proxy meshes stay with the caller.
    struct CharacterHost
    {
        World&                        world;
        Renderer&                     renderer;
        AssetManager&                 assets;
        AssetPinTable&                pins;
        Audio::AudioSystem&           audio;
        const Terrain::TerrainGround& ground;
    };

    // AnimatedOnly refuses a model that has no skeleton. AllowStatic still pins that model.
    enum class ModelAttach : uint8_t
    {
        AnimatedOnly,
        AllowStatic,
    };

    enum class NpcKind : uint8_t
    {
        Hunter,
        Wolf,
    };

    struct ContentModelResult
    {
        bool attached = false;
        bool skinned  = false;
    };

    // Does not skip an entity that already has a model. Callers keep that early-out.
    ContentModelResult attachContentModel(const CharacterHost& host, Entity e, const char* gltfPath, ModelAttach mode);

    // Adds health, hit reaction, a hittable, and posed hit volumes only when each one is missing.
    void ensureHumanoidBody(World& world, Entity e, float maxHp, const Math::Vector3f& halfExtents);

    // Motor, HSM, weapons, throwable belt, shield, sounds, and the player skill profile. Melee timing is set only when the loadout is created.
    // Movement speeds are re-read from entities/player.entity.movement.json. Crouch, dodge, and motor progress stay as they are.
    void ensureLocalPlayerRig(const CharacterHost& host, Entity e, WeaponHitFn hitFn, void* hitUser);

    // Extents, wolf tag and skill, hunter sounds, and that type's movement file. A hunter with no interact
    // mark becomes a guard. A wolf loses that mark. Does not load a model.
    void configureNpc(const CharacterHost& host, Entity e, NpcKind kind);

    // Re-reads player, hunter, and wolf movement files onto bodies already in the world.
    // A missing file leaves that body on the speeds it already has. Wolf is the tag "Wolf" or "wolf".
    // An agent that is not a wolf and has no interact mark becomes a guard, so an old save can still be taken down.
    void applyAuthoredMovement(World& world);

    // Model, then configureNpc. Extents and sounds are applied even when the model fails to load.
    ContentModelResult attachNpcVisual(const CharacterHost& host, Entity e, NpcKind kind, ModelAttach mode);

} // namespace Dark
