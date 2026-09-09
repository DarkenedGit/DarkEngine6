#pragma once

#include "Core/Application.h"
#include "Render/Mesh.h"
#include "Render/LineMesh.h"
#include "Assets/Model.h"
#include "Render/MeshPipeline.h"
#include "Render/ModelDraw.h"
#include "Render/LinePipeline.h"
#include "Render/TonemapPipeline.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/LocalLightGpuList.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Render/BloomPipeline.h"
#include "Render/MotionBlurPipeline.h"
#include "Render/TaaPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Camera2D.h"
#include "Render/ShadowSystem.h"
#include "Render/DebugOverlay.h"
#include "Render/Material.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"
#include "Scene/SceneTypes.h"
#include "Math/AABox2f.h"
#include "Editor/EditorImGui.h"
#include "Editor/ParticleEditorPanel.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"

#include "Audio/SoundClip.h"

#include <filesystem>
#include <memory>
#include <vector>

using namespace Dark;

class EditorApp : public Application
{
public:
    explicit EditorApp(const AppConfig& cfg);

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
    void drawStatusBar();
    void applySceneMode(SceneMode mode);
    void newScene3D();
    void newScene2D();
    bool ensure2DResources();
    void rebuildGrid2D();
    void clampCamera2D();
    bool worldFromMouse2D(Math::Vector2f& out);
    Math::AABox2f objectBounds2D(SceneObjectType type, const Math::Vector3f& pos, const Math::Vector3f& scale) const;
    Entity pickObject2D(const Math::Vector2f& world);
    void drawSprite2D(
        ID3D12GraphicsCommandList* cmd,
        const Texture2D& texture,
        const Math::Vector2f& pos,
        const Math::Vector2f& size,
        float z,
        float cr,
        float cg,
        float cb,
        float uvSx,
        float uvSy);
    void renderScene3D(ID3D12GraphicsCommandList* cmd);
    void renderScene2D(ID3D12GraphicsCommandList* cmd);

    bool groundHitFromMouse(Math::Vector3f& outPoint);
    bool groundHitFromRay(const Math::Ray3f& ray, Math::Vector3f& outPoint) const;
    Entity pickObject(const Math::Ray3f& ray);

    Entity spawnObject(SceneObjectType type,
                             const Math::Vector3f& pos,
                             const Math::Vector3f& scale,
                             const Math::Quaternion& rot,
                             const float color[4],
                             const ParticleEmitterDesc* particleDesc = nullptr,
                             const SceneObjectData* authored = nullptr,
                             bool registerNet = true);

    Entity placeAtCursor(SceneObjectType type);
    Entity placeGlowProp();
    void   drawInspector3D();
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

    static bool onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(World& world, Entity e, NetId id, void* user);
    static void onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user);

    void clearScene();
    bool saveScene();
    bool loadScene();

    ParticleEmitterDesc makeDefaultParticleDesc() const;
    void applyParticleDescToEmitter(int emitterIndex, const ParticleEmitterDesc& desc);
    void fillParticleDescFromEmitter(int emitterIndex, ParticleEmitterDesc& out) const;
    void syncSelectedEmitterFromUi();

    const Mesh* meshForType(SceneObjectType type) const;
    SceneObject* findObject(Entity e);
    const SceneObject* findObject(Entity e) const;
    ParticleEmitter* selectedEmitter();

    static float snap(float v, float grid);

    MeshPipeline    m_meshPipeline;
    MeshPipeline    m_meshTransparentPipeline;
    LinePipeline    m_linePipeline;
    LinePipeline    m_linePipeline3D;
    TonemapPipeline          m_tonemap;
    DeferredLightingPipeline m_lighting;
    LocalLightVolumePipeline m_localLightVolumes;
    LocalLightGpuList        m_localLightGpu;
    Mesh                     m_pointVolumeMesh;
    Mesh                     m_spotVolumeMesh;
    BloomPipeline            m_bloom;
    MotionBlurPipeline       m_motionBlur;
    TaaPipeline              m_taa;
    DebugOverlay             m_debugOverlay;
    ShadowSystem    m_shadows;

    Mesh m_cubeMesh;
    Mesh m_sphereMesh;
    Mesh m_groundMesh;
    LineMesh m_gridMesh;
    LineMesh m_pointLightGizmo;
    LineMesh m_spotLightGizmo;

    AssetRef<Material> m_propMaterial;
    AssetRef<Material> m_groundMaterial;

    Camera3D m_camera;
    Math::Matrix4f m_prevViewProj{};
    bool                 m_havePrevViewProj = false;
    bool                 m_taaHistoryValid  = false;
    uint32_t             m_taaHistoryW      = 0;
    uint32_t             m_taaHistoryH      = 0;
    uint32_t             m_bloomW           = 0;
    uint32_t             m_bloomH           = 0;
    Camera2D m_camera2D;
    SceneMode m_sceneMode = SceneMode::Scene3D;

    SpritePipeline     m_spritePipe;
    Mesh     m_quadMesh;
    LineMesh m_grid2D;
    LineMesh m_boxOutline2D;
    Texture2D          m_texPlatform;
    Texture2D          m_texCoin;
    Texture2D          m_texSpawn;
    bool                     m_2dReady = false;

    Math::Vector2f m_worldMin{ 0.0f, 0.0f };
    Math::Vector2f m_worldMax{ 96.0f, 22.0f };
    bool                 m_panning = false;
    int                  m_panMouseX = 0;
    int                  m_panMouseY = 0;

    std::vector<SceneObject>                   m_objects;
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;
    ParticleRenderer                           m_particleRenderer;
    Entity                                     m_selected{};

    EditorImGui          m_imgui;
    ParticleEditorPanel  m_particlePanel;
    bool                 m_showParticlePanel = true;

    SceneObjectType m_placeType  = SceneObjectType::Cube;
    int                   m_colorIndex = 0;

    std::filesystem::path m_scenePath;
    std::string           m_sceneName = "level";

    bool  m_showGrid    = true;
    bool  m_showSolid   = true;
    bool  m_showGBuffer  = false;
    bool  m_showVelocity = false;
    float m_gridSnap  = 1.0f;

    float m_moveSpeed = 8.0f;
    float m_lookSpeed = 0.005f;

    std::shared_ptr<Audio::SoundClip> m_sfxPlace;
    std::shared_ptr<Audio::SoundClip> m_sfxDelete;
    std::shared_ptr<Audio::SoundClip> m_sfxSave;

    bool m_dragging = false;
    int  m_lmbDownX = 0;
    int  m_lmbDownY = 0;

    char m_joinAddress[64]{"127.0.0.1"};

    bool          m_cliJoin     = false;
    NetRole m_lastNetRole = NetRole::Idle;
};
