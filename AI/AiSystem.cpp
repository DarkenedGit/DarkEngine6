#include "AI/AiSystem.h"

#include "AI/HsmGraph.h"
#include "AI/Sight.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Weapons/HittableComponent.h"

#include <cmath>
#include <random>

namespace Dark
{
    using Math::Vector3f;

    bool AiSystem::bake(const AI::WalkabilityDesc& desc)
    {
        m_agentR = desc.agentRadius > 0.0f ? desc.agentRadius : 0.8f;
        if (!m_walk.bake(desc))
            return false;
        return m_finder.bind(&m_walk);
    }

    void AiSystem::setHunterHitReaction(World& world, const HitReactionSettings& settings)
    {
        m_hunterHit = settings;
        world.each<HitReactionComponent>([&](Entity e, HitReactionComponent& hit) {
            if (!world.has<AiAgentComponent>(e))
                return;
            hit.hit.setSettings(m_hunterHit);
        });
    }

    Entity AiSystem::spawnHunter(World& world, AssetPinTable& pins, AssetManager& assets, AssetRef<Material> aiMat, const TransformComponent& xf)
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Hunter");
        world.emplace<TransformComponent>(e, xf);

        MeshComponent mc{};
        mc.primitive  = PrimitiveMesh::Cube;
        mc.matAssetID = (aiMat && aiMat->id != NULL_ASSET) ? aiMat->id : NULL_ASSET;
        mc.castShadow = true;
        setMeshComponent(world, pins, assets, e, mc);

        HealthSettings hs;
        hs.maxHp       = 48.0f;
        hs.regenPerSec = 5.0f;
        hs.regenDelay  = 4.0f;
        HealthComponent hc{};
        hc.health = Health{ hs };
        world.emplace<HealthComponent>(e, std::move(hc));

        HitReactionComponent hr{};
        hr.hit.setSettings(m_hunterHit);
        hr.hit.reset();
        world.emplace<HitReactionComponent>(e, std::move(hr));

        HittableComponent hit{};
        hit.halfExtents = Vector3f{ 1.0f, 1.0f, 1.0f };
        world.emplace<HittableComponent>(e, hit);

        AiAgentComponent ai{};
        ai.forward = Vector3f{ 0.0f, 0.0f, 1.0f };
        world.emplace<AiAgentComponent>(e, ai);

        PathAgentComponent path{};
        path.radius = m_agentR;
        world.emplace<PathAgentComponent>(e, path);
        world.emplace<SightComponent>(e);

        BrainComponent brain{};
        brain.brain = std::make_unique<AI::Brain>();
        if (AssetRef<HsmGraphDef> graph = assets.tryLoadHsmGraph("ai/hunter.hsm.json"))
        {
            if (!brain.brain->load(*graph))
                DE_LOG_WARN(LogCategory::AI, "AiSystem: hunter.hsm.json failed, using built-in graph");
        }
        if (!brain.brain || !brain.brain->start())
        {
            DE_LOG_ERROR(LogCategory::AI, "AiSystem: hunter brain start failed");
            onEntityRemoved(world, e, &pins);
            world.destroyEntity(e);
            return {};
        }
        world.emplace<BrainComponent>(e, std::move(brain));
        return e;
    }

    bool AiSystem::bind(World& world, Entity e, View& v)
    {
        v.e      = e;
        v.xf     = world.get<TransformComponent>(e);
        v.ai     = world.get<AiAgentComponent>(e);
        v.path   = world.get<PathAgentComponent>(e);
        v.health = world.get<HealthComponent>(e);
        v.hit    = world.get<HitReactionComponent>(e);
        v.brain  = world.get<BrainComponent>(e);
        v.sight  = world.get<SightComponent>(e);
        return v.xf && v.ai && v.path && v.health && v.hit && v.brain && v.brain->brain;
    }

    void AiSystem::collectHunters(World& world)
    {
        m_scratch.clear();
        world.each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
            if (world.has<HealthComponent>(e) && world.has<TransformComponent>(e))
                m_scratch.push_back(e);
        });
    }

    void AiSystem::tickHealthAndRespawn(World& world, float dt)
    {
        for (Entity e : m_scratch)
        {
            View v{};
            if (!bind(world, e, v))
                continue;
            if (v.health->health.alive())
            {
                v.health->health.tick(dt);
                continue;
            }
            v.ai->deadFor += dt;
            if (v.ai->deadFor < 8.0f || !m_walk.valid())
                continue;

            std::mt19937                          rng{ static_cast<unsigned>((m_time + v.ai->deadFor) * 1000.0f) + 91u };
            std::uniform_real_distribution<float> ux(-22.0f, 22.0f);
            std::uniform_real_distribution<float> uz(-22.0f, 22.0f);
            bool                                  placed = false;
            for (int tries = 0; tries < 64; ++tries)
            {
                const float x = ux(rng);
                const float z = uz(rng);
                if (!m_walk.walkableWorld(x, z))
                    continue;
                v.xf->position = Vector3f{ x, 0.0f, z };
                if (const Terrain::HeightMap* hm = m_walk.heightMap())
                    v.xf->position.y = hm->heightAtWorld(x, z) + 1.0f;
                placed = true;
                break;
            }
            if (!placed)
                continue;
            v.health->health.revive();
            v.hit->hit.setSettings(m_hunterHit);
            v.hit->hit.reset();
            v.ai->assistLeft  = 0.0f;
            v.ai->fleeLeft    = 0.0f;
            v.ai->deadFor     = 0.0f;
            v.path->path.points.clear();
            v.path->waypoint  = 0;
            v.ai->givenUp     = false;
            v.ai->hasLastSeen = false;
            v.ai->forward     = Vector3f{ 0.0f, 0.0f, 1.0f };
            v.brain->brain->start();
            DE_LOG_INFO(LogCategory::AI, "Hunter recovered");
        }
    }

    void AiSystem::integrateHitReaction(View& v, float dt, Terrain::TerrainWorld& terrain)
    {
        const Vector3f d = v.hit->hit.tick(dt);
        if (d.MagnitudeSqrd() < 1.0e-10f)
            return;

        const float nx = v.xf->position.x + d.x;
        const float nz = v.xf->position.z + d.z;
        if (!m_walk.valid())
        {
            v.xf->position.x = nx;
            v.xf->position.z = nz;
        }
        else if (m_walk.walkableWorld(nx, nz))
        {
            v.xf->position.x = nx;
            v.xf->position.z = nz;
        }
        else if (m_walk.walkableWorld(nx, v.xf->position.z))
            v.xf->position.x = nx;
        else if (m_walk.walkableWorld(v.xf->position.x, nz))
            v.xf->position.z = nz;

        v.xf->position.y = terrain.heightAtWorld(v.xf->position.x, v.xf->position.z) + 1.0f;
        Vector3f face{ d.x, 0.0f, d.z };
        if (face.MagnitudeSqrd() > 1.0e-8f)
        {
            face.Normalize();
            v.ai->forward = face;
        }
    }

    void AiSystem::follow(View& v, float dt, Terrain::TerrainWorld& terrain, float speed)
    {
        if (v.ai->givenUp || v.path->path.points.empty())
            return;
        if (speed < 0.0f)
            speed = 0.0f;
        float remain = speed * dt;
        while (remain > 0.0f && v.path->waypoint < static_cast<int>(v.path->path.points.size()))
        {
            const Vector3f& wp = v.path->path.points[static_cast<size_t>(v.path->waypoint)];
            Vector3f        d{ wp.x - v.xf->position.x, 0.0f, wp.z - v.xf->position.z };
            const float     dist = d.Magnitude();
            if (dist < 1.0f)
            {
                ++v.path->waypoint;
                continue;
            }
            const float step = remain < dist ? remain : dist;
            d *= (1.0f / dist);
            const Vector3f next{ v.xf->position.x + d.x * step, 0.0f, v.xf->position.z + d.z * step };
            if (!m_walk.walkableWorld(next.x, next.z))
                break;
            v.xf->position.x = next.x;
            v.xf->position.z = next.z;
            remain -= step;
        }
        v.xf->position.y = terrain.heightAtWorld(v.xf->position.x, v.xf->position.z) + 1.0f;
    }

    void AiSystem::repath(World& world, View& v, float destX, float destZ)
    {
        AI::AgentStamp stamps[8];
        int            n = 0;
        for (Entity other : m_scratch)
        {
            if (other.id() == v.e.id() || n >= 8)
                continue;
            const TransformComponent* ox = world.get<TransformComponent>(other);
            const PathAgentComponent* op = world.get<PathAgentComponent>(other);
            if (!ox)
                continue;
            stamps[n].x      = ox->position.x;
            stamps[n].z      = ox->position.z;
            stamps[n].radius = op ? op->radius : m_agentR;
            ++n;
        }
        AI::PathRequest req;
        req.startX     = v.xf->position.x;
        req.startZ     = v.xf->position.z;
        req.destX      = destX;
        req.destZ      = destZ;
        req.others     = stamps;
        req.otherCount = n;
        AI::PathResult next;
        if (!m_finder.find(req, next) || next.points.empty())
        {
            v.ai->givenUp = true;
            v.path->path.points.clear();
            v.path->waypoint = 0;
            return;
        }
        v.ai->givenUp    = false;
        v.path->path     = std::move(next);
        v.path->waypoint = 0;
        v.path->repathAt = m_time + 0.5f;
    }

    bool AiSystem::pickWanderDest(View& v)
    {
        std::mt19937                          rng{ static_cast<unsigned>(m_time * 1000.0f) + 17u };
        std::uniform_real_distribution<float> off(-18.0f, 18.0f);
        const int                             island = m_walk.islandWorld(v.xf->position.x, v.xf->position.z);
        for (int tries = 0; tries < 48; ++tries)
        {
            const float x = v.xf->position.x + off(rng);
            const float z = v.xf->position.z + off(rng);
            if (!m_walk.walkableWorld(x, z))
                continue;
            if (m_walk.islandWorld(x, z) != island)
                continue;
            const float dx = x - v.xf->position.x;
            const float dz = z - v.xf->position.z;
            if (dx * dx + dz * dz < 16.0f)
                continue;
            v.ai->wanderDest = Vector3f{ x, 0.0f, z };
            return true;
        }
        return false;
    }

    bool AiSystem::pickFleeDest(View& v, const Vector3f& playerPos)
    {
        Vector3f away{ v.xf->position.x - playerPos.x, 0.0f, v.xf->position.z - playerPos.z };
        if (away.MagnitudeSqrd() < 1.0e-6f)
            away = v.ai->forward;
        away.y = 0.0f;
        if (away.MagnitudeSqrd() < 1.0e-6f)
            away = Vector3f{ 0.0f, 0.0f, 1.0f };
        else
            away.Normalize();
        Vector3f    side{ -away.z, 0.0f, away.x };
        const int   island     = m_walk.islandWorld(v.xf->position.x, v.xf->position.z);
        const float dists[]    = { 14.0f, 18.0f, 10.0f, 22.0f, 8.0f };
        const float laterals[] = { 0.0f, 4.0f, -4.0f, 8.0f, -8.0f };
        for (float dist : dists)
        {
            for (float lat : laterals)
            {
                const float x = v.xf->position.x + away.x * dist + side.x * lat;
                const float z = v.xf->position.z + away.z * dist + side.z * lat;
                if (!m_walk.walkableWorld(x, z))
                    continue;
                if (m_walk.islandWorld(x, z) != island)
                    continue;
                v.ai->wanderDest = Vector3f{ x, 0.0f, z };
                return true;
            }
        }
        return pickWanderDest(v);
    }

    bool AiSystem::hunterSeesPoint(const View& v, const Vector3f& worldPos) const
    {
        AI::SightQuery q;
        q.eye       = Vector3f{ v.xf->position.x, v.xf->position.y + 0.5f, v.xf->position.z };
        q.forward   = v.ai->forward;
        q.target    = Vector3f{ worldPos.x, worldPos.y + 0.5f, worldPos.z };
        q.coneDeg   = v.sight ? v.sight->coneDeg : 70.0f;
        q.range     = v.sight ? v.sight->range : 25.0f;
        q.heightMap = m_walk.heightMap();
        return q.heightMap && AI::sees(q);
    }

    void AiSystem::beginAssist(View& v, const Vector3f& helpPos)
    {
        if (v.brain->brain->leaf() == AI::Leaf::Flee)
            return;
        v.ai->assistLeft = m_pack.assistSeconds;
        v.ai->helpPos    = helpPos;
        v.path->repathAt = 0.0f;
        v.brain->brain->onAssist();
    }

    void AiSystem::beginFlee(View& v)
    {
        v.ai->fleeLeft   = m_pack.fleeSeconds;
        v.ai->assistLeft = 0.0f;
        v.path->path.points.clear();
        v.path->waypoint = 0;
        v.path->repathAt = 0.0f;
        v.brain->brain->onFlee();
    }

    void AiSystem::tickHunters(World& world, Terrain::TerrainWorld& terrain, bool playerInWater, float dt, Entity player)
    {
        m_time += dt;
        collectHunters(world);
        tickHealthAndRespawn(world, dt);

        Vector3f playerPos{};
        if (const TransformComponent* px = player.valid() ? world.get<TransformComponent>(player) : nullptr)
            playerPos = px->position;

        constexpr float kStandoff = 2.25f;
        for (Entity e : m_scratch)
        {
            View v{};
            if (!bind(world, e, v) || !v.health->health.alive())
                continue;
            integrateHitReaction(v, dt, terrain);
            const float dx       = v.xf->position.x - playerPos.x;
            const float dz       = v.xf->position.z - playerPos.z;
            const bool  standoff = (dx * dx + dz * dz) <= kStandoff * kStandoff;

            AI::SightQuery q;
            q.eye       = Vector3f{ v.xf->position.x, v.xf->position.y + 0.5f, v.xf->position.z };
            q.forward   = v.ai->forward;
            q.target    = Vector3f{ playerPos.x, playerPos.y + 0.5f, playerPos.z };
            q.coneDeg   = v.sight ? v.sight->coneDeg : 70.0f;
            q.range     = v.sight ? v.sight->range : 25.0f;
            q.heightMap = m_walk.heightMap();
            const bool sees = !playerInWater && (standoff || (q.heightMap && AI::sees(q)));
            if (sees)
            {
                v.ai->lastSeen    = playerPos;
                v.ai->hasLastSeen = true;
            }
            if (v.ai->fleeLeft > 0.0f)
            {
                v.ai->fleeLeft -= dt;
                if (v.ai->fleeLeft <= 0.0f)
                {
                    v.ai->fleeLeft = 0.0f;
                    v.brain->brain->onFleeDone();
                }
            }
            else if (v.ai->assistLeft > 0.0f)
            {
                v.ai->assistLeft -= dt;
                if (v.ai->assistLeft <= 0.0f)
                {
                    v.ai->assistLeft = 0.0f;
                    v.brain->brain->onAssistDone();
                }
            }

            if (v.brain->brain->leaf() != AI::Leaf::Flee && v.brain->brain->leaf() != AI::Leaf::Chase)
            {
                const float allyR2 = m_pack.assistAllyRadius * m_pack.assistAllyRadius;
                for (Entity other : m_scratch)
                {
                    if (other.id() == e.id())
                        continue;
                    View ov{};
                    if (!bind(world, other, ov) || !ov.health->health.alive())
                        continue;
                    const float adx = ov.xf->position.x - playerPos.x;
                    const float adz = ov.xf->position.z - playerPos.z;
                    if (adx * adx + adz * adz > allyR2)
                        continue;
                    if (!hunterSeesPoint(v, ov.xf->position))
                        continue;
                    beginAssist(v, playerPos);
                    break;
                }
            }

            v.brain->brain->tick(dt, sees, playerInWater);
            if (playerInWater)
            {
                v.ai->assistLeft = 0.0f;
                v.ai->fleeLeft   = 0.0f;
            }
            if (v.hit->hit.stunned())
                continue;

            if (standoff)
            {
                Vector3f to{ -dx, 0.0f, -dz };
                if (to.MagnitudeSqrd() > 1.0e-6f)
                {
                    to.Normalize();
                    v.ai->forward = to;
                }
            }

            const AI::Leaf leaf   = v.brain->brain->leaf();
            const bool     sprint = leaf == AI::Leaf::Assist || leaf == AI::Leaf::Flee || v.ai->assistLeft > 0.0f;
            const float    speed  = sprint ? m_pack.sprintSpeed : m_pack.walkSpeed;

            if (leaf == AI::Leaf::Flee)
            {
                const bool arrived = v.path->path.points.empty() || v.path->waypoint >= static_cast<int>(v.path->path.points.size());
                if (arrived)
                {
                    if (pickFleeDest(v, playerPos))
                        repath(world, v, v.ai->wanderDest.x, v.ai->wanderDest.z);
                }
                else if (m_time >= v.path->repathAt)
                    repath(world, v, v.ai->wanderDest.x, v.ai->wanderDest.z);
            }
            else if ((leaf == AI::Leaf::Chase || leaf == AI::Leaf::Memory || leaf == AI::Leaf::Assist) && standoff)
                continue;
            else if (leaf == AI::Leaf::Wander)
            {
                const bool arrived = v.path->path.points.empty() || v.path->waypoint >= static_cast<int>(v.path->path.points.size());
                if (arrived)
                {
                    if (pickWanderDest(v))
                        repath(world, v, v.ai->wanderDest.x, v.ai->wanderDest.z);
                }
                else if (m_time >= v.path->repathAt)
                    repath(world, v, v.ai->wanderDest.x, v.ai->wanderDest.z);
            }
            else if (leaf == AI::Leaf::Chase)
            {
                if (m_time >= v.path->repathAt || v.path->path.points.empty())
                    repath(world, v, playerPos.x, playerPos.z);
            }
            else if (leaf == AI::Leaf::Assist)
            {
                if (m_time >= v.path->repathAt || v.path->path.points.empty())
                    repath(world, v, v.ai->helpPos.x, v.ai->helpPos.z);
            }

            const Vector3f before = v.xf->position;
            follow(v, dt, terrain, speed);
            Vector3f move{ v.xf->position.x - before.x, 0.0f, v.xf->position.z - before.z };
            if (move.MagnitudeSqrd() > 1.0e-6f)
            {
                move.Normalize();
                v.ai->forward = move;
            }
        }
    }

    bool AiSystem::applyHunterDamage(World& world, Entity e, float amount)
    {
        HealthComponent* hp = world.get<HealthComponent>(e);
        if (!hp)
            return false;
        const float before = hp->health.hp();
        const bool  killed = hp->health.applyDamage(amount);
        if (killed)
        {
            if (AiAgentComponent* ai = world.get<AiAgentComponent>(e))
                ai->deadFor = 0.0f;
            if (PathAgentComponent* path = world.get<PathAgentComponent>(e))
            {
                path->path.points.clear();
                path->waypoint = 0;
            }
            if (HitReactionComponent* hit = world.get<HitReactionComponent>(e))
                hit->hit.reset();
            DE_LOG_INFO(LogCategory::AI, "Hunter down");
        }
        return hp->health.hp() < before;
    }

    void AiSystem::applyHunterHitReaction(World& world, Entity e, const Vector3f& hitDirection)
    {
        HealthComponent* hp = world.get<HealthComponent>(e);
        if (!hp || !hp->health.alive())
            return;
        if (HitReactionComponent* hit = world.get<HitReactionComponent>(e))
            hit->hit.apply(hitDirection);
    }

    void AiSystem::onHunterAttacked(World& world, Entity victim, const Vector3f& playerPos)
    {
        const TransformComponent* srcXf = world.get<TransformComponent>(victim);
        if (!srcXf)
            return;
        const Vector3f src = srcXf->position;
        const float    r2  = m_pack.alertRange * m_pack.alertRange;
        collectHunters(world);
        for (Entity e : m_scratch)
        {
            if (e.id() == victim.id())
                continue;
            View v{};
            if (!bind(world, e, v) || !v.health->health.alive())
                continue;
            const float dx = v.xf->position.x - src.x;
            const float dz = v.xf->position.z - src.z;
            if (dx * dx + dz * dz > r2)
                continue;
            beginAssist(v, playerPos);
            DE_LOG_INFO(LogCategory::AI, "Hunter alerted by attack");
        }
    }

    void AiSystem::onHunterKilled(World& world, Entity victim)
    {
        const TransformComponent* whereXf = world.get<TransformComponent>(victim);
        if (!whereXf)
            return;
        const Vector3f where = whereXf->position;
        collectHunters(world);
        for (Entity e : m_scratch)
        {
            if (e.id() == victim.id())
                continue;
            View v{};
            if (!bind(world, e, v) || !v.health->health.alive())
                continue;
            if (!hunterSeesPoint(v, where))
                continue;
            beginFlee(v);
            DE_LOG_INFO(LogCategory::AI, "Hunter fled after seeing a kill");
        }
    }

} // namespace Dark
