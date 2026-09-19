#pragma once

#include "Core/Application.h"
#include "Render/Mesh.h"
#include "Render/LineMesh.h"
#include "Assets/Model.h"
#include "Render/ModelDraw.h"
#include "Render/LinePipeline.h"
#include "Render/SceneRenderer.h"
#include "Render/Camera3D.h"
#include "Render/Camera2D.h"
#include "Assets/Material.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"
#include "Scene/SceneTypes.h"
#include "Editor/EditorObject.h"
#include "Math/AABox2f.h"
#include "Editor/EditorImGui.h"
#include "Editor/TranslateGizmo.h"
#include "Editor/ParticleEditorPanel.h"
#include "Editor/AnimEditorPanel.h"
#include "Editor/HsmEditorPanel.h"
#include "Animation/AnimGraphComponent.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleRenderer.h"

#include "Audio/SoundClip.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"

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
    void ensureGlobalLights();
    void gatherEditorLighting(Math::Vector3f& lightDir, Math::Vector3f& lightColor, Math::Vector3f& ambientColor);
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
    Entity pickSelectedGizmo(const Math::Vector2f& mouse, Dark::EditorDetail::TranslateGizmoAxis& outAxis);
    void   drawTranslateGizmos();
    void   applyGizmoDrag(const Math::Ray3f& ray);

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
    void applyIbl();

    bool loadGltfModel();
    bool saveGltfModel();
    bool saveGltfModelAs();
    bool spawnLoadedModel(const AssetRef<Model>& model);
    void frameCameraOnModel(const Model& model, const TransformComponent& xf);
    void drawModelPartsPanel();
    void drawMaterialPanel();
    void drawTerrainPanel();
    void syncTerrainLod();
    bool createEditorTerrain();
    void removeEditorTerrain();
    bool rebuildTerrainGpuFromSurface();
    bool uploadTerrainSplatGpu();
    void applyTerrainBrush(float dt);
    bool loadTerrainFromScene(const SceneFileData& data, const std::filesystem::path& scenePath);
    void fillTerrainSceneDesc(SceneFileData& data) const;
    bool saveTerrainSidecars(const std::filesystem::path& scenePath) const;

    enum class TerrainBrushMode : uint8_t
    {
        None = 0,
        Paint,
        SculptRaise,
        SculptLower,
        SculptSmooth,
    };
    AssetRef<Model> selectedModel();
    const Model::Part* selectedModelPart();
    AssetRef<Material> meshMaterialOf(Entity e);
    bool               meshMaterialShared(AssetID id);
    AssetRef<Material> ensureUniqueMeshMaterial(Entity e);
    AssetRef<Material> ensureUniqueParticleMaterial(Entity e);

    ParticleEmitterDesc makeDefaultParticleDesc() const;

    const Mesh* meshForType(SceneObjectType type) const;
    EditorObjectComponent* findObject(Entity e);
    uint32_t editorObjectCount();
    void collectEditorEntities(std::vector<Entity>& out);
    ParticleEmitter* selectedEmitter();
    AnimGraphComponent* selectedAnimGraph();
    bool attachAnimGraph(Entity e, const AssetRef<Model>& model);
    bool ensureAnimGraphOnSelected();

    static float snap(float v, float grid);

    SceneRenderer   m_scene;
    LinePipeline    m_linePipeline;
    LinePipeline    m_linePipeline3D;

    Mesh m_cubeMesh;
    Mesh m_sphereMesh;
    Mesh m_groundMesh;
    LineMesh m_gridMesh;
    LineMesh m_pointLightGizmo;
    LineMesh m_spotLightGizmo;
    LineMesh m_dirLightGizmo;

    AssetRef<Material> m_propMaterial;
    AssetRef<Material> m_groundMaterial;

    Camera3D m_camera;
    Camera2D m_camera2D;
    SceneMode m_sceneMode = SceneMode::Scene3D;
    IblSettings m_ibl;
    AssetID     m_iblImageId = NULL_ASSET;

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

    ParticleRenderer                           m_particleRenderer;
    Entity                                     m_selected{};

    EditorImGui          m_imgui;
    ParticleEditorPanel  m_particlePanel;
    AnimEditorPanel      m_animPanel;
    HsmEditorPanel       m_hsmPanel;
    bool                 m_showParticlePanel = true;
    bool                 m_showAnimPanel     = true;
    bool                 m_showHsmPanel      = true;

    SceneObjectType m_placeType  = SceneObjectType::Cube;
    int                   m_colorIndex = 0;

    std::filesystem::path m_scenePath;
    std::string           m_sceneName = "level";

    bool  m_showGrid    = true;
    bool  m_showSolid   = true;
    bool  m_showGBuffer  = false;
    bool  m_showVelocity = false;
    bool  m_showModelParts     = true;
    bool  m_showMaterialEditor = false;
    bool  m_showTerrainPanel   = true;

    Terrain::TerrainWorld         m_terrain;
    TerrainMaterial               m_terrainMaterial;
    Terrain::SplatMap             m_splat;
    Terrain::SplatRules           m_splatRules;
    Terrain::TerrainSurfaceDesc   m_terrainSurface;
    bool                          m_haveTerrain         = false;
    bool                          m_terrainHeightDirty  = false;
    bool                          m_terrainSplatDirty   = false;
    TerrainBrushMode              m_terrainBrush        = TerrainBrushMode::None;
    int                           m_terrainPaintLayer   = 1;
    bool                          m_terrainPaintLower   = false;
    float                         m_terrainBrushRadius  = 8.0f;
    float                         m_terrainBrushStrength = 0.5f;
    std::string                   m_terrainHeightFile;
    std::string                   m_terrainSplatFile;
    std::string                   m_terrainAlbedoPath[Terrain::kMaxTerrainLayers];
    std::string                   m_terrainNormalPath[Terrain::kMaxTerrainLayers];
    std::string                   m_terrainOrmPath[Terrain::kMaxTerrainLayers];
    int   m_selectedPart       = 0;
    float m_gridSnap  = 1.0f;

    float m_moveSpeed = 8.0f;
    float m_lookSpeed = 0.005f;

    std::shared_ptr<Audio::SoundClip> m_sfxPlace;
    std::shared_ptr<Audio::SoundClip> m_sfxDelete;
    std::shared_ptr<Audio::SoundClip> m_sfxSave;

    bool m_dragging = false;
    int  m_lmbDownX = 0;
    int  m_lmbDownY = 0;
    Entity                                 m_gizmoHoverEntity{};
    Dark::EditorDetail::TranslateGizmoAxis m_gizmoHover    = Dark::EditorDetail::TranslateGizmoAxis::None;
    Dark::EditorDetail::TranslateGizmoAxis m_gizmoDragAxis = Dark::EditorDetail::TranslateGizmoAxis::None;
    Math::Vector3f                         m_gizmoDragStart{ 0.0f, 0.0f, 0.0f };
    Math::Vector3f                         m_gizmoGrabPoint{ 0.0f, 0.0f, 0.0f };

    char m_joinAddress[64]{"127.0.0.1"};

    bool          m_cliJoin     = false;
    NetRole m_lastNetRole = NetRole::Idle;
};
