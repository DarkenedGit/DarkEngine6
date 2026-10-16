#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

namespace Dark
{

    class Renderer;
    class SceneRenderer;
    class WeaponTargetList;
    class World;

    namespace Terrain
    {
        class TerrainGrid;
        class TerrainGround;
    }

    struct ImpactFxHost
    {
        World*                        world       = nullptr;
        SceneRenderer*                scene       = nullptr;
        Renderer*                     renderer    = nullptr;
        Terrain::TerrainGrid*         terrain     = nullptr;
        const Terrain::TerrainGround* ground      = nullptr;
        const Terrain::TerrainGrid*   contactGrid = nullptr;
        WeaponTargetList*             targets     = nullptr;
    };

    bool impactDecalsReady(const ImpactFxHost& host);
    bool impactIsStepCue(const ImpactFxHost& host, Entity e, const char* cue);
    void spawnFootmark(const ImpactFxHost& host, const Math::Vector3f& bodyPos, const Math::Vector3f& facing);
    void spawnHunterFootmark(const ImpactFxHost& host, Entity hunter);
    void spawnWeaponImpact(const ImpactFxHost& host, const WeaponHit& hit);
    void spawnLivingBloodDecal(const ImpactFxHost& host, Entity victim, const Math::Vector3f& point, const Math::Vector3f& hitNormal);
    void spawnDeathBloodDecal(const ImpactFxHost& host, const Math::Vector3f& point, const Math::Vector3f& hitNormal);

} // namespace Dark
