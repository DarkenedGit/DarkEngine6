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
#include "Assets/Material.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"
#include "Audio/SoundClip.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Render/HealthHud.h"
#include "Render/CrosshairHud.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"
#include "Particles/BloodSplatPool.h"
#include "Weapons/WeaponLoadout.h"
#include "Gameplay/HealthPack.h"
#include "PathChase.h"
#include "Ui/HudTagComponent.h"
#include "Weapons/HittableComponent.h"
#include "Ui/ImGuiHost.h"

#include <unordered_map>
#include <vector>

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
    void spawnGltfDemo();
    void spawnAnimatedDemo();
    void updateWiggleAnim();
    static void onWiggleNotify(void* user, const Dark::AnimNotify& n);
    void spawnHybridLocalLights();
    void updateFlashlight();
    void pulseMuzzle();
    void drawLanternFixtures(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, Dark::MeshFrameConstants& cb);
    void drawLanternFixturesGBuffer(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj);
    void drawLanternFixturesDepth(ID3D12GraphicsCommandList* cmd, int cascade);
    void spawnHunterBlood(const Dark::Math::Vector3f& pos);
    void respawnPlayer();
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
    Dark::Health*       localHealth();
    Dark::HitReaction*  localHit();
    Dark::PlayerMotor*  localMotor();
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
    std::vector<Dark::Entity>       m_lanternFixtures;
    Dark::Camera3D          m_viewCamera;
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f> m_prevWorldByEntity;
    Dark::Sky::Environment  m_env;

    Dark::AssetRef<Dark::Material> m_cubeMaterial;
    Dark::AssetRef<Dark::Material> m_treeTrunkMaterial;
    Dark::AssetRef<Dark::Material> m_treeMaterial;
    Dark::AssetRef<Dark::Material> m_aiMaterial;
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

    Dark::Terrain::TerrainWorld m_terrain;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::WaterWorld            m_water;

    std::shared_ptr<Dark::Audio::SoundClip> m_sfxReset;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxClick;
    std::shared_ptr<Dark::Audio::SoundClip> m_music;

    Dark::AssetID m_cubeMatId      = Dark::NULL_ASSET;
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
    char          m_joinHost[64]   = "127.0.0.1";
    float         m_lookYaw        = 0.0f;
    float         m_lookPitch      = 0.18f;
    bool          m_playerWet      = false;
    float         m_footstepAcc    = 0.0f;
    Dark::HealthHud                  m_healthHud;
    Dark::Math::Vector3f             m_playerSpawn{ 0.0f, 0.5f, 0.0f };
    bool                             m_havePlayerSpawn = false;
    float                            m_playerDeadTimer = 0.0f;
    float                            m_spawnAge        = 0.0f;
    float                            m_hurtSoundTimer  = 0.0f;
    Dark::WeaponLoadout              m_weapons;
    Dark::CrosshairHud               m_crosshair;
    Dark::Mesh                       m_tracerMesh;
    Dark::AssetRef<Dark::Material>   m_tracerMaterial;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxStep;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxWater;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxGrunt;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxLand;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxSplash;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxPain;
    Dark::ParticleEmitter            m_blood;
    Dark::ParticleRenderer           m_particles;
    Dark::BloodSplatPool             m_bloodSplats;

    Dark::Mesh             m_crossMesh;
    Dark::AssetRef<Dark::Material>   m_packMaterial;
    Dark::AssetRef<Dark::Material>   m_lanternMaterial;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxHeal;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxFire;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxImpact;
};
