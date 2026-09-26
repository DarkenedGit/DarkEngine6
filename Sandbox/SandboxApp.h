#pragma once
#include <wrl/client.h>
#include "Core/Application.h"
#include "Animation/AnimNotify.h"
#include "Network/Replication.h"
#include "Assets/Model.h"
#include "Render/Mesh.h"
#include "Render/LinePipeline.h"
#include "Render/ModelDraw.h"
#include "Render/SceneRenderer.h"
#include "Render/Camera3D.h"
#include "Sky/Environment.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Render/HealthHud.h"
#include "Render/CrosshairHud.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"
#include "Particles/BloodSplatPool.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "Gameplay/HealthPack.h"
#include "PathChase.h"
#include "Ui/HudTagComponent.h"
#include "Weapons/HittableComponent.h"
#include "Ui/ImGuiHost.h"
#include "Ui/MainMenu.h"

#include <unordered_map>
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
    void handleRuntimeCommands(float dt);
    void handleNetHotkeys();
    void applyNetRole();
    void drawDevTools();
    void drawPauseOverlay();
    void devNetHost();
    void devNetJoin(const Dark::Address& addr);
    void devNetDisconnect();
    void devNetBrowse();
    void devToggleListen();
    void updateFlyCamera(float dt);
    void updatePawnMotion(float dt);
    void updatePossessed(float dt);
    void updateCombat(float dt);
    void handleWeaponSwitch();
    Dark::WeaponWorldQuery makeWeaponQuery();
    static void onWeaponHitThunk(void* user, const Dark::WeaponHit& hit);
    void onWeaponHit(const Dark::WeaponHit& hit);
    void drawProjectiles(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, Dark::MeshFrameConstants& cb);
    void drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj);
    bool createSandboxModels();
    void spawnGltfDemo();
    void spawnAnimatedDemo();
    void updateWiggleAnim();
    static void onWiggleNotify(void* user, const Dark::AnimNotify& n);
    void spawnHybridLocalLights();
    void updateFlashlight();
    void pulseMuzzle();
    void spawnHunterBlood(const Dark::Math::Vector3f& pos);
    void respawnPlayer();
    void resolveJumpAttackAndFx(const Dark::Combat::DamageEvent* events, int count);
    bool firePossessedLoadout();
    void placeHealthPacks();
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
    Dark::SceneRenderer     m_scene;
    Dark::LinePipeline        m_skelLinePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineVb[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineIb[2];
    D3D12_VERTEX_BUFFER_VIEW  m_skelLineVbv[2]{};
    D3D12_INDEX_BUFFER_VIEW   m_skelLineIbv[2]{};
    Dark::Entity                    m_flashlight;
    Dark::Entity                    m_muzzle;
    float                           m_muzzleTimer = 0.0f;
    Dark::Camera3D          m_viewCamera;
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f> m_prevWorldByEntity;
    Dark::Sky::Environment  m_env;
    Dark::IblSettings       m_ibl;
    Dark::GtaoSettings      m_ssao;
    Dark::SsrSettings       m_ssr;
    Dark::AssetID           m_iblImageId = Dark::NULL_ASSET;

    Dark::PathChase                m_chase;
    bool                           m_chaseOk = false;
    struct WeaponTargetScratch
    {
        Dark::Entity         entity{};
        Dark::Math::Vector3f center{};
        Dark::Math::Vector3f halfExtents{ 1.0f, 1.0f, 1.0f };
        bool                 alive = false;
    };
    std::vector<WeaponTargetScratch> m_weaponTargets;

    Dark::Terrain::TerrainGrid m_terrain;
    float                     m_terrainSeaLevel = 0.0f;
    bool                      m_haveTerrainSea  = false;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::WaterWorld            m_water;

    Dark::AssetID m_cubeModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_packModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_lanternModelId = Dark::NULL_ASSET;
    Dark::AssetID m_tracerModelId  = Dark::NULL_ASSET;
    Dark::NetRole m_netRole        = Dark::NetRole::Idle;
    uint32_t      m_browseLogCount = ~0u;
    bool          m_netBrowsing    = false;
    bool          m_gameplayPaused = false;
    bool          m_stepGameplay   = false;
    float         m_spinSpeed      = 0.8f;
    bool          m_showShadowMaps = false;
    bool          m_showDepth      = false;
    bool          m_showGBuffer    = false;
    bool          m_showVelocity   = false;
    bool          m_showSkeleton   = false;
    bool          m_showDevTools   = false;
    ImGuiHost     m_imgui;
    Dark::MainMenu m_menu;
    char          m_joinHost[64]   = "127.0.0.1";
    float         m_lookYaw        = 0.0f;
    float         m_lookPitch      = 0.18f;
    float         m_lowerBodyYaw   = 0.0f;
    bool          m_playerWet      = false;
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
};
