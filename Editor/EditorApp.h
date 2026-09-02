#pragma once

#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Geometry/LineMesh.h"
#include "Render/MeshPipeline.h"
#include "Render/LinePipeline.h"
#include "Render/TonemapPipeline.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Camera2D.h"
#include "Render/ShadowSystem.h"
#include "Render/Material.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"
#include "Scene/SceneTypes.h"
#include "Math/Aabb2f.h"
#include "Editor/EditorImGui.h"
#include "Editor/ParticleEditorPanel.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"

#include "Audio/SoundClip.h"

#include <filesystem>
#include <memory>
#include <vector>

class EditorApp : public Dark::Application
{
public:
    explicit EditorApp(const Dark::AppConfig& cfg);

    void onInit() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void registerActions();
    void updateCamera(float dt);
    void updateCamera2D(float dt);
    void handleEditorCommands(float dt);
    void drawEditorUi();
    void applySceneMode(Dark::SceneMode mode);
    void newScene3D();
    void newScene2D();
    bool ensure2DResources();
    void rebuildGrid2D();
    void clampCamera2D();
    bool worldFromMouse2D(Dark::Math::Vector2f& out);
    Dark::Math::Aabb2f objectBounds2D(Dark::SceneObjectType type, const Dark::Math::Vector3f& pos, const Dark::Math::Vector3f& scale) const;
    Dark::Entity pickObject2D(const Dark::Math::Vector2f& world);
    void drawSprite2D(
        ID3D12GraphicsCommandList* cmd,
        const Dark::Texture2D& texture,
        const Dark::Math::Vector2f& pos,
        const Dark::Math::Vector2f& size,
        float z,
        float cr,
        float cg,
        float cb,
        float uvSx,
        float uvSy);
    void renderScene3D(ID3D12GraphicsCommandList* cmd);
    void renderScene2D(ID3D12GraphicsCommandList* cmd);

    bool groundHitFromMouse(Dark::Math::Vector3f& outPoint);
    bool groundHitFromRay(const Dark::Math::Ray3f& ray, Dark::Math::Vector3f& outPoint) const;
    Dark::Entity pickObject(const Dark::Math::Ray3f& ray);

    Dark::Entity spawnObject(Dark::SceneObjectType type,
                             const Dark::Math::Vector3f& pos,
                             const Dark::Math::Vector3f& scale,
                             const Dark::Math::Quaternion& rot,
                             const float color[4],
                             const Dark::ParticleEmitterDesc* particleDesc = nullptr);

    Dark::Entity placeAtCursor(Dark::SceneObjectType type);
    void         deleteSelected();
    void         selectNext(int delta);
    void         cyclePlaceType(int delta);
    void         cycleSelectedColor();

    bool netClientLocked();
    bool netSceneLocked();
    bool canHostSession();
    bool canJoinSession();
    void registerReplicatedProps();
    void hostNetworkSession();
    void joinNetworkSession();
    void discardLocalSceneForJoin();
    void drawNetworkMenu();
    void drawDebugMenu();

    static bool onNetSpawn(Dark::World& world, Dark::Entity e, Dark::NetPrefab prefab, const Dark::TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(Dark::World& world, Dark::Entity e, Dark::NetId id, void* user);
    static void onNetPeer(const Dark::NetPeerInfo& info, Dark::NetPeerEvent event, void* user);

    void clearScene();
    bool saveScene();
    bool loadScene();

    Dark::ParticleEmitterDesc makeDefaultParticleDesc() const;
    void applyParticleDescToEmitter(int emitterIndex, const Dark::ParticleEmitterDesc& desc);
    void fillParticleDescFromEmitter(int emitterIndex, Dark::ParticleEmitterDesc& out) const;
    void syncSelectedEmitterFromUi();

    const Dark::Geometry::Mesh* meshForType(Dark::SceneObjectType type) const;
    Dark::SceneObject* findObject(Dark::Entity e);
    const Dark::SceneObject* findObject(Dark::Entity e) const;
    Dark::ParticleEmitter* selectedEmitter();

    static float snap(float v, float grid);

    Dark::MeshPipeline    m_meshPipeline;
    Dark::LinePipeline    m_linePipeline;
    Dark::LinePipeline    m_linePipeline3D;
    Dark::TonemapPipeline          m_tonemap;
    Dark::DeferredLightingPipeline m_lighting;
    Dark::ShadowSystem    m_shadows;

    Dark::Geometry::Mesh m_cubeMesh;
    Dark::Geometry::Mesh m_sphereMesh;
    Dark::Geometry::Mesh m_groundMesh;
    Dark::Geometry::LineMesh m_gridMesh;

    Dark::AssetRef<Dark::Material> m_propMaterial;
    Dark::AssetRef<Dark::Material> m_groundMaterial;

    Dark::Camera3D m_camera;
    Dark::Camera2D m_camera2D;
    Dark::SceneMode m_sceneMode = Dark::SceneMode::Scene3D;

    Dark::SpritePipeline     m_spritePipe;
    Dark::Geometry::Mesh     m_quadMesh;
    Dark::Geometry::LineMesh m_grid2D;
    Dark::Geometry::LineMesh m_boxOutline2D;
    Dark::Texture2D          m_texPlatform;
    Dark::Texture2D          m_texCoin;
    Dark::Texture2D          m_texSpawn;
    bool                     m_2dReady = false;

    Dark::Math::Vector2f m_worldMin{ 0.0f, 0.0f };
    Dark::Math::Vector2f m_worldMax{ 96.0f, 22.0f };
    bool                 m_panning = false;
    int                  m_panMouseX = 0;
    int                  m_panMouseY = 0;

    std::vector<Dark::SceneObject>                   m_objects;
    std::vector<std::unique_ptr<Dark::ParticleEmitter>> m_emitters;
    Dark::ParticleRenderer                           m_particleRenderer;
    Dark::Entity                                     m_selected{};

    EditorImGui          m_imgui;
    ParticleEditorPanel  m_particlePanel;
    bool                 m_showParticlePanel = true;

    Dark::SceneObjectType m_placeType  = Dark::SceneObjectType::Cube;
    int                   m_colorIndex = 0;

    std::filesystem::path m_scenePath;
    std::string           m_sceneName = "level";

    bool  m_showGrid  = true;
    bool  m_showSolid = true;
    float m_gridSnap  = 1.0f;

    float m_moveSpeed = 8.0f;
    float m_lookSpeed = 0.005f;

    std::shared_ptr<Dark::SoundClip> m_sfxPlace;
    std::shared_ptr<Dark::SoundClip> m_sfxDelete;
    std::shared_ptr<Dark::SoundClip> m_sfxSave;

    bool m_dragging = false;
    int  m_lmbDownX = 0;
    int  m_lmbDownY = 0;

    char m_joinAddress[64]{"127.0.0.1"};

    bool          m_cliJoin     = false;
    Dark::NetRole m_lastNetRole = Dark::NetRole::Idle;
};
