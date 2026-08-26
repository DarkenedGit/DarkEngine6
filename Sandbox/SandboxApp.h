#pragma once
#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Network/Replication.h"
#include "Render/MeshPipeline.h"
#include "Render/TerrainPipeline.h"
#include "Render/WaterPipeline.h"
#include "Render/SkyPipeline.h"
#include "Render/ShadowSystem.h"
#include "Render/DebugOverlay.h"
#include "Render/Camera3D.h"
#include "Sky/Environment.h"
#include "Render/Material.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"
#include "Audio/SoundClip.h"

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
    void updateFlyCamera(float dt);
    void updatePawnMotion(float dt);
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

    Dark::Geometry::Mesh    m_cubeMesh;
    Dark::MeshPipeline      m_meshPipeline;
    Dark::TerrainPipeline   m_terrainPipeline;
    Dark::WaterPipeline     m_waterPipeline;
    Dark::SkyPipeline       m_skyPipeline;
    Dark::ShadowSystem      m_shadows;
    Dark::DebugOverlay      m_debugOverlay;
    Dark::Camera3D          m_viewCamera;
    Dark::Sky::Environment  m_env;

    Dark::AssetRef<Dark::Material> m_cubeMaterial;

    Dark::Terrain::TerrainWorld m_terrain;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::Water::WaterWorld     m_water;

    std::shared_ptr<Dark::SoundClip> m_sfxReset;
    std::shared_ptr<Dark::SoundClip> m_sfxClick;
    std::shared_ptr<Dark::SoundClip> m_music;

    Dark::AssetID m_cubeMatId      = Dark::NULL_ASSET;
    Dark::NetRole m_netRole        = Dark::NetRole::Idle;
    uint32_t      m_browseLogCount = ~0u;
    bool          m_netBrowsing    = false;
    bool          m_spinPaused     = false;
    float         m_spinSpeed      = 0.8f;
    bool          m_showShadowMaps = false;
    bool          m_showDepth      = false;
};
