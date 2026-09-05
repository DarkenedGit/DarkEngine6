#pragma once
#include "Core/Application.h"
#include "Network/Replication.h"
#include "Render/Mesh.h"
#include "Render/MeshPipeline.h"
#include "Render/TerrainPipeline.h"
#include "Render/WaterPipeline.h"
#include "Render/SkyPipeline.h"
#include "Render/TonemapPipeline.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/MotionBlurPipeline.h"
#include "Render/TaaPipeline.h"
#include "Render/ShadowSystem.h"
#include "Render/DebugOverlay.h"
#include "Render/Camera3D.h"
#include "Sky/Environment.h"
#include "Render/Material.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"
#include "Audio/SoundClip.h"
#include "Character/Health.h"
#include "Character/PlayerMotor.h"
#include "Render/HealthHud.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"
#include "Particles/BloodSplatPool.h"
#include "PathChase.h"
#include "Ui/ImGuiHost.h"

#include <unordered_map>

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
    void spawnHunterBlood(const Dark::Math::Vector3f& pos);
    void respawnPlayer();
    void placeHealthPacks();
    void updateHealthPacks(float dt);
    void drawHealthPacks(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, Dark::MeshFrameConstants& cb);
    void drawHealthPacksGBuffer(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj);
    void drawHealthPacksDepth(ID3D12GraphicsCommandList* cmd, int cascade);
    void updateShoulderCamera();
    Dark::TonemapSettings playerPostFx();
    Dark::Entity possessedBody();
    void spawnOwnedPawn(Dark::ClientId owner, float offsetX);
    Dark::Entity findPawn(Dark::ClientId owner);
    void ensureLocalCube();
    void syncTerrainLod();
    void drawDebugOverlays(ID3D12GraphicsCommandList* cmd);

    static bool onNetSpawn(Dark::World& world, Dark::Entity e, Dark::NetPrefab prefab, const Dark::TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(Dark::World& world, Dark::Entity e, Dark::NetId id, void* user);
    static void onNetPeer(const Dark::NetPeerInfo& info, Dark::NetPeerEvent event, void* user);

    Dark::Entity m_camera;
    Dark::Entity m_cube;

    Dark::Mesh    m_cubeMesh;
    Dark::MeshPipeline      m_meshPipeline;
    Dark::TerrainPipeline   m_terrainPipeline;
    Dark::WaterPipeline     m_waterPipeline;
    Dark::SkyPipeline       m_skyPipeline;
    Dark::TonemapPipeline           m_tonemap;
    Dark::DeferredLightingPipeline  m_lighting;
    Dark::MotionBlurPipeline        m_motionBlur;
    Dark::TaaPipeline               m_taa;
    Dark::ShadowSystem      m_shadows;
    Dark::DebugOverlay      m_debugOverlay;
    Dark::Camera3D          m_viewCamera;
    Dark::Math::Matrix4f    m_prevViewProj{};
    bool                    m_havePrevViewProj = false;
    bool                    m_taaHistoryValid  = false;
    uint32_t                m_taaHistoryW      = 0;
    uint32_t                m_taaHistoryH      = 0;
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f> m_prevWorldByEntity;
    Dark::Sky::Environment  m_env;

    Dark::AssetRef<Dark::Material> m_cubeMaterial;
    Dark::AssetRef<Dark::Material> m_treeTrunkMaterial;
    Dark::AssetRef<Dark::Material> m_treeMaterial;
    Dark::AssetRef<Dark::Material> m_aiMaterial;
    Dark::PathChase                m_chase;
    bool                           m_chaseOk = false;

    Dark::Terrain::TerrainWorld m_terrain;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::Water::WaterWorld     m_water;

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
    bool          m_showDevTools   = false;
    ImGuiHost     m_imgui;
    char          m_joinHost[64]   = "127.0.0.1";
    float         m_lookYaw        = 0.0f;
    float         m_lookPitch      = 0.18f;
    bool          m_playerWet      = false;
    float         m_footstepAcc    = 0.0f;
    uint32_t      m_waterVoice     = 0;
    Dark::PlayerMotor                m_motor;
    Dark::Health                     m_playerHealth;
    Dark::HealthHud                  m_healthHud;
    Dark::Math::Vector3f             m_playerSpawn{ 0.0f, 0.5f, 0.0f };
    bool                             m_havePlayerSpawn = false;
    float                            m_playerDeadTimer = 0.0f;
    float                            m_spawnAge        = 0.0f;
    float                            m_attackCooldown  = 0.0f;
    float                            m_hurtSoundTimer  = 0.0f;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxStep;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxWater;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxGrunt;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxLand;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxSplash;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxPain;
    Dark::ParticleEmitter            m_blood;
    Dark::ParticleRenderer           m_particles;
    Dark::BloodSplatPool             m_bloodSplats;

    struct HealthPack
    {
        Dark::Math::Vector3f pos{};
        Dark::Math::Matrix4f prevWorld{};
        bool                 havePrevWorld = false;
        bool                 active        = false;
        float                respawnIn     = 0.0f;
    };
    static constexpr int             kMaxHealthPacks = 4;
    HealthPack                       m_healthPacks[kMaxHealthPacks]{};
    int                              m_healthPackCount = 0;
    float                            m_packSpin        = 0.0f;
    float                            m_packBob         = 0.0f;
    Dark::Mesh             m_crossMesh;
    Dark::AssetRef<Dark::Material>   m_packMaterial;
    std::shared_ptr<Dark::Audio::SoundClip> m_sfxHeal;
};
