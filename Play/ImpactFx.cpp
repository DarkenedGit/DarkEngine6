#include "Play/ImpactFx.h"

#include "AI/AiComponents.h"
#include "Animation/AnimGraphComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/WeaponTargets.h"
#include "Render/DebugRenderState.h"
#include "Render/DecalBasis.h"
#include "Render/DecalTypes.h"
#include "Render/Renderer.h"
#include "Render/ScenePath.h"
#include "Render/SceneRenderer.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainGround.h"

namespace Dark
{

    using namespace Math;

    bool impactDecalsReady(const ImpactFxHost& host)
    {
        return host.renderer && host.scene && host.renderer->scenePath() == ScenePath::HybridDeferred && host.renderer->debugState().decalsEnabled && host.scene->decalsReady();
    }

    bool impactIsStepCue(const ImpactFxHost& host, Entity e, const char* cue)
    {
        const char* groundCue = nullptr;
        if (host.world && host.ground && e.valid())
        {
            if (const TransformComponent* xf = host.world->get<TransformComponent>(e))
                groundCue = Terrain::groundContactAt(*host.ground, host.contactGrid, xf->position.x, xf->position.z).cue;
        }
        return decalCueIsGroundStep(cue, groundCue);
    }

    void spawnFootmark(const ImpactFxHost& host, const Vector3f& bodyPos, const Vector3f& facing)
    {
        if (!impactDecalsReady(host) || !host.scene)
            return;
        const float    groundY = host.terrain ? host.terrain->heightAtWorld(bodyPos.x, bodyPos.z) : bodyPos.y;
        DecalSpawnDesc desc;
        desc.kind     = DecalKind::Footmark;
        desc.space    = DecalSpace::World;
        desc.position = Vector3f(bodyPos.x, groundY, bodyPos.z);
        desc.axisY    = Vector3f(0.0f, 1.0f, 0.0f);
        desc.axisX    = decalFlatFacing(facing);
        host.scene->spawnDecal(desc);
    }

    void spawnHunterFootmark(const ImpactFxHost& host, Entity hunter)
    {
        if (!impactDecalsReady(host) || !host.world)
            return;
        const TransformComponent* xf = hunter.valid() ? host.world->get<TransformComponent>(hunter) : nullptr;
        if (!xf)
            return;
        Vector3f facing(0.0f, 0.0f, 1.0f);
        if (const AiAgentComponent* ai = host.world->get<AiAgentComponent>(hunter))
            facing = ai->forward;
        spawnFootmark(host, xf->position, facing);
    }

    void spawnWeaponImpact(const ImpactFxHost& host, const WeaponHit& hit)
    {
        if (!impactDecalsReady(host) || !host.scene || !decalPointFinite(hit.point))
            return;
        const Vector3f normal = decalSafeNormal(hit.normal);
        DecalSpawnDesc desc;
        desc.kind   = DecalKind::Impact;
        desc.weapon = hit.weapon;
        desc.axisY  = normal;
        desc.axisX  = decalImpactAxisX(normal, hit.direction);
        Entity victim = hit.targetEntity;
        if (!victim.valid() && host.targets)
            victim = host.targets->entityAt(hit.targetIndex);
        const bool attach = host.world && hit.hitTarget && victim.valid() && host.world->alive(victim) && host.world->get<TransformComponent>(victim);
        if (attach)
        {
            desc.entity   = victim;
            desc.position = decalShellBiasedPosition(hit.point, normal);
            if (host.world->has<AnimGraphComponent>(victim))
            {
                desc.space = DecalSpace::Bone;
                desc.bone  = -1;
            }
            else
                desc.space = DecalSpace::Entity;
        }
        else
        {
            desc.space    = DecalSpace::World;
            desc.position = hit.point;
        }
        host.scene->spawnDecal(desc);
    }

    void spawnLivingBloodDecal(const ImpactFxHost& host, Entity victim, const Vector3f& point, const Vector3f& hitNormal)
    {
        if (!impactDecalsReady(host) || !host.scene || !host.world || !decalPointFinite(point))
            return;
        if (!victim.valid() || !host.world->alive(victim) || !host.world->get<TransformComponent>(victim))
            return;
        const Vector3f normal = decalSafeNormal(hitNormal);
        DecalSpawnDesc desc;
        desc.kind     = DecalKind::Blood;
        desc.entity   = victim;
        desc.position = decalShellBiasedPosition(point, normal);
        desc.axisY    = normal;
        desc.axisX    = decalImpactAxisX(normal, Vector3f(1.0f, 0.0f, 0.0f));
        if (host.world->has<AnimGraphComponent>(victim))
        {
            desc.space = DecalSpace::Bone;
            desc.bone  = -1;
        }
        else
            desc.space = DecalSpace::Entity;
        host.scene->spawnDecal(desc);
    }

    void spawnDeathBloodDecal(const ImpactFxHost& host, const Vector3f& point, const Vector3f& hitNormal)
    {
        if (!impactDecalsReady(host) || !host.scene || !decalPointFinite(point))
            return;
        const float groundY = host.terrain ? host.terrain->heightAtWorld(point.x, point.z) : point.y;
        const Vector3f terrainNormal = host.terrain ? host.terrain->normalAtWorld(point.x, point.z) : Vector3f(0.0f, 1.0f, 0.0f);
        DecalSpawnDesc desc;
        desc.kind     = DecalKind::Blood;
        desc.space    = DecalSpace::World;
        desc.position = Vector3f(point.x, groundY, point.z);
        desc.axisY    = decalDeathAxisY(hitNormal, terrainNormal);
        desc.axisX    = decalYawFacing(point.x, point.z);
        host.scene->spawnDecal(desc);
    }

} // namespace Dark
