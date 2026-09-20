#pragma once

#include "AI/AiComponents.h"
#include "AI/Pathfinder.h"
#include "AI/Walkability.h"
#include "Assets/AssetHandle.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Combat/DamageEvent.h"
#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"

#include <vector>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class World;

    namespace Terrain
    {
        class TerrainWorld;
    }

    class AiSystem
    {
    public:
        AiSystem() = default;

        bool bake(const AI::WalkabilityDesc& desc);

        AI::Walkability&       walkability() { return m_walk; }
        const AI::Walkability& walkability() const { return m_walk; }
        AI::Pathfinder&        pathfinder() { return m_finder; }

        void                       setPackSettings(const AI::PackSettings& settings) { m_pack = settings; }
        const AI::PackSettings&    packSettings() const { return m_pack; }
        void                       setHunterHitReaction(World& world, const HitReactionSettings& settings);
        const HitReactionSettings& hunterHitReaction() const { return m_hunterHit; }
        void                       setHunterHitReactionSettings(const HitReactionSettings& settings) { m_hunterHit = settings; }

        Entity spawnHunter(World& world, AssetPinTable& pins, AssetManager& assets, const TransformComponent& xf);
        bool   attachHunter(World& world, Entity e, AssetPinTable& pins, AssetManager& assets);

        void tickHunters(World& world, Terrain::TerrainWorld& terrain, bool playerInWater, float dt, Entity player, const Math::AABox3f* cubes = nullptr, int cubeCount = 0);

        Entity jumpAttackToken() const { return m_jumpAttackToken; }
        void   setJumpAttackHits(void (*fn)(void* user, const Combat::DamageEvent* events, int count), void* user);
        void   setHunterCue(void (*fn)(void* user, Entity hunter, const char* cue), void* user);

        bool applyHunterDamage(World& world, Entity e, float amount);
        void applyHunterHitReaction(World& world, Entity e, const Math::Vector3f& hitDirection);
        void onHunterAttacked(World& world, Entity victim, const Math::Vector3f& playerPos);
        void onHunterKilled(World& world, Entity victim);

    private:
        struct View
        {
            Entity                 e{};
            TransformComponent*    xf     = nullptr;
            AiAgentComponent*      ai     = nullptr;
            PathAgentComponent*    path   = nullptr;
            HealthComponent*       health = nullptr;
            HitReactionComponent*  hit    = nullptr;
            BrainComponent*        brain  = nullptr;
            SightComponent*        sight  = nullptr;
        };

        bool bind(World& world, Entity e, View& v);
        void collectHunters(World& world);
        void tickHealthAndRespawn(World& world, float dt);
        void integrateHitReaction(View& v, float dt, Terrain::TerrainWorld& terrain);
        void follow(View& v, float dt, Terrain::TerrainWorld& terrain, float speed);
        void repath(World& world, View& v, float destX, float destZ);
        bool pickWanderDest(View& v);
        bool pickFleeDest(View& v, const Math::Vector3f& playerPos);
        bool hunterSeesPoint(const View& v, const Math::Vector3f& worldPos) const;
        void beginAssist(View& v, const Math::Vector3f& helpPos);
        void beginFlee(World& world, View& v);
        void seekToward(View& v, float dt, Terrain::TerrainWorld& terrain, float speed, float destX, float destZ);
        void cancelJumpAndToken(World& world, Entity e, bool forceIdle);
        void collectJumpTargets(World& world);
        void resolveJumpHits(World& world, const Combat::DamageEvent* events, int count);
        void walkableBackStep(Math::Vector3f& position, const Math::Vector3f& incomingXZ) const;
        bool packTokenBusy(World& world, Entity self) const;

        struct JumpTargetScratch
        {
            Entity         e{};
            Math::Vector3f center{};
            bool           alive = false;
        };

        AI::Walkability     m_walk;
        AI::Pathfinder      m_finder;
        AI::PackSettings    m_pack{};
        HitReactionSettings m_hunterHit{};
        float               m_time   = 0.0f;
        float               m_agentR = 0.8f;
        float               m_waterY = -1.0e9f;
        Entity              m_jumpAttackToken{};
        void (*m_jumpHitsFn)(void* user, const Combat::DamageEvent* events, int count) = nullptr;
        void* m_jumpHitsUser                                                           = nullptr;
        void (*m_hunterCueFn)(void* user, Entity hunter, const char* cue)              = nullptr;
        void*               m_hunterCueUser                                            = nullptr;
        std::vector<Entity> m_scratch;
        std::vector<JumpTargetScratch> m_jumpTargets;
    };

} // namespace Dark
