#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Application.h"
#include "Network/Replication.h"
#include "Assets/AssetHandle.h"
#include "Render/Mesh.h"
#include "Render/FoliagePipeline.h"
#include "Render/GrassPipeline.h"
#include "Render/FoliagePrototypes.h"
#include "Render/EchoChirpFile.h"
#include "Render/Camera3D.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/GtaoPipeline.h"
#include "Render/AutoExposure.h"
#include "Render/TonemapPipeline.h"
#include "Render/CloudVolumePipeline.h"
#include "Render/HealthHud.h"
#include "Render/CrosshairHud.h"
#include "Sky/Environment.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Quest.h"
#include "Scene/Building.h"
#include "Scene/SceneTypes.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/FoliageCollision.h"
#include "Terrain/GrassField.h"
#include "Terrain/WindField.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainMaterial.h"
#include "Terrain/TerrainGround.h"
#include "Terrain/LeafField.h"
#include "Terrain/LeafRustle.h"
#include "Water/Water.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/PlayerStealth.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"
#include "Particles/GpuParticleSystem.h"
#include "Particles/LeafRenderer.h"
#include "Particles/BloodSplatPool.h"
#include "Weapons/Weapon.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "PathChase.h"
#include "SandboxDemo.h"
#include "SandboxFrame.h"
#include "SandboxHud.h"
#include "Media/MoviePlayer.h"
#include "Play/DialogueController.h"
#include "Play/GuardVoice.h"
#include "Play/ImpactFx.h"
#include "Play/PlayFeedback.h"
#include "Play/PlaySession.h"
#include "Play/Spawner.h"
#include "Play/Crow.h"
#include "Ui/NpcInfoOverlay.h"
#include "Save/SaveSystem.h"

namespace Dark::Combat
{
    struct DamageEvent;
}

class SandboxApp : public Dark::Application
{
public:
    explicit SandboxApp(const Dark::AppConfig& cfg);
    ~SandboxApp() override;

    void onInit() override;
    void onSplashFinished() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void registerDefaultActions();
    void populateMainMenu();
    // Terrain, pawns, and the save baseline. Called once: from onInit when the
    // menu is skipped, or from the first menu confirm. After the splash, confirm
    // returns and onUpdate advances one slice per frame so Present runs between them.
    bool startScene(const std::filesystem::path& scenePath);

    // One step of startScene, then the caller pumps. Editor and Sandbox2D do not use this.
    enum class BootSlice : uint8_t
    {
        Ibl,
        WaitScene,
        WaitTerrainCpu,
        TerrainMesh,
        WaitFoliage,
        TerrainMaterial,
        PrimeStream,
        CoarseHeight,
        WaterCpu,
        WaterGpu,
        StreamRibbon,
        PrimitiveMesh,
        SandboxModel,
        SpawnBatch,
        Done,
    };
    void prepareBoot(bool runScene, const std::filesystem::path& scenePath);
    void beginSceneAfterIbl(const std::filesystem::path& scenePath);
    bool bootSceneLoop();
    bool advanceBootSlice();
    bool advanceSpawnBatch();
    void handleRuntimeCommands(float dt);
    void handleNetHotkeys();
    void applyNetRole();
    bool uiOpen() const
    {
        return m_showDevTools || m_showInventory || m_showProfiler || m_hud.m_showJournal || m_hud.m_showUpgrades || m_hud.m_lootTarget.valid()
            || m_dialogue.choiceOpen() || m_dialogue.locksWorld();
    }
    int  giveItem(Dark::InventoryComponent& inv, const Dark::ItemStack& item);
    void stockHunter(Dark::Entity hunter);
    void tickLeaves(float dt);
    Dark::Entity spawnSpawnerUnit(Dark::Entity spawner, const Dark::SpawnerRequest& req);
    void startQuest(const std::string& id);
    void updateQuest(float dt);
    bool questTarget(Dark::Math::Vector3f& out);
    bool ownsWeapon(Dark::WeaponKind kind);
    void dropInventoryItem(int bagIndex);
    bool camouflageHides(Dark::Entity e);
    static bool skipShadowModel(void* user, Dark::Entity e);
    static void drawPrimitiveShadowCasters(void* user, ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& lightViewProj, const Dark::Frustum3f& frustum, bool recordThisView);
    void devNetHost();
    void devNetJoin(const Dark::Address& addr);
    void devNetDisconnect();
    void devNetBrowse();
    void devToggleListen();
    void updateFlyCamera(float dt);
    void updatePawnMotion(float dt);
    void syncPlayerMode();
    Dark::WeaponWorldQuery makeWeaponQuery();
    static void onWeaponHitThunk(void* user, const Dark::WeaponHit& hit);
    friend class SandboxDemo;
    friend class SandboxFrame;
    friend class SandboxHud;
    void bindPlayFeedback();
    Dark::ImpactFxHost impactHost();
    bool deferredDecals();
    bool isStepCue(Dark::Entity e, const char* cue);
    void spawnFootmark(const Dark::Math::Vector3f& bodyPos, const Dark::Math::Vector3f& facing);
    void spawnHunterFootmark(Dark::Entity hunter);
    void spawnWeaponImpact(const Dark::WeaponHit& hit);
    void spawnLivingBloodDecal(Dark::Entity victim, const Dark::Math::Vector3f& point, const Dark::Math::Vector3f& normal);
    void spawnDeathBloodDecal(const Dark::Math::Vector3f& point, const Dark::Math::Vector3f& normal);

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
    Dark::TonemapSettings playerPostFx();
    Dark::Entity possessedBody();
    void spawnOwnedPawn(Dark::ClientId owner, float offsetX);
    Dark::Entity findPawn(Dark::ClientId owner);
    void ensureLocalCube();
    void attachReplicaCombat(Dark::Entity e);
    void attachLocalPlayer(Dark::Entity e);
    void ensurePlayerStealth(Dark::Entity e);
    Dark::PlayerStealthComponent* localStealth();
    bool attachAnimatedCharacter(Dark::Entity e, const char* gltfPath);
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


    static void fillPlayHost(void* user, Dark::PlayHost& host);
    static void fillDialogueHost(void* user, Dark::DialogueHost& host);
    static void fillMovieHost(void* user, Dark::MovieHost& host);
    static void stepPlayPhysics(void* user, float dt);
    static void onPlayRespawn(void* user);
    static void onPlayQuestNote(void* user, const Dark::QuestNoteEvent& note);
    static void onPlayDefenseText(void* user, bool parried);
    static void onPlayLoot(void* user, Dark::Entity loot);
    static Dark::Entity onPlaySpawnUnit(void* user, Dark::Entity spawner, const Dark::SpawnerRequest& req);
    static bool bindListedPhysics(void* user, Dark::Entity e, bool wolf);
    static void noteListedHunter(void* user, Dark::Entity e);
    static float onPlayHeight(void* user, float x, float z);
    static Dark::Terrain::GroundContact onPlayGroundContact(void* user, float x, float z);

    static bool onNetSpawn(Dark::World& world, Dark::Entity e, Dark::NetPrefab prefab, const Dark::TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(Dark::World& world, Dark::Entity e, Dark::NetId id, void* user);
    static void onNetPeer(const Dark::NetPeerInfo& info, Dark::NetPeerEvent event, void* user);

    Dark::Entity m_camera;

    SandboxDemo  m_demo;
    SandboxFrame m_frame;
    Dark::Entity                        m_shield;
    Dark::PlaySession                   m_play;
    Dark::Entity                        m_muzzle;
    float                               m_muzzleTimer = 0.0f;
    Dark::PlayFeedback                  m_playFeedback{};
    bool                            m_demoScene    = true;
    bool                            m_sceneStarted = false;
    std::filesystem::path           m_loadedScenePath;
    Dark::StealthSceneDesc          m_sceneStealth{};
    Dark::GuardVoice                m_guardVoice;
    Dark::DialogueController        m_dialogue;
    Dark::MoviePlayer               m_movie;
    bool                            m_movieWasBlocking = false;
    std::string                     m_sceneQuest;
    void         expandSceneBuildings(Dark::SceneFileData& scene);
    // False when the boot budget is spent and fixtures remain.
    bool         spawnBuildingExtras(Dark::BuildingFixtureSpawn& spawn);
    std::vector<Dark::BuildingContainer> m_buildingContainers;
    std::vector<Dark::BuildingHideSpot>  m_buildingHides;
    std::vector<Dark::BuildingDoor>      m_buildingDoors;
    std::vector<Dark::BuildingCamera>    m_buildingCameras;
    std::deque<std::string>              m_buildingLabels;
    float                           m_goalShown    = 0.0f;
    Dark::Camera3D          m_viewCamera;
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

    Dark::Terrain::TerrainGrid m_terrain;
    Dark::FoliagePrototypes                   m_foliagePrototypes;
    Dark::FoliagePipeline                     m_foliagePipeline;
    Dark::GrassPipeline                       m_grassPipeline;
    Dark::Terrain::GrassField                 m_grassField;
    double                                    m_grassWindSeconds = 0.0;
    Dark::Terrain::WindSettings               m_wind = Dark::Terrain::defaultWindSettings();
    Dark::Terrain::WindClock                  m_windClock;
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
    Dark::Physics::PhysicsBodyId m_groundIgnore  = Dark::Physics::kNullPhysicsBody;

    Dark::Terrain::TerrainGround m_ground;
    Dark::Terrain::GroundContact groundContactAt(float x, float z) const;
    float                     m_terrainSeaLevel = 0.0f;
    bool                      m_haveTerrainSea  = false;
    // A scene with no terrain block still needs a height field for walking.
    // The mesh stays hidden so the scene's floor is the visible ground.
    bool                      m_showTerrain     = true;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::WaterWorld            m_water;
    struct SandboxStream
    {
        Dark::Mesh            mesh;
        float                 flowSpeed = 1.6f;
        Dark::Math::AABox3f   bounds;
    };
    std::vector<SandboxStream> m_streams;

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
    Dark::NpcInfoOverlaySettings m_npcInfo{};
    struct SandboxVision
    {
        bool                    camouflage    = false;
        bool                    nightSightOn  = false;
        float                   nightSight    = 0.0f;
        bool                    infraredOn    = false;
        float                   infrared      = 0.0f;
        bool                    ultravioletOn = false;
        float                   ultraviolet   = 0.0f;
        bool                    echoOn        = false;
        float                   echo          = 0.0f;
        float                   echoChirpAge  = 0.0f;
        bool                    echoCapture   = false;
        bool                    echoHaveChirp = false;
        Dark::Math::Matrix4f    echoChirpViewProj{};
        Dark::Math::Vector3f    echoChirpPos{ 0.0f, 0.0f, 0.0f };
        Dark::EchoChirpSettings echoChirp{};
    };
    SandboxVision m_vision;
    SandboxHud    m_hud;
    char          m_joinHost[64]   = "127.0.0.1";
    Dark::HealthHud                  m_healthHud;
    Dark::CrosshairHud               m_crosshair;
    Dark::ParticleEmitter            m_blood;
    Dark::ParticleRenderer           m_particles;
    Dark::GpuParticleSystem          m_gpuParticles;
    Dark::BloodSplatPool             m_bloodSplats;

    Dark::Mesh             m_crossMesh;

    Dark::Save::SaveSystem m_save;
    Dark::Entity           m_session{};

    struct SandboxBootLoad;
    // Raw pointer: the type is complete only in SandboxBoot.cpp, and the constructor lives here.
    SandboxBootLoad* m_boot = nullptr;
    BootSlice                        m_bootSlice = BootSlice::Done;
    // Menu confirm after splash teardown. The main loop Presents between slices.
    bool                             m_bootPolling = false;

    static Dark::JobStatus bootTerrainCpu(void* user);
    static Dark::JobStatus bootFoliage(void* user);
    static Dark::JobStatus bootWorldEngineDecode(void* user);
};
