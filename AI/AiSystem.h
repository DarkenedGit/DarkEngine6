#pragma once

#include "AI/AiComponents.h"
#include "AI/AttackPattern.h"
#include "AI/Pathfinder.h"
#include "AI/Walkability.h"
#include "Assets/AssetHandle.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerStealth.h"
#include "Character/HitReaction.h"
#include "Combat/DamageEvent.h"
#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Terrain/TerrainGround.h"

#include <vector>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class World;

    namespace Terrain
    {
        class TerrainGrid;
    }

    struct SkillComponent;

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

        void setPreySense(const PreySense& sense) { m_prey = sense; }
        void setAwarenessEnabled(bool on) { m_awarenessOn = on; }
        bool awarenessEnabled() const { return m_awarenessOn; }
        void setPlayerExposure(float exposure) { m_playerExposure = exposure; }
        void setConcealVolumes(std::vector<Math::AABox3f> volumes) { m_conceal = std::move(volumes); }
        void setPlayerMotion(float motion) { m_playerMotion = motion; }
        void addNoise(const Math::Vector3f& pos, float range)
        {
            if (m_noises.size() < 32)
                m_noises.push_back(Noise{ pos, range });
        }
        void forceAlert(World& world, Entity e, const Math::Vector3f& attackerPos);
        const PreySense& preySense() const { return m_prey; }

        void tickHunters(World& world, Terrain::TerrainGrid& terrain, bool playerInWater, float dt, Entity player, const Math::AABox3f* cubes = nullptr, int cubeCount = 0, const Math::Sphere3f* spheres = nullptr, int sphereCount = 0);

        // Non-owning. Used for the next tickHunters calls. Null splat leaves NPCs at full speed.
        void setGroundSurface(const Terrain::TerrainGround* ground, const Terrain::HeightMap* height, const Terrain::SplatMap* splat);

        Entity jumpAttackToken() const { return m_jumpAttackToken; }
        void   setJumpAttackToken(Entity e) { m_jumpAttackToken = e; }
        float  simTime() const { return m_time; }
        void   setSimTime(float seconds) { m_time = seconds; }
        float  packAttackGap() const { return m_packAttackGap; }
        void   setPackAttackGap(float seconds) { m_packAttackGap = seconds; }
        void   setJumpAttackHits(void (*fn)(void* user, const Combat::DamageEvent* events, int count), void* user);
        void   setHunterCue(void (*fn)(void* user, Entity hunter, const char* cue), void* user);

        bool applyHunterDamage(World& world, Entity e, float amount);
        void applyHunterHitReaction(World& world, Entity e, const Math::Vector3f& hitDirection);
        enum class MeleeResponse : uint8_t
        {
            Stagger,
            Poise,
            Blocked
        };
        // How a hunter takes a sword swing from `attackerPos`: blocks it, shrugs it off, or staggers.
        MeleeResponse respondToMelee(World& world, Entity e, const Math::Vector3f& attackerPos);
        // Cancels a melee wind-up and holds the next attack back for `recover` seconds.
        void interruptAttack(World& world, Entity e, float recover);
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
            SkillComponent*        skill  = nullptr;
        };

        bool bind(World& world, Entity e, View& v);
        void collectHunters(World& world);
        void tickHealthAndRespawn(World& world, float dt);
        void integrateHitReaction(View& v, float dt, Terrain::TerrainGrid& terrain);
        void follow(View& v, float dt, Terrain::TerrainGrid& terrain, float speed);
        void repath(World& world, View& v, float destX, float destZ);
        bool pickWanderDest(View& v);
        bool pickFleeDest(View& v, const Math::Vector3f& playerPos);
        bool hunterSeesPoint(const View& v, const Math::Vector3f& worldPos) const;
        void beginAssist(View& v, const Math::Vector3f& helpPos);
        void beginFlee(World& world, View& v);
        void seekToward(View& v, float dt, Terrain::TerrainGrid& terrain, float speed, float destX, float destZ);
        void cancelJumpAndToken(World& world, Entity e, bool forceIdle);
        void fallDeadAgent(World& world, View& v, float dt, Terrain::TerrainGrid& terrain, const Math::AABox3f* cubes, int cubeCount, const Math::Sphere3f* spheres, int sphereCount);
        void ensureAttackPatterns();
        const AI::AttackPattern* attackPatternFor(World& world, Entity e) const;
        void releaseMelee(World& world, View& v, const AI::AttackPattern& pattern, Entity player, const Math::Vector3f& playerPos);
        void commitAttack(AiAgentComponent& ai, const AI::AttackPattern& pattern, int attackIndex, const Math::Vector3f& selfPos, const Math::Vector3f& playerPos);
        void collectJumpTargets(World& world);
        void resolveJumpHits(World& world, const Combat::DamageEvent* events, int count);
        void walkableBackStep(Math::Vector3f& position, const Math::Vector3f& incomingXZ) const;
        bool packTokenBusy(World& world, Entity self) const;
        bool entityIsWolf(World& world, Entity e) const;
        void guardMove(World& world, View& v, float dt, Terrain::TerrainGrid& terrain, float speed, const char* stepCue);
        int  wolfPackNear(World& world, Entity self) const;
        Math::Vector3f nudgeWolfPoint(const Math::Vector3f& islandAt, const Math::Vector3f& origin, const Math::Vector3f& ideal) const;
        void updateWolfApproach(World& world, View& v, AI::Leaf leaf, bool sees, bool playerAlive, bool standoff, const Math::Vector3f& playerPos, const Math::Vector3f& playerForward, float horiz, float dt);
        bool wolfApproachDest(const View& v, const Math::Vector3f& playerPos, const Math::Vector3f& playerForward, float& destX, float& destZ) const;

        struct JumpTargetScratch
        {
            Entity         e{};
            Math::Vector3f center{};
            bool           alive = false;
        };

        AI::Walkability     m_walk;
        AI::Pathfinder      m_finder;
        AI::PackSettings    m_pack{};
        PreySense           m_prey{};
        HitReactionSettings m_hunterHit{};
        float               m_time   = 0.0f;
        float               m_agentR = 0.8f;
        float               m_waterY = -1.0e9f;
        Entity              m_jumpAttackToken{};
        Entity              m_duelToken{};
        bool                m_awarenessOn = false;
        float               m_playerExposure = 1.0f;
        float               m_playerMotion   = 0.0f;
        struct Noise
        {
            Math::Vector3f pos;
            float          range;
        };
        std::vector<Noise>  m_noises;
        std::vector<Math::AABox3f> m_conceal;
        AI::AwarenessSettings m_aware{};
        void (*m_jumpHitsFn)(void* user, const Combat::DamageEvent* events, int count) = nullptr;
        void* m_jumpHitsUser                                                           = nullptr;
        void (*m_hunterCueFn)(void* user, Entity hunter, const char* cue)              = nullptr;
        const Terrain::TerrainGround* m_ground       = nullptr;
        const Terrain::HeightMap*     m_groundHeight = nullptr;
        const Terrain::SplatMap*      m_groundSplat  = nullptr;
        AI::AttackPattern              m_hunterAttacks{};
        AI::AttackPattern              m_wolfAttacks{};
        bool                           m_attacksLoaded = false;
        float                          m_packAttackGap = 0.0f;
        void*               m_hunterCueUser                                            = nullptr;
        std::vector<Entity> m_scratch;
        std::vector<JumpTargetScratch> m_jumpTargets;
    };

} // namespace Dark
