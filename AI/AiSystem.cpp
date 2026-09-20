#include "AI/AiSystem.h"

#include "AI/HsmGraph.h"
#include "AI/Sight.h"
#include "Assets/AssetManager.h"
#include "Collision/SweptCollision.h"
#include "Combat/CombatSystem.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Sphere3f.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/Weapon.h"

#include <cmath>
#include <random>

namespace Dark
{
    using Math::Vector3f;

    namespace
    {
        constexpr float kHunterJumpMin     = 4.0f;
        constexpr float kHunterJumpMax     = 9.0f;
        constexpr float kSweepRadius       = 0.45f;
        constexpr float kBackStepMeters    = 0.5f;
        constexpr int   kBackStepTries     = 8;
        constexpr float kLookEps           = 1.0e-6f;

        Combat::JumpAttackDef hunterJumpDef()
        {
            Combat::JumpAttackDef def{};
            def.telegraphSeconds    = 0.40f;
            def.cooldown            = 3.5f;
            def.groundOffset        = 1.0f;
            def.connectDamage       = 22.0f;
            def.poundDamage         = 14.0f;
            def.leapVerticalSpeed   = 10.0f;
            def.leapForwardSpeedMax = 12.0f;
            def.gravity             = 24.0f;
            return def;
        }

        Vector3f flattenDir(const Vector3f& v, const Vector3f& fallback)
        {
            Vector3f d{ v.x, 0.0f, v.z };
            if (d.MagnitudeSqrd() > kLookEps)
            {
                d.Normalize();
                return d;
            }
            Vector3f f{ fallback.x, 0.0f, fallback.z };
            if (f.MagnitudeSqrd() > kLookEps)
            {
                f.Normalize();
                return f;
            }
            return Vector3f{ 0.0f, 0.0f, 1.0f };
        }

        void sweepCubes(Vector3f& position, const Vector3f& before, const Math::AABox3f* cubes, int cubeCount)
        {
            if (!cubes || cubeCount <= 0)
                return;
            Vector3f delta{ position.x - before.x, 0.0f, position.z - before.z };
            if (delta.MagnitudeSqrd() <= 1.0e-10f)
                return;
            Math::Sphere3f ball{ Vector3f{ before.x, position.y, before.z }, kSweepRadius };
            for (int i = 0; i < cubeCount; ++i)
            {
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, cubes[i]);
                if (hit.hit && hit.t < 1.0f)
                {
                    delta *= Math::Max(0.0f, hit.t - 0.02f);
                    break;
                }
            }
            position.x = before.x + delta.x;
            position.z = before.z + delta.z;
        }
    } // namespace

    bool AiSystem::bake(const AI::WalkabilityDesc& desc)
    {
        m_agentR = desc.agentRadius > 0.0f ? desc.agentRadius : 0.8f;
        m_waterY = desc.waterLevel;
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

    bool AiSystem::attachHunter(World& world, Entity e, AssetPinTable& pins, AssetManager& assets)
    {
        if (!e.valid() || !world.alive(e) || !world.get<TransformComponent>(e))
            return false;
        if (world.has<AiAgentComponent>(e))
            return true;

        if (!world.has<HealthComponent>(e))
        {
            HealthSettings hs;
            hs.maxHp       = 48.0f;
            hs.regenPerSec = 5.0f;
            hs.regenDelay  = 4.0f;
            HealthComponent hc{};
            hc.health = Health{ hs };
            world.emplace<HealthComponent>(e, std::move(hc));
        }
        if (!world.has<HitReactionComponent>(e))
        {
            HitReactionComponent hr{};
            hr.hit.setSettings(m_hunterHit);
            hr.hit.reset();
            world.emplace<HitReactionComponent>(e, std::move(hr));
        }
        if (!world.has<HittableComponent>(e))
        {
            HittableComponent hit{};
            hit.halfExtents = Vector3f{ 1.0f, 1.0f, 1.0f };
            world.emplace<HittableComponent>(e, hit);
        }

        AiAgentComponent ai{};
        ai.forward = Vector3f{ 0.0f, 0.0f, 1.0f };
        world.emplace<AiAgentComponent>(e, ai);

        if (!world.has<PathAgentComponent>(e))
        {
            PathAgentComponent path{};
            path.radius = m_agentR;
            world.emplace<PathAgentComponent>(e, path);
        }
        if (!world.has<SightComponent>(e))
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
            return false;
        }
        world.emplace<BrainComponent>(e, std::move(brain));
        if (!world.has<Combat::StatusEffectComponent>(e))
            world.emplace<Combat::StatusEffectComponent>(e);
        if (!world.has<Combat::PoiseComponent>(e))
            world.emplace<Combat::PoiseComponent>(e);
        if (!world.has<JumpAttackComponent>(e))
        {
            JumpAttackComponent jac{};
            jac.jump.setDef(hunterJumpDef());
            world.emplace<JumpAttackComponent>(e, std::move(jac));
        }
        (void)pins;
        return true;
    }

    Entity AiSystem::spawnHunter(World& world, AssetPinTable& pins, AssetManager& assets, const TransformComponent& xf)
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Hunter");
        world.emplace<TransformComponent>(e, xf);
        if (!attachHunter(world, e, pins, assets))
        {
            onEntityRemoved(world, e, &pins);
            world.destroyEntity(e);
            return {};
        }
        return e;
    }

    void AiSystem::setJumpAttackHits(void (*fn)(void* user, const Combat::DamageEvent* events, int count), void* user)
    {
        m_jumpHitsFn   = fn;
        m_jumpHitsUser = user;
    }

    void AiSystem::setHunterCue(void (*fn)(void* user, Entity hunter, const char* cue), void* user)
    {
        m_hunterCueFn   = fn;
        m_hunterCueUser = user;
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
            if (JumpAttackComponent* jac = world.get<JumpAttackComponent>(e))
            {
                if (jac->jump.busy())
                    DE_LOG_INFO(LogCategory::AI, "Hunter: jump death-cancel");
            }
            cancelJumpAndToken(world, e, false);
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
            cancelJumpAndToken(world, e, true);
            if (Combat::StatusEffectComponent* st = world.get<Combat::StatusEffectComponent>(e))
                st->reset();
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

    void AiSystem::seekToward(View& v, float dt, Terrain::TerrainWorld& terrain, float speed, float destX, float destZ)
    {
        Vector3f d{ destX - v.xf->position.x, 0.0f, destZ - v.xf->position.z };
        const float dist = d.Magnitude();
        if (dist < 0.45f)
            return;
        if (speed < 0.0f)
            speed = 0.0f;
        d *= (1.0f / dist);
        float step = speed * dt;
        if (step > dist)
            step = dist;
        v.xf->position.x += d.x * step;
        v.xf->position.z += d.z * step;
        v.xf->position.y = terrain.heightAtWorld(v.xf->position.x, v.xf->position.z) + 1.0f;
        v.ai->forward    = d;
        v.ai->givenUp    = false;
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

    void AiSystem::beginFlee(World& world, View& v)
    {
        cancelJumpAndToken(world, v.e, false);
        v.ai->fleeLeft   = m_pack.fleeSeconds;
        v.ai->assistLeft = 0.0f;
        v.path->path.points.clear();
        v.path->waypoint = 0;
        v.path->repathAt = 0.0f;
        v.brain->brain->onFlee();
    }

    void AiSystem::cancelJumpAndToken(World& world, Entity e, bool forceIdle)
    {
        if (JumpAttackComponent* jac = world.get<JumpAttackComponent>(e))
            jac->jump.cancel(forceIdle ? Combat::JumpAttackCancel::ForceIdle : Combat::JumpAttackCancel::NoPound);
        if (m_jumpAttackToken.valid() && m_jumpAttackToken.id() == e.id())
        {
            m_jumpAttackToken = {};
            DE_LOG_INFO(LogCategory::AI, "Hunter: jump token clear");
        }
    }

    void AiSystem::collectJumpTargets(World& world)
    {
        m_jumpTargets.clear();
        world.each<HittableComponent>([&](Entity e, HittableComponent&) {
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            const HealthComponent* hp = world.get<HealthComponent>(e);
            JumpTargetScratch t{};
            t.e      = e;
            t.center = xf->position;
            t.alive  = hp && hp->health.alive();
            m_jumpTargets.push_back(t);
        });
    }

    void AiSystem::resolveJumpHits(World& world, const Combat::DamageEvent* events, int count)
    {
        if (count <= 0 || !events)
            return;
        if (m_jumpHitsFn)
        {
            m_jumpHitsFn(m_jumpHitsUser, events, count);
            return;
        }
        Combat::CombatSystem combat;
        Combat::resolveJumpAttackEvents(world, combat, events, count);
    }

    void AiSystem::walkableBackStep(Vector3f& position, const Vector3f& incomingXZ) const
    {
        if (!m_walk.valid() || m_walk.walkableWorld(position.x, position.z))
            return;
        Vector3f dir{ incomingXZ.x, 0.0f, incomingXZ.z };
        if (dir.MagnitudeSqrd() < 1.0e-8f)
            return;
        dir.Normalize();
        dir = Vector3f{ -dir.x, 0.0f, -dir.z };
        for (int i = 1; i <= kBackStepTries; ++i)
        {
            const float x = position.x + dir.x * kBackStepMeters * static_cast<float>(i);
            const float z = position.z + dir.z * kBackStepMeters * static_cast<float>(i);
            if (m_walk.walkableWorld(x, z))
            {
                position.x = x;
                position.z = z;
                return;
            }
        }
    }

    bool AiSystem::packTokenBusy(World& world, Entity self) const
    {
        if (!m_jumpAttackToken.valid() || m_jumpAttackToken.id() == self.id())
            return false;
        const JumpAttackComponent* other = world.get<JumpAttackComponent>(m_jumpAttackToken);
        return other && other->jump.busy();
    }

    void AiSystem::tickHunters(World& world, Terrain::TerrainWorld& terrain, bool playerInWater, float dt, Entity player, const Math::AABox3f* cubes, int cubeCount)
    {
        m_time += dt;
        if (m_jumpAttackToken.valid() && !world.alive(m_jumpAttackToken))
            m_jumpAttackToken = {};
        collectHunters(world);
        for (Entity e : m_scratch)
        {
            if (Combat::StatusEffectComponent* st = world.get<Combat::StatusEffectComponent>(e))
                st->tick(dt);
        }
        tickHealthAndRespawn(world, dt);

        Vector3f playerPos{};
        bool     playerAlive = false;
        if (const TransformComponent* px = player.valid() ? world.get<TransformComponent>(player) : nullptr)
        {
            playerPos   = px->position;
            const HealthComponent* php = world.get<HealthComponent>(player);
            playerAlive = php && php->health.alive();
        }

        struct HeightCtx
        {
            Terrain::TerrainWorld* terrain = nullptr;
        };
        HeightCtx heightCtx{ &terrain };
        auto      heightAt = [](void* user, float x, float z) -> float {
            return static_cast<HeightCtx*>(user)->terrain->heightAtWorld(x, z);
        };

        WeaponWorldQuery jumpQuery{};
        jumpQuery.targetUser = this;
        jumpQuery.targetCount = [](void* user) -> int {
            return static_cast<int>(static_cast<AiSystem*>(user)->m_jumpTargets.size());
        };
        jumpQuery.targetAlive = [](void* user, int i) -> bool {
            auto* self = static_cast<AiSystem*>(user);
            return i >= 0 && i < static_cast<int>(self->m_jumpTargets.size()) && self->m_jumpTargets[static_cast<size_t>(i)].alive;
        };
        jumpQuery.targetCenter = [](void* user, int i) -> Vector3f {
            auto* self = static_cast<AiSystem*>(user);
            if (i < 0 || i >= static_cast<int>(self->m_jumpTargets.size()))
                return {};
            return self->m_jumpTargets[static_cast<size_t>(i)].center;
        };
        jumpQuery.targetEntityAt = [](void* user, int i) -> Entity {
            auto* self = static_cast<AiSystem*>(user);
            if (i < 0 || i >= static_cast<int>(self->m_jumpTargets.size()))
                return {};
            return self->m_jumpTargets[static_cast<size_t>(i)].e;
        };

        constexpr float kStandoff = 2.25f;
        for (Entity e : m_scratch)
        {
            View v{};
            if (!bind(world, e, v) || !v.health->health.alive())
                continue;

            JumpAttackComponent*           jac  = world.get<JumpAttackComponent>(e);
            Combat::JumpAttack*            jump = jac ? &jac->jump : nullptr;
            Combat::PoiseComponent*        poise = world.get<Combat::PoiseComponent>(e);
            Combat::StatusEffectComponent* stCc  = world.get<Combat::StatusEffectComponent>(e);

            if (jump)
            {
                jump->tick(dt);
                if (jump->phase() == Combat::JumpAttackPhase::Idle && m_jumpAttackToken.valid() && m_jumpAttackToken.id() == e.id())
                {
                    m_jumpAttackToken = {};
                    DE_LOG_INFO(LogCategory::AI, "Hunter: jump token clear");
                }
                // Player in water: cancel before takeoff / connect / land / pound (hunted-loop escape).
                if (playerInWater && jump->busy())
                    cancelJumpAndToken(world, e, false);

                const Vector3f before = v.xf->position;
                bool           landed   = false;
                bool           splashed = false;
                const bool     hasTarget = player.valid() && playerAlive;
                jump->tickAutonomous(v.xf->position, dt, heightAt, &heightCtx, m_waterY, hasTarget ? &playerPos : nullptr, hasTarget, landed, splashed);
                if (jump->phase() == Combat::JumpAttackPhase::Connected && !landed && !splashed && hasTarget)
                    jump->applyConnectSnap(v.xf->position, playerPos, dt);
                sweepCubes(v.xf->position, before, cubes, cubeCount);

                if (splashed)
                {
                    walkableBackStep(v.xf->position, jump->velocity());
                    v.xf->position.y = terrain.heightAtWorld(v.xf->position.x, v.xf->position.z) + jump->def().groundOffset;
                    cancelJumpAndToken(world, e, false);
                }
                else if (landed)
                {
                    walkableBackStep(v.xf->position, jump->velocity());
                    v.xf->position.y = terrain.heightAtWorld(v.xf->position.x, v.xf->position.z) + jump->def().groundOffset;
                    jump->onLanded(v.xf->position);
                    if (jump->phase() == Combat::JumpAttackPhase::Pound)
                    {
                        collectJumpTargets(world);
                        Combat::DamageEvent poundEvents[8]{};
                        const int n = jump->tryPound(jumpQuery, v.xf->position, poundEvents, 8);
                        DE_LOG_INFO(LogCategory::AI, "Hunter: ground pound");
                        if (m_hunterCueFn)
                            m_hunterCueFn(m_hunterCueUser, e, "pound");
                        resolveJumpHits(world, poundEvents, n);
                    }
                }
                else if (jump->phase() == Combat::JumpAttackPhase::Leap)
                {
                    collectJumpTargets(world);
                    Combat::DamageEvent connectEv{};
                    if (jump->tryConnect(jumpQuery, v.xf->position, v.ai->forward, connectEv))
                    {
                        DE_LOG_INFO(LogCategory::AI, "Hunter: pounce connect");
                        if (m_hunterCueFn)
                            m_hunterCueFn(m_hunterCueUser, e, "pounce");
                        resolveJumpHits(world, &connectEv, 1);
                    }
                }

                if (jump->phase() == Combat::JumpAttackPhase::Telegraph)
                    v.ai->forward = flattenDir(Vector3f{ playerPos.x - v.xf->position.x, 0.0f, playerPos.z - v.xf->position.z }, v.ai->forward);
            }
            if (poise)
                poise->hyperArmor = jump && jump->inAirCommit();

            if (!(jump && jump->inAirCommit()))
                integrateHitReaction(v, dt, terrain);

            const float dx       = v.xf->position.x - playerPos.x;
            const float dz       = v.xf->position.z - playerPos.z;
            const float distSq   = dx * dx + dz * dz;
            const bool  standoff = distSq <= kStandoff * kStandoff;

            AI::SightQuery q;
            q.eye       = Vector3f{ v.xf->position.x, v.xf->position.y + 0.5f, v.xf->position.z };
            q.forward   = v.ai->forward;
            q.target    = Vector3f{ playerPos.x, playerPos.y + 0.5f, playerPos.z };
            q.coneDeg   = v.sight ? v.sight->coneDeg : 70.0f;
            q.range     = v.sight ? v.sight->range : 25.0f;
            q.heightMap = m_walk.heightMap();
            if ((!q.heightMap || !q.heightMap->valid()) && terrain.heightMap().valid())
                q.heightMap = &terrain.heightMap();
            bool sees = !playerInWater && (standoff || (q.heightMap && q.heightMap->valid() && AI::sees(q)));
            if (!sees && !playerInWater && playerAlive)
            {
                Vector3f to{ playerPos.x - v.xf->position.x, 0.0f, playerPos.z - v.xf->position.z };
                Vector3f fwd = v.ai->forward;
                fwd.y        = 0.0f;
                const float dist = to.Magnitude();
                if (dist > 1.0e-3f && dist <= q.range && fwd.MagnitudeSqrd() > 1.0e-8f)
                {
                    to *= (1.0f / dist);
                    fwd.Normalize();
                    const float halfRad = q.coneDeg * 0.5f * (3.14159265f / 180.0f);
                    if (fwd.Dot(to) >= std::cos(halfRad))
                        sees = true;
                }
            }
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
            if (v.hit->hit.stunned() || (stCc && stCc->hasHardCc()))
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

            const AI::Leaf leaf = v.brain->brain->leaf();
            const float    horiz = sqrtf(distSq);
            const bool     wantJump = (leaf == AI::Leaf::Chase || leaf == AI::Leaf::Assist) && sees && horiz >= kHunterJumpMin && horiz <= kHunterJumpMax;
            if (wantJump)
            {
                if (!jump)
                    DE_LOG_WARN(LogCategory::AI, "Hunter: jump attack wanted but JumpAttackComponent missing");
                else if (jump->phase() == Combat::JumpAttackPhase::Idle && jump->cooldownLeft() <= 0.0f && !packTokenBusy(world, e))
                {
                    Combat::JumpAttackBegin req{};
                    req.attacker          = e;
                    req.position          = v.xf->position;
                    req.lookFlat          = flattenDir(Vector3f{ playerPos.x - v.xf->position.x, 0.0f, playerPos.z - v.xf->position.z }, v.ai->forward);
                    req.intendedTarget    = player;
                    req.intendedTargetPos = playerPos;
                    if (jump->begin(req))
                    {
                        v.ai->forward = req.lookFlat;
                        v.path->path.points.clear();
                        v.path->waypoint = 0;
                        m_jumpAttackToken = e;
                        DE_LOG_INFO(LogCategory::AI, "Hunter: jump telegraph");
                        DE_LOG_INFO(LogCategory::AI, "Hunter: jump token grant");
                        if (m_hunterCueFn)
                            m_hunterCueFn(m_hunterCueUser, e, "grunt");
                    }
                }
            }

            if (jump && jump->busy())
                continue;

            const bool  sprint = leaf == AI::Leaf::Assist || leaf == AI::Leaf::Flee || v.ai->assistLeft > 0.0f;
            const float speed  = sprint ? m_pack.sprintSpeed : m_pack.walkSpeed;

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
            if (move.MagnitudeSqrd() < 1.0e-8f)
            {
                if (leaf == AI::Leaf::Chase && playerAlive)
                    seekToward(v, dt, terrain, speed, playerPos.x, playerPos.z);
                else if (leaf == AI::Leaf::Assist)
                    seekToward(v, dt, terrain, speed, v.ai->helpPos.x, v.ai->helpPos.z);
                else if (leaf == AI::Leaf::Memory && v.ai->hasLastSeen)
                    seekToward(v, dt, terrain, speed, v.ai->lastSeen.x, v.ai->lastSeen.z);
                else if (leaf == AI::Leaf::Wander && !m_walk.valid())
                {
                    Vector3f toW{ v.ai->wanderDest.x - v.xf->position.x, 0.0f, v.ai->wanderDest.z - v.xf->position.z };
                    if (toW.MagnitudeSqrd() < 1.0f)
                    {
                        v.ai->wanderDest.x = v.xf->position.x + ((static_cast<int>(m_time * 17.0f) & 1) ? 8.0f : -8.0f);
                        v.ai->wanderDest.z = v.xf->position.z + ((static_cast<int>(m_time * 13.0f) & 1) ? 6.0f : -6.0f);
                    }
                    seekToward(v, dt, terrain, speed, v.ai->wanderDest.x, v.ai->wanderDest.z);
                }
                move = Vector3f{ v.xf->position.x - before.x, 0.0f, v.xf->position.z - before.z };
            }
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
            if (JumpAttackComponent* jac = world.get<JumpAttackComponent>(e))
            {
                if (jac->jump.busy())
                    DE_LOG_INFO(LogCategory::AI, "Hunter: jump death-cancel");
            }
            cancelJumpAndToken(world, e, false);
            DE_LOG_INFO(LogCategory::AI, "Hunter down");
        }
        return hp->health.hp() < before;
    }

    void AiSystem::applyHunterHitReaction(World& world, Entity e, const Vector3f& hitDirection)
    {
        HealthComponent* hp = world.get<HealthComponent>(e);
        if (!hp || !hp->health.alive())
            return;
        if (const JumpAttackComponent* jac = world.get<JumpAttackComponent>(e))
        {
            if (jac->jump.inAirCommit())
                return;
        }
        if (HitReactionComponent* hit = world.get<HitReactionComponent>(e))
        {
            hit->hit.setSettings(m_hunterHit);
            hit->hit.apply(hitDirection);
        }
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
            beginFlee(world, v);
            DE_LOG_INFO(LogCategory::AI, "Hunter fled after seeing a kill");
        }
    }

} // namespace Dark
