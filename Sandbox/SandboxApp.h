#pragma once
#include <vector>
#include <wrl/client.h>
#include "Core/Application.h"
#include "Animation/AnimNotify.h"
#include "Network/Replication.h"
#include "Assets/Model.h"
#include "Render/DrawList.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Render/Mesh.h"
#include "Render/LinePipeline.h"
#include "Render/ModelDraw.h"
#include "Render/FoliagePipeline.h"
#include "Render/GrassPipeline.h"
#include "Render/FoliagePrototypes.h"
#include "Render/SceneRenderer.h"
#include "Render/EchoChirpFile.h"
#include "Render/Camera3D.h"
#include "Sky/Environment.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/QuestRuntime.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/FoliageCollision.h"
#include "Terrain/GrassField.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Animation/Locomotion.h"
#include "Character/PlayerStealth.h"
#include "Render/HealthHud.h"
#include "Render/CrosshairHud.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"
#include "Particles/LeafRenderer.h"
#include "Terrain/LeafField.h"
#include "Terrain/LeafRustle.h"
#include "Particles/BloodSplatPool.h"
#include "Combat/HoldCharge.h"
#include "Combat/Shield.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "Gameplay/HealthPack.h"
#include "PathChase.h"
#include "Terrain/TerrainGround.h"
#include "Play/Carry.h"
#include "Play/GuardVoice.h"
#include "Play/ImpactFx.h"
#include "Play/Spawner.h"
#include "Play/Crow.h"
#include "Play/WeaponTargets.h"
#include "Ui/HudTagComponent.h"
#include "Ui/NpcInfoOverlay.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/ThrowableFlight.h"
#include "Ui/ImGuiHost.h"
#include "Ui/MainMenu.h"
#include "Save/SaveSystem.h"

#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Dark::Combat
{
    struct DamageEvent;
}

class SandboxApp : public Dark::Application
{
public:
    using Application::Application;

    void onInit() override;
    void onSplashFinished() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void registerDefaultActions();
    void populateMainMenu();
    // Terrain, pawns, and the save baseline. Called once: from onInit when the
    // menu is skipped, or from the first menu confirm. A later confirm resumes.
    bool startScene(const std::filesystem::path& scenePath);
    void handleRuntimeCommands(float dt);
    void handleNetHotkeys();
    void applyNetRole();
    void drawDevTools();
    void drawProfiler();
    void drawInventory();
    void drawArmorHud();
    void drawPickupLabels();
    bool uiOpen() const { return m_showDevTools || m_showInventory || m_showProfiler || m_showJournal || m_lootTarget.valid(); }
    void drawLoot();
    int  giveItem(Dark::InventoryComponent& inv, const Dark::ItemStack& item);
    void stockHunter(Dark::Entity hunter);
    void tickSpawners(float dt);
    void tickCrows(float dt);
    void tickLeaves(float dt);
    Dark::Entity spawnSpawnerUnit(Dark::Entity spawner, const Dark::SpawnerRequest& req);
    void faceHitTarget(Dark::Entity target);
    bool boneWorldPos(Dark::Entity e, const char* bone, Dark::Math::Vector3f& out);
    void onPlayerDefended(Dark::Entity attacker, bool parried);
    void drawDefenseText();
    void drawTargetMarker();
    void drawGuardAwareness();
    void drawGuardSpeech();
    float playerLightLevel();
    void drawStaminaBar();
    void startQuest(const std::string& id);
    void updateQuest(float dt);
    void drawQuestHud();
    void drawJournal();
    bool questTarget(Dark::Math::Vector3f& out);
    void drawBreathBar();
    void setCircuit(Dark::Entity breaker, bool on, bool alertGuards);
    void updatePower(float dt);
    void updateGuardFlashlights();
    static void powerClick(void* user);
    static float carryHeightAt(void* user, float x, float z);
    static void carryHidden(void* user);
    void fillCarryView(Dark::CarryView& view);
    Dark::Entity takedownTarget();
    Dark::Entity bodyNear();
    bool tryTakedown();
    bool toggleCarry(Dark::Entity target = {});
    void updateCarry();
    void updateHunterLoot();
    bool ownsWeapon(Dark::WeaponKind kind);
    void pickUpNearestItem();
    void dropInventoryItem(int bagIndex);
    void applyInventoryArmor();
    void drawPauseOverlay();
    void drawNpcInfoOverlay();
    bool camouflageHides(Dark::Entity e);
    void devNetHost();
    void devNetJoin(const Dark::Address& addr);
    void devNetDisconnect();
    void devNetBrowse();
    void devToggleListen();
    void updateFlyCamera(float dt);
    void updatePawnMotion(float dt);
    void updatePossessed(float dt);
    void updateCombat(float dt);
    void syncPlayerMode();
    void handleWeaponSwitch();
    Dark::WeaponWorldQuery makeWeaponQuery();
    static void onWeaponHitThunk(void* user, const Dark::WeaponHit& hit);
    void onWeaponHit(const Dark::WeaponHit& hit);
    void drawProjectiles(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, Dark::MeshFrameConstants& cb, bool instanced = false, const std::vector<int>* rejectedShots = nullptr);
    void drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj, bool instanced = false, const std::vector<int>* rejectedShots = nullptr);
    bool createSandboxModels();
    void spawnGltfDemo();
    void spawnAnimatedDemo();
    void updateWiggleAnim();
    static void onWiggleNotify(void* user, const Dark::AnimNotify& n);
    void spawnHybridLocalLights();
    void pulseMuzzle();
    void spawnHunterBlood(const Dark::Math::Vector3f& pos);
    Dark::ImpactFxHost impactHost();
    bool deferredDecals();
    bool isStepCue(Dark::Entity e, const char* cue);
    void spawnFootmark(const Dark::Math::Vector3f& bodyPos, const Dark::Math::Vector3f& facing);
    void spawnHunterFootmark(Dark::Entity hunter);
    void spawnWeaponImpact(const Dark::WeaponHit& hit);
    void spawnLivingBloodDecal(Dark::Entity victim, const Dark::Math::Vector3f& point, const Dark::Math::Vector3f& normal);
    void spawnDeathBloodDecal(const Dark::Math::Vector3f& point, const Dark::Math::Vector3f& normal);
    void respawnPlayer();
    void resolveJumpAttackAndFx(const Dark::Combat::DamageEvent* events, int count);
    bool firePossessedLoadout(bool charged);
    bool releaseThrowable(bool charged);
    void tickThrowableFlights(float dt);
    void placeHealthPacks();
    void installSaveHost();
    void ensureSessionEntity();
    void ensurePlayerProgress(Dark::Entity e);
    void pullPhysicsVelocities();
    void pushPhysicsProgress();
    static bool saveCanSave(void* user, Dark::Save::SaveResult& why);
    static void saveBeginLoad(void* user, Dark::World& world);
    static void saveEndLoad(void* user, Dark::World& world);
    static Dark::Entity saveRespawn(void* user, Dark::World& world, std::string_view archetype, const Dark::Save::SavePose& pose);
    static void saveDestroy(void* user, Dark::World& world, Dark::Entity e);
    static bool saveSkip(void* user, Dark::World& world, Dark::Entity e);
    static int saveNetRole(void* user);
    static uint32_t savePeerCount(void* user);
    static void saveBeforeCapture(void* user, Dark::World& world);
    static void saveAfterApply(void* user, Dark::World& world);
    void updateHealthPacks(float dt);
    void updateShoulderCamera();
    Dark::TonemapSettings playerPostFx();
    Dark::Entity possessedBody();
    void spawnOwnedPawn(Dark::ClientId owner, float offsetX);
    Dark::Entity findPawn(Dark::ClientId owner);
    void ensureLocalCube();
    void attachReplicaCombat(Dark::Entity e);
    void attachLocalPlayer(Dark::Entity e);
    bool attachAnimatedCharacter(Dark::Entity e, const char* gltfPath);
    void updateCharacterAnims(float dt);
    Dark::Health*         localHealth();
    Dark::HitReaction*    localHit();
    Dark::PlayerMotor*    localMotor();
    Dark::WeaponLoadout*  localWeapons();
    void syncTerrainLod();
    void createPhysicsWorld();
    void addChaseObstaclesToPhysics();
    void bindEntityPhysics(Dark::Entity e, const char* entityType);
    void bindCharacterPhysics();
    float playerGroundHeight(float x, float z) const;
    void drawDebugOverlays(ID3D12GraphicsCommandList* cmd);
    void drawSkeletonOverlay(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj);
    bool createSkeletonLineBuffers();

    static bool onNetSpawn(Dark::World& world, Dark::Entity e, Dark::NetPrefab prefab, const Dark::TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(Dark::World& world, Dark::Entity e, Dark::NetId id, void* user);
    static void onNetPeer(const Dark::NetPeerInfo& info, Dark::NetPeerEvent event, void* user);

    Dark::Entity m_camera;
    Dark::Entity m_cube;
    Dark::Entity m_wiggle;

    Dark::Mesh    m_cubeMesh;
    Dark::DrawList m_drawList;
    Dark::SkinnedInstanceBuffer m_skinnedInstances;
    Dark::SceneRenderer     m_scene;
    Dark::LinePipeline        m_skelLinePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineVb[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineIb[2];
    D3D12_VERTEX_BUFFER_VIEW  m_skelLineVbv[2]{};
    D3D12_INDEX_BUFFER_VIEW   m_skelLineIbv[2]{};
    Dark::Entity                    m_shield;
    Dark::Combat::OffhandState         m_offhand{};
    Dark::Combat::PlayerChargeSettings m_chargeSettings{};
    Dark::PlayerStealthSettings        m_stealth{};
    Dark::Combat::HoldCharge           m_attackCharge{};
    Dark::Combat::HoldCharge           m_blockCharge{};
    Dark::ChargeWindup                 m_chargeWindup{};
    bool                               m_fireQuick    = false;
    bool                               m_fireCharged  = false;
    Dark::ThrowableFlights             m_throwFlights;
    Dark::Entity                    m_muzzle;
    float                           m_muzzleTimer = 0.0f;
    float                           m_hitStop      = 0.0f;
    float                           m_shake        = 0.0f;
    float                           m_shakeTime    = 0.0f;
    bool                            m_swingCharged = false;
    float                           m_swingLock    = 0.0f;
    float                           m_comboWindow  = 0.0f;
    int                             m_comboStep    = 0;
    int                             m_comboCount   = 0;
    float                           m_faceYaw      = 0.0f;
    float                           m_faceLeft     = 0.0f;
    float                           m_blockRecoil  = 0.0f;
    bool                            m_demoScene    = true;
    bool                            m_sceneStarted = false;
    std::filesystem::path           m_loadedScenePath;
    float                           m_lightLevel   = 1.0f;
    float                           m_exposure     = 1.0f;
    float                           m_strideLeft   = 0.0f;
    enum class InteractKind
    {
        None,
        Body,
        Power,
        Item
    };
    struct InteractPick
    {
        InteractKind kind = InteractKind::None;
        Dark::Entity e{};
    };
    InteractPick pickInteract();
    Dark::QuestRuntime              m_quest;
    Dark::GuardVoice                m_guardVoice;
    std::string                     m_sceneQuest;
    struct QuestBanner
    {
        std::string label;
        std::string text;
        uint32_t    color    = 0xFFFFFFFFu;
        float       timeLeft = 3.5f;
    };
    std::vector<QuestBanner>        m_questBanners;
    bool                            m_showJournal = false;
    float                           m_goalShown    = 0.0f;
    float                           m_shieldBump   = 0.0f;
    const char*                     m_defenseText  = "";
    float                           m_defenseTextLeft = 0.0f;
    Dark::Camera3D          m_viewCamera;
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f> m_prevWorldByEntity;
    Dark::Sky::Environment  m_env;
    Dark::IblSettings       m_ibl;
    Dark::GtaoSettings      m_ssao;
    Dark::SsrSettings       m_ssr;
    Dark::CloudLodSettings     m_cloudLod{};
    Dark::AutoExposureSettings m_autoExposure{};
    Dark::AutoExposureState    m_autoExposureState{};
    Dark::AutoExposureResult   m_autoExposureResult{};
    float                      m_frameDt = 0.0f;
    Dark::AssetID           m_iblImageId = Dark::NULL_ASSET;

    Dark::PathChase                m_chase;
    bool                           m_chaseOk = false;
    Dark::CrowModels               m_crowModels{};
    Dark::WeaponTargetList m_weaponTargets;

    Dark::Terrain::TerrainGrid m_terrain;
    Dark::FoliagePrototypes                   m_foliagePrototypes;
    Dark::FoliagePipeline                     m_foliagePipeline;
    Dark::GrassPipeline                       m_grassPipeline;
    Dark::Terrain::GrassField                 m_grassField;
    double                                    m_grassWindSeconds = 0.0;
    Dark::Terrain::LeafField                  m_leafField;
    Dark::Terrain::LeafRustleVoices           m_leafRustle;
    Dark::LeafRenderer                        m_leafRenderer;
    float                                     m_leafShedScale  = 1.0f;
    double                                    m_leafSimSeconds = 0.0;
    bool                                      m_loggedForwardGrassSkip = false;
    Dark::Terrain::FoliageDensity             m_foliageDensity{};
    std::vector<Dark::Terrain::FoliageRecord> m_foliage;

    Dark::Physics::PhysicsWorld  m_physics;
    Dark::Physics::PhysicsBodyId m_physicsGround = Dark::Physics::kNullPhysicsBody;
    Dark::Physics::FoliageCollisionSet m_foliageCollision;
    float                        m_groundProbeY  = 0.0f;
    Dark::Physics::PhysicsBodyId m_groundIgnore  = Dark::Physics::kNullPhysicsBody;

    Dark::Terrain::TerrainGround m_ground;
    Dark::Terrain::GroundContact groundContactAt(float x, float z) const;
    float                     m_terrainSeaLevel = 0.0f;
    bool                      m_haveTerrainSea  = false;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::WaterWorld            m_water;
    struct SandboxStream
    {
        Dark::Mesh            mesh;
        float                 flowSpeed = 1.6f;
        Dark::Math::AABox3f   bounds;
    };
    std::vector<SandboxStream> m_streams;

    Dark::AssetID m_cubeModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_packModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_lanternModelId = Dark::NULL_ASSET;
    Dark::AssetID m_tracerModelId  = Dark::NULL_ASSET;
    Dark::NetRole m_netRole        = Dark::NetRole::Idle;
    uint32_t      m_browseLogCount = ~0u;
    bool          m_netBrowsing    = false;
    bool          m_gameplayPaused = false;
    bool          m_godMode        = false;
    bool          m_reaperMode     = false;
    bool          m_stepGameplay   = false;
    float         m_flySpeed       = 18.0f;
    float         m_spinSpeed      = 0.8f;
    float         m_cloudTime      = 0.0f;
    bool          m_showShadowMaps = false;
    bool          m_showDepth      = false;
    bool          m_showGBuffer    = false;
    bool          m_showVelocity   = false;
    bool          m_showSkeleton   = false;
    bool          m_showDevTools   = false;
    bool          m_showInventory  = false;
    bool          m_showProfiler   = false;
    Dark::Entity  m_lootTarget{};
    const Dark::ItemDef* m_wornArmor[static_cast<int>(Dark::EquipSlot::Count)]{};
    Dark::NpcInfoOverlaySettings m_npcInfo{};
    bool          m_camouflage     = false;
    bool          m_nightSightOn   = false;
    float         m_nightSight     = 0.0f;
    bool          m_infraredOn     = false;
    float         m_infrared       = 0.0f;
    bool          m_ultravioletOn  = false;
    float         m_ultraviolet    = 0.0f;
    bool          m_echoOn         = false;
    float         m_echo           = 0.0f;
    float         m_echoChirpAge   = 0.0f;
    bool          m_echoCapture    = false;
    bool          m_echoHaveChirp  = false;
    Dark::Math::Matrix4f m_echoChirpViewProj{};
    Dark::Math::Vector3f m_echoChirpPos{ 0.0f, 0.0f, 0.0f };
    Dark::EchoChirpSettings m_echoChirp{};
    ImGuiHost     m_imgui;
    Dark::MainMenu m_menu;
    char          m_joinHost[64]   = "127.0.0.1";
    float         m_lookYaw        = 0.0f;
    float         m_lookPitch      = 0.18f;
    float         m_lowerBodyYaw   = 0.0f;
    bool          m_playerWet       = false;
    bool          m_playerSubmerged  = false;
    bool          m_swimDiveEdge     = false;
    bool          m_swimStandEdge    = false;
    float         m_underAmount      = 0.0f;
    float         m_footstepAcc    = 0.0f;
    Dark::HealthHud                  m_healthHud;
    Dark::Math::Vector3f             m_playerSpawn{ 0.0f, 0.5f, 0.0f };
    bool                             m_havePlayerSpawn = false;
    float                            m_playerDeadTimer = 0.0f;
    float                            m_spawnAge        = 0.0f;
    float                            m_hurtSoundTimer    = 0.0f;
    float                            m_jumpAttackBuffer  = 0.0f;
    Dark::CrosshairHud               m_crosshair;
    Dark::ParticleEmitter            m_blood;
    Dark::ParticleRenderer           m_particles;
    Dark::BloodSplatPool             m_bloodSplats;

    Dark::Mesh             m_crossMesh;

    Dark::Save::SaveSystem m_save;
    Dark::Entity           m_session{};
    bool                   m_crouchLatch = false;
};
