#pragma once

#include "Animation/Locomotion.h"
#include "Collision/HitResult.h"
#include "Combat/CombatSystem.h"
#include "Combat/HoldCharge.h"
#include "Combat/Shield.h"
#include "ECS/Entity.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/QuestRuntime.h"
#include "Play/Facility.h"
#include "Play/ImpactFx.h"
#include "Play/Interaction.h"
#include "Play/PlayerCombat.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsIds.h"
#include "Play/WeaponTargets.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/ThrowableFlight.h"
#include "Weapons/Weapon.h"

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class AiSystem;
    class AssetManager;
    class AssetPinTable;
    class Camera3D;
    class GuardVoice;
    class Input;
    class Renderer;
    class SceneRenderer;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }
    namespace Physics
    {
        class PhysicsWorld;
        struct FoliageCollisionSet;
    }
    namespace Terrain
    {
        class TerrainGrid;
        struct FoliageRecord;
    }

    struct CrowModels;
    struct SpawnerRequest;

    inline constexpr float kPlayDeathSeconds      = 2.5f;
    inline constexpr float kPlaySpawnFocusSeconds = 1.75f;
    inline constexpr float kPlayJumpAttackBuffer  = 0.12f;

    // Pawn, combat, and quest state for one play session.
    // Sandbox aliases these fields for the process lifetime, so resetPawn writes them in place.
    struct PlayRuntime
    {
        float lookYaw      = 0.0f;
        float lookPitch    = 0.18f;
        float lowerBodyYaw = 0.0f;
        bool  crouchLatch  = false;

        float footstepAcc      = 0.0f;
        float jumpAttackBuffer = 0.0f;
        float swingLock        = 0.0f;
        float comboWindow      = 0.0f;
        float blockRecoil      = 0.0f;
        float faceYaw          = 0.0f;
        float faceLeft         = 0.0f;
        float shieldBump       = 0.0f;
        int   comboStep        = 0;
        int   comboCount       = 0;
        bool  swingCharged     = false;
        bool  fireQuick        = false;
        bool  fireCharged      = false;

        bool  wet           = false;
        bool  submerged     = false;
        bool  swimDiveEdge  = false;
        bool  swimStandEdge = false;
        float deadTimer     = 0.0f;
        float spawnAge      = 0.0f;
        float hurtTimer     = 0.0f;
        float shake         = 0.0f;
        float shakeTime     = 0.0f;
        float hitStop       = 0.0f;
        float underAmount   = 0.0f;
        float groundProbeY  = 0.0f;

        bool           haveSpawn = false;
        Math::Vector3f spawn{ 0.0f, 0.5f, 0.0f };

        Combat::OffhandState         offhand{};
        Combat::HoldCharge           attackCharge{};
        Combat::HoldCharge           blockCharge{};
        Combat::PlayerChargeSettings chargeSettings{};
        ChargeWindup                 chargeWindup{};
        ThrowableFlights             throws{};
        WeaponTargetList             targets{};
        QuestRuntime                 quest{};
        const ItemDef*               wornArmor[static_cast<int>(EquipSlot::Count)]{};
    };

    // Filled by the app at the start of each session call. Pointers are not owned.
    struct PlayHost
    {
        World*                         world     = nullptr;
        Renderer*                      renderer  = nullptr;
        SceneRenderer*                 scene     = nullptr;
        AssetManager*                  assets    = nullptr;
        AssetPinTable*                 pins      = nullptr;
        Audio::AudioSystem*            audio     = nullptr;
        Physics::PhysicsWorld*         physics   = nullptr;
        AiSystem*                      ai        = nullptr;
        Camera3D*                      camera    = nullptr;
        Input*                         input     = nullptr;
        Terrain::TerrainGrid*          terrain   = nullptr;
        const Terrain::TerrainGround*  ground    = nullptr;
        Physics::PhysicsBodyId         groundBody{};
        GuardVoice*                    guardVoice = nullptr;
        CrowModels*                    crows     = nullptr;
        Physics::FoliageCollisionSet*  foliageCollision = nullptr;
        const std::vector<Terrain::FoliageRecord>* foliage = nullptr;

        Entity player{};
        Entity shield{};
        Entity cameraEntity{};
        Entity muzzle{};
        Entity session{};

        float          waterLevel    = -1.0e9f;
        float          sunElevation  = 0.0f;
        Math::Vector3f sunDir{ 0.0f, 1.0f, 0.0f };
        float          cloudTime     = 0.0f;

        float*                    groundProbeY = nullptr;
        Physics::PhysicsBodyId*  groundIgnore  = nullptr;
        bool                      camouflage   = false;
        bool                      frozen       = false; // hidden in a locker: no movement input

        // applyMouseLook gates the mouse. allowMouseButtons gates attack clicks.
        // uiMouse gates the shield mouse button the way Sandbox does (ImGui capture, not every open panel).
        bool uiKeys            = false;
        bool applyMouseLook    = false;
        bool uiMouse           = false;
        bool allowMouseButtons = false;
        bool chaseReady        = false;

        const Math::AABox3f*  cubes            = nullptr;
        int                   cubeCount        = 0;
        const Math::Sphere3f* spheres          = nullptr;
        int                   sphereCount      = 0;
        const Entity*         cubeEntities     = nullptr;
        int                   cubeEntityCount  = 0;
        const Entity*         sphereEntities   = nullptr;
        int                   sphereEntityCount = 0;

        void (*preparePlayer)(void* user)                                  = nullptr;
        void (*stepPhysics)(void* user, float dt)                          = nullptr;
        void (*onMuzzle)(void* user)                                       = nullptr;
        void (*onBloodBurst)(void* user, const Math::Vector3f& pos)        = nullptr;
        void (*onBloodSplat)(void* user, float x, float z)                 = nullptr;
        void (*onRespawn)(void* user)                                      = nullptr;
        void (*onQuestNote)(void* user, const QuestNoteEvent& note)        = nullptr;
        void (*onDefenseText)(void* user, bool parried)                    = nullptr;
        void (*onLoot)(void* user, Entity loot)                            = nullptr;
        // Runs after facility use (doors and hiding). Returning true consumes the press.
        bool (*onUse)(void* user, const PlayInteractPick& pick)            = nullptr;
        Entity (*spawnUnit)(void* user, Entity spawner, const SpawnerRequest& req) = nullptr;
        float (*heightAt)(void* user, float x, float z)                    = nullptr;
        Terrain::GroundContact (*groundContact)(void* user, float x, float z) = nullptr;
        void* user = nullptr;
    };

    // Shared Sandbox / Editor play tick. Does not own the world, and is never relocated.
    class PlaySession
    {
    public:
        PlaySession() = default;
        PlaySession(const PlaySession&) = delete;
        PlaySession& operator=(const PlaySession&) = delete;
        PlaySession(PlaySession&&) = delete;
        PlaySession& operator=(PlaySession&&) = delete;

        void bind(void (*fill)(void* user, PlayHost& host), void* user);

        void resetPawn();
        void startQuest(std::string_view id);

        PlayRuntime&        runtime() { return m_rt; }
        const PlayRuntime&  runtime() const { return m_rt; }
        QuestRuntime&       quest() { return m_rt.quest; }
        Combat::CombatSystem& combat() { return m_combat; }
        ThrowableFlights&   throws() { return m_rt.throws; }
        WeaponTargetList&   targets() { return m_rt.targets; }

        // Shake decay, then hit-stop. Returns the dt the rest of the play frame should use.
        float scaleDt(float dt);

        void tickPawn(float dt);
        void tickCamera();
        void tickWorld(float dt);
        void tickHeldItems();

        void onWeaponHit(const WeaponHit& hit);
        void switchWeapons();
        void stockHunter(Entity hunter);

        FacilityState&       facility() { return m_facility; }
        const FacilityState& facility() const { return m_facility; }
        void clearFacility();
        void refreshUpgrades();

        PlayInteractPick pickInteract();
        Entity           takedownTarget();
        WeaponWorldQuery weaponQuery();

    private:
        void pull();
        ImpactFxHost impact();
        bool boneWorld(Entity e, const char* bone, Math::Vector3f& out) const;
        void respawn();
        void tickCombat(float dt);
        void tryPickup();
        bool tryTakedown();
        void tickCarry();
        void applyArmor();
        void lootHunters();
        void lightAndExposure(float dt, const Math::AABox3f* cubes, int cubeCount);
        void characterAnims(float dt);
        void presentQuestNotes();
        float terrainHeight(float x, float z) const;
        PlayerCombatHooks combatHooks();

        static void onJumpHits(void* user, const Combat::DamageEvent* events, int count);
        static void onRefreshCamera(void* user);
        static void onHunterCue(void* user, Entity hunter, const char* cue);
        static void onPowerClick(void* user);
        static float onCarryHeight(void* user, float x, float z);
        static void onCarryHidden(void* user);
        static Collision::RayHit3D onTerrainRay(void* user, const Math::Ray3f& ray, float maxDistance);
        static float onTerrainHeight(void* user, float x, float z);

        void (*m_fill)(void* user, PlayHost& host) = nullptr;
        void*        m_user = nullptr;
        PlayHost     m_host{};
        PlayRuntime  m_rt{};
        FacilityState m_facility{};
        Combat::CombatSystem m_combat{};
    };

    int grantInventoryItem(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity listener, InventoryComponent& inv, const ItemStack& item);

} // namespace Dark
