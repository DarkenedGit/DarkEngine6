#pragma once

#include "Collision/HitResult.h"
#include "ECS/Entity.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

namespace Dark
{

    struct PlayHost;
    struct PlayRuntime;

    namespace Combat
    {
        class CombatSystem;
        class JumpAttack;
        struct PoiseComponent;
        struct DamageEvent;
    }

    // Terrain rays and the shoulder-camera refresh stay with the session.
    struct PlayerCombatHooks
    {
        Collision::RayHit3D (*raycastTerrain)(void* user, const Math::Ray3f& ray, float maxDistance) = nullptr;
        float (*terrainHeight)(void* user, float x, float z)                                         = nullptr;
        void* terrainUser                                                                            = nullptr;
        void (*refreshCamera)(void* user)                                                            = nullptr;
        void* cameraUser                                                                             = nullptr;
    };

    bool             playerOwnsWeapon(const PlayHost& host, WeaponKind kind);
    WeaponWorldQuery makePlayerWeaponQuery(PlayHost& host, PlayRuntime& runtime, const PlayerCombatHooks& hooks);

    void tickPlayerThrows(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks);
    bool releasePlayerThrow(PlayHost& host, PlayRuntime& runtime, bool charged);
    void tickPlayerLoadout(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks);
    bool firePlayerLoadout(PlayHost& host, PlayRuntime& runtime, bool charged, const PlayerCombatHooks& hooks);
    void switchPlayerWeapons(PlayHost& host);

    void onPlayerWeaponHit(PlayHost& host, PlayRuntime& runtime, const WeaponHit& hit);
    void resolvePlayerJumpHits(PlayHost& host, PlayRuntime& runtime, Combat::CombatSystem& combat, const Combat::DamageEvent* events, int count);

    // Pound, pounce connect, and poise hyper-armor. True when the pound cue replaces the land cue.
    bool tickPlayerJumpAttack(PlayHost& host, PlayRuntime& runtime, Combat::CombatSystem& combat, Combat::JumpAttack& jump, Combat::PoiseComponent* poise, Entity body,
                              const Math::Vector3f& position, const Math::Vector3f& lookFlat, bool splashed, bool landed, bool alive, float dt, const PlayerCombatHooks& hooks);

    // Jump-attack buffer, throws, and sword or rifle fire. Sense XP stays here because every return passes through it.
    void tickPlayerAttack(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks);

} // namespace Dark
