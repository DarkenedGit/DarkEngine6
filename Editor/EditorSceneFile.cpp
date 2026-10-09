#include "EditorApp.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "Editor/EditorFileDialog.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Save/PersistentId.h"
#include "Save/SaveSystem.h"
#include "Core/UiPalette.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/AABox2f.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"
#include "Render/LineMesh.h"
#include "Render/MeshGen.h"
#include "Render/TaaJitter.h"
#include "Render/ModelDraw.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Sky/CloudVolume.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

bool EditorApp::saveSceneWithDialog()
{
    if (m_playMode)
    {
        static bool logged = false;
        if (!logged)
        {
            DE_LOG_INFO("Editor: scene save blocked during play");
            logged = true;
        }
        return false;
    }
    const std::filesystem::path suggested = m_scenePath.empty() ? defaultScenePath(m_sceneMode == SceneMode::Scene2D ? "level2d.json" : "level.json") : m_scenePath;
    std::filesystem::path       chosen;
    if (!pickEditorFile(window().nativeHandle(), true, L"Save Scene", L"Scene (*.json)\0*.json\0All files (*.*)\0*.*\0", L"json", suggested, chosen))
        return false;
    m_scenePath = chosen;
    m_sceneName = chosen.stem().string();
    m_save.setWorldIdentity(m_scenePath.generic_string(), "", "");
    return saveScene();
}

bool EditorApp::loadSceneWithDialog(const std::filesystem::path& suggested)
{
    if (netSceneLocked())
        return false;
    const std::filesystem::path start = suggested.empty() ? m_scenePath : suggested;
    std::filesystem::path       chosen;
    if (!pickEditorFile(window().nativeHandle(), false, L"Load Scene", L"Scene (*.json)\0*.json\0All files (*.*)\0*.*\0", L"json", start, chosen))
        return false;
    m_scenePath = chosen;
    m_sceneName = chosen.stem().string();
    m_save.setWorldIdentity(m_scenePath.generic_string(), "", "");
    return loadScene();
}

bool EditorApp::saveScene()
{
    SceneFileData data{};
    data.version         = 3;
    data.name            = m_sceneName;
    data.mode            = m_sceneMode;
    data.worldMin        = m_worldMin;
    data.worldMax        = m_worldMax;
    data.environment     = m_ibl.virtualPath;
    data.iblIntensity    = m_ibl.intensity;
    data.iblRotationRadY = m_ibl.rotationRadY;

    std::vector<Entity> saved;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const auto* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        SceneObjectData d{};
        d.type     = so.type;
        d.position = xf->position;
        d.rotation = xf->rotation;
        d.scale    = xf->scale;
        copyColor(d.color, so.color);
        if (ParticleEmitterComponent* pe = world().get<ParticleEmitterComponent>(e))
        {
            if (pe->runtime)
                sceneDataFromDesc(pe->runtime->desc(), d);
            else
                sceneDataFromDesc(pe->desc, d);
        }
        if (const auto* light = world().get<LocalLightComponent>(e))
        {
            d.hasLight          = true;
            d.lightIntensity    = light->intensity;
            d.lightRange        = light->range;
            d.lightInnerDeg     = light->innerConeDeg;
            d.lightOuterDeg     = light->outerConeDeg;
            d.lightSourceRadius = light->sourceRadius;
            d.lightEnabled      = light->enabled;
            d.lightCastShadow   = light->castShadow;
            d.color[0]          = light->color.x;
            d.color[1]          = light->color.y;
            d.color[2]          = light->color.z;
        }
        else if (const auto* dir = world().get<DirectionalLightComponent>(e))
        {
            d.hasLight       = true;
            d.lightIntensity = dir->intensity;
            d.lightEnabled   = dir->enabled;
            d.color[0]       = dir->color.x;
            d.color[1]       = dir->color.y;
            d.color[2]       = dir->color.z;
        }
        else if (const auto* amb = world().get<AmbientLightComponent>(e))
        {
            d.hasLight       = true;
            d.lightIntensity = amb->intensity;
            d.lightEnabled   = amb->enabled;
            d.color[0]       = amb->color.x;
            d.color[1]       = amb->color.y;
            d.color[2]       = amb->color.z;
        }
        if (const auto* mc = world().get<MeshComponent>(e))
            d.emissive = mc->emissive;
        if (const CloudVolumeComponent* cloud = world().get<CloudVolumeComponent>(e))
            sceneDataFromCloudDesc(cloud->desc, d);
        if (const StreamComponent* stream = world().get<StreamComponent>(e))
        {
            d.hasStream        = true;
            d.streamWidth      = stream->width;
            d.streamFlowSpeed  = stream->flowSpeed;
            d.streamPoints     = stream->points;
            if (!stream->points.empty())
            {
                d.position.x = stream->points.front().x;
                d.position.z = stream->points.front().y;
            }
        }
        if (so.type == SceneObjectType::Model)
        {
            if (const ModelComponent* modelComp = world().get<ModelComponent>(e))
            {
                if (const auto model = assets().getAs<Model>(modelComp->modelAssetID))
                {
                    const std::string virt = assets().virtualPathFromAbsolute(model->sourcePath());
                    d.modelPath = virt.empty() ? model->sourcePath().generic_string() : virt;
                }
            }
        }
        data.objects.push_back(d);
        saved.push_back(e);
    });
    fillTerrainSceneDesc(data);
    if (data.mode != SceneMode::Scene2D)
    {
        captureSceneAtmosphere(m_env, data);
        if (m_authoredWater.present)
        {
            data.water = m_authoredWater;
            const WaterParams& wp = m_water.params();
            data.water.amplitudeScale = wp.amplitudeScale;
            data.water.speedScale     = wp.speedScale;
            data.water.flowSpeed      = wp.flowSpeed;
            data.water.foam           = wp.foam;
            data.water.foamWidthScale = wp.foamWidthScale;
            data.water.detail         = wp.detailAmount;
            data.water.flowDir[0]     = wp.flowDir.x;
            data.water.flowDir[1]     = wp.flowDir.y;
            data.water.flowStrength   = wp.flowStrength;
            data.water.steepness      = wp.steepness;
            data.water.waveCount      = kWaterWaveCount;
            for (int i = 0; i < kWaterWaveCount; ++i)
            {
                data.water.waves[i].angleFromFlow = wp.waves[i].angleFromFlow;
                data.water.waves[i].frequency     = wp.waves[i].frequency;
                data.water.waves[i].amplitude     = wp.waves[i].amplitude;
                data.water.waves[i].speed         = wp.waves[i].speed;
            }
        }
        if (m_authoredExposure.present)
            data.exposure = m_authoredExposure;
    }
    if (data.hasTerrain)
    {
        if (!saveTerrainSidecars(m_scenePath))
        {
            DE_LOG_ERROR("Editor: terrain sidecar save failed");
            return false;
        }
        m_terrainHeightFile = data.terrain.heightFile;
        m_terrainSplatFile  = data.terrain.splatFile;
        if (data.terrain.hasGrid)
        {
            m_terrainCoarseFile = data.terrain.grid.coarseFile;
            m_terrainTileDir    = data.terrain.grid.tileDir;
            m_terrainSeed       = data.terrain.grid.seed;
            m_terrainSeaLevel   = data.terrain.grid.seaLevel;
        }
    }
    for (size_t i = 0; i < data.objects.size(); ++i)
    {
        const auto* light = world().get<LocalLightComponent>(saved[i]);
        if (!light || !light->emissiveMesh.valid())
            continue;
        for (size_t j = 0; j < saved.size(); ++j)
        {
            if (saved[j].id() == light->emissiveMesh.id())
            {
                data.objects[i].emissiveMeshIndex = static_cast<int>(j);
                break;
            }
        }
    }

    std::string err;
    if (!saveSceneToJson(m_scenePath, data, &err))
    {
        DE_LOG_ERROR("Editor: save failed — {}", err);
        return false;
    }
    DE_LOG_INFO("Editor: saved {} objects → {}", data.objects.size(), m_scenePath.string());
    audio().play2D(m_sfxSave, 0.45f);
    return true;
}

bool EditorApp::loadScene()
{
    if (netSceneLocked())
        return false;
    m_save.setWorldIdentity(m_scenePath.generic_string(), "", "");
    SceneFileData data{};
    std::string err;
    if (!loadSceneFromJson(m_scenePath, data, &err))
    {
        DE_LOG_ERROR("Editor: load failed — {}", err);
        return false;
    }

    clearScene();
    removeEditorTerrain();
    m_authoredWater = {};
    m_authoredExposure = {};
    m_authoredGrass = {};
    if (data.mode != SceneMode::Scene2D)
    {
        applySceneAtmosphere(m_env, data);
        if (data.water.present)
            m_authoredWater = data.water;
        if (data.exposure.present)
            m_authoredExposure = data.exposure;
    }
    m_worldMin  = data.worldMin;
    m_worldMax  = data.worldMax;
    if (data.mode == SceneMode::Scene3D)
    {
        m_ibl.virtualPath  = data.environment;
        m_ibl.intensity    = data.iblIntensity;
        m_ibl.rotationRadY = data.iblRotationRadY;
    }
    applySceneMode(data.mode);
    m_sceneName = data.name.empty() ? (data.mode == SceneMode::Scene2D ? "level2d" : "level") : data.name;
    if (data.mode == SceneMode::Scene2D)
        rebuildGrid2D();

    std::vector<Entity> spawned;
    spawned.reserve(data.objects.size());
    m_suspendLiveStamp = true;
    for (const SceneObjectData& d : data.objects)
    {
        ParticleEmitterDesc pdesc = makeDefaultParticleDesc();
        if (d.hasParticle)
            descFromSceneData(d, pdesc);
        spawned.push_back(spawnObject(d.type, d.position, d.scale, d.rotation, d.color,
                    d.type == SceneObjectType::ParticleEmitter ? &pdesc : nullptr, &d));
    }
    m_suspendLiveStamp = false;
    for (size_t i = 0; i < data.objects.size() && i < spawned.size(); ++i)
    {
        if (!spawned[i].valid())
            continue;
        const SceneObjectType type = data.objects[i].type;
        if (isLocalLightType(type) || isGlobalLightType(type) || type == SceneObjectType::CloudVolume)
            continue;
        stampAuthoredId(world(), spawned[i], m_scenePath.generic_string(), static_cast<int>(i), toString(type));
    }
    for (size_t i = 0; i < data.objects.size() && i < spawned.size(); ++i)
    {
        const int idx = data.objects[i].emissiveMeshIndex;
        if (idx < 0 || idx >= static_cast<int>(spawned.size()) || !spawned[i].valid())
            continue;
        if (auto* light = world().get<LocalLightComponent>(spawned[i]))
            light->emissiveMesh = spawned[static_cast<size_t>(idx)];
    }
    if (m_sceneMode == SceneMode::Scene3D)
    {
        ensureGlobalLights();
        applySkyToLights();
    }
    if (data.mode == SceneMode::Scene3D && data.hasTerrain)
    {
        if (!loadTerrainFromScene(data, m_scenePath))
            DE_LOG_ERROR("Editor: terrain load failed — using ground plane");
    }
    else
        resetFoliageAuthoring();
    if (m_authoredWater.present)
    {
        float level = m_authoredWater.level;
        if (!m_authoredWater.hasLevel && m_haveTerrain && m_terrain.valid())
        {
            const AABox3f terrainBox = m_terrain.bounds();
            level = Lerp(terrainBox.Min.y, terrainBox.Max.y, m_authoredWater.levelFraction);
        }
        m_water.params() = sceneWaterParams(m_authoredWater, level);
        createEditorWaterSheet();
    }
    m_selected = {};
    DE_LOG_INFO("Editor: loaded {} objects", editorObjectCount());
    return true;
}

void EditorApp::handleEditorCommands(float dt)
{
    if (m_queuePlaceAtCursor)
    {
        m_queuePlaceAtCursor = false;
        if (!netClientLocked())
            placeAtCursor(m_placeType);
    }
    if (m_queueGlowProp)
    {
        m_queueGlowProp = false;
        if (!netClientLocked())
            placeGlowProp();
    }
    if (m_queueAssetPlace)
    {
        m_queueAssetPlace = false;
        if (!netClientLocked() && !m_playMode)
            flushQueuedAssetPlace();
    }

    const bool ctrl = input().keyDown(Key::LeftControl) || input().keyDown(Key::RightControl);
    const bool uiKey = m_imgui.wantCaptureKeyboard();
    const bool uiMouse = m_imgui.wantCaptureMouse();

    if (!uiKey && input().actionPressed("quit"))
    {
        if (m_playMode)
        {
            setPlayMode(false);
            return;
        }
        if (m_selected.valid())
        {
            m_selected = {};
            m_dragging = false;
            m_gizmoDragAxis    = TranslateGizmoAxis::None;
            m_gizmoHover       = TranslateGizmoAxis::None;
            m_gizmoHoverEntity = {};
        }
        else
            requestQuit();
        return;
    }

    if (!uiKey)
    {
        if (input().actionPressed("play") && m_sceneMode == SceneMode::Scene3D)
            togglePlayMode();
        const bool shift = input().keyDown(Key::LeftShift) || input().keyDown(Key::RightShift);
        if (m_playMode && shift && input().keyPressed(Key::F5))
        {
            const Save::SaveResult result = m_save.requestSave(Save::SaveKind::Quick, "Quick Save");
            DE_LOG_INFO("Editor: quicksave {}", Save::toString(result));
        }
        else if (m_playMode && shift && input().keyPressed(Key::F9))
        {
            const Save::SaveResult result = m_save.requestLoadNewest(Save::SaveKind::Quick);
            DE_LOG_INFO("Editor: quickload {}", Save::toString(result));
        }
        else if (!m_playMode && (input().keyPressed(Key::F5) || (ctrl && input().keyPressed(Key::S))))
            saveSceneWithDialog();
        if (!m_playMode && (input().keyPressed(Key::F9) || (ctrl && input().keyPressed(Key::O))) && !netSceneLocked())
            loadSceneWithDialog(m_scenePath);
        if (input().actionPressed("toggle_particle_ui"))
            m_showParticlePanel = !m_showParticlePanel;
        if (input().actionPressed("toggle_anim_ui"))
            m_showAnimPanel = !m_showAnimPanel;
        if (input().actionPressed("toggle_hsm_ui"))
            m_showHsmPanel = !m_showHsmPanel;
        if (input().actionPressed("toggle_assets_ui"))
            m_showAssetBrowser = !m_showAssetBrowser;
        if (input().actionPressed("debug_fill"))
        {
            renderer().debugState().cycleFill();
            DE_LOG_INFO("Editor: fill = {}", toString(renderer().debugState().fill));
        }
        if (input().actionPressed("debug_lighting"))
        {
            renderer().debugState().lighting = !renderer().debugState().lighting;
            DE_LOG_INFO("Editor: lighting = {}", renderer().debugState().lighting);
        }
        if (input().actionPressed("debug_shadow_enable"))
        {
            renderer().debugState().shadows = !renderer().debugState().shadows;
            const bool shadowsOn = renderer().debugState().shadows;
            m_shadows.setDebugEnabled(shadowsOn);
            m_scene.localShadows().setDebugEnabled(shadowsOn);
            DE_LOG_INFO("Editor: shadows = {}", shadowsOn);
        }
        if (input().keyPressed(Key::F11) && renderer().hasGBuffer())
        {
            m_showGBuffer = !m_showGBuffer;
            DE_LOG_INFO("Editor: G-buffer overlay = {}", m_showGBuffer);
        }
        if (input().actionPressed("toggle_grid"))
            m_showGrid = !m_showGrid;
        if (!m_playMode && input().actionPressed("toggle_solid"))
            m_showSolid = !m_showSolid;
        if (input().actionPressed("toggle_snap"))
            m_gridSnap = (m_gridSnap > 0.0f) ? 0.0f : 1.0f;
        if (input().actionPressed("select_next"))
            selectNext(+1);
        if (input().actionPressed("select_prev"))
            selectNext(-1);
        if (input().keyPressed(Key::F3))
        {
            if (m_sceneMode == SceneMode::Scene2D)
                applySceneMode(SceneMode::Scene3D);
            else
                applySceneMode(SceneMode::Scene2D);
            if (m_sceneMode == SceneMode::Scene3D)
                ensureGlobalLights();
            DE_LOG_INFO("Editor: mode {}", toString(m_sceneMode));
        }
        if (!m_playMode)
        {
            if (input().actionPressed("type_cube"))
                m_placeType = (m_sceneMode == SceneMode::Scene2D) ? SceneObjectType::Platform : SceneObjectType::Cube;
            if (input().actionPressed("type_sphere"))
                m_placeType = (m_sceneMode == SceneMode::Scene2D) ? SceneObjectType::Coin : SceneObjectType::Sphere;
            if (input().actionPressed("type_particle"))
                m_placeType = (m_sceneMode == SceneMode::Scene2D) ? SceneObjectType::Spawn : SceneObjectType::ParticleEmitter;
            if (m_sceneMode != SceneMode::Scene2D && input().actionPressed("type_point_light"))
                m_placeType = SceneObjectType::PointLight;
            if (m_sceneMode != SceneMode::Scene2D && input().actionPressed("type_spot_light"))
                m_placeType = SceneObjectType::SpotLight;
            if (m_sceneMode != SceneMode::Scene2D && input().actionPressed("type_player"))
                m_placeType = SceneObjectType::Player;
            if (m_sceneMode != SceneMode::Scene2D && input().actionPressed("type_hunter"))
                m_placeType = SceneObjectType::Hunter;
            if (m_sceneMode != SceneMode::Scene2D && input().actionPressed("type_wolf"))
                m_placeType = SceneObjectType::Wolf;
            if (input().actionPressed("cycle_type"))
                cyclePlaceType(+1);
            if (input().actionPressed("cycle_color"))
                cycleSelectedColor();
            if (input().actionPressed("delete") && !netClientLocked())
                deleteSelected();
            if (input().actionPressed("place") && !netClientLocked())
                placeAtCursor(m_placeType);
        }
    }

    if (m_sceneMode == SceneMode::Scene3D && !m_dragging)
    {
        if (uiMouse)
        {
            m_gizmoHoverEntity = {};
            m_gizmoHover       = TranslateGizmoAxis::None;
        }
        else
        {
            const Vector2f mouse(static_cast<float>(input().mouseX()), static_cast<float>(input().mouseY()));
            TranslateGizmoAxis hover = TranslateGizmoAxis::None;
            Entity hoverEnt = pickSelectedGizmo(mouse, hover);
            m_gizmoHoverEntity = hoverEnt;
            m_gizmoHover       = hover;
        }
    }

    if (!m_playMode && !uiMouse && input().mousePressed(MouseButton::Left))
    {
        m_lmbDownX = input().mouseX();
        m_lmbDownY = input().mouseY();
        Entity hit{};
        TranslateGizmoAxis gizmoAxis = TranslateGizmoAxis::None;
        if (m_sceneMode == SceneMode::Scene2D)
        {
            Vector2f p{};
            worldFromMouse2D(p);
            hit = pickObject2D(p);
        }
        else
        {
            const Vector2f mouse(static_cast<float>(input().mouseX()), static_cast<float>(input().mouseY()));
            hit = pickSelectedGizmo(mouse, gizmoAxis);
            if (!hit.valid())
            {
                const Ray3f ray = m_camera.ScreenPointToRay(
                    static_cast<float>(input().mouseX()),
                    static_cast<float>(input().mouseY()),
                    static_cast<float>(renderer().width()),
                    static_cast<float>(renderer().height()));
                hit = pickObject(ray);
            }
        }
        if (hit.valid())
        {
            m_selected = hit;
            m_gizmoDragAxis = TranslateGizmoAxis::None;
            if (m_sceneMode == SceneMode::Scene2D)
            {
                m_dragging = true;
            }
            else if (gizmoAxis != TranslateGizmoAxis::None && !netClientLocked())
            {
                if (const auto* xf = world().get<TransformComponent>(hit))
                {
                    const Ray3f ray = m_camera.ScreenPointToRay(
                        static_cast<float>(input().mouseX()),
                        static_cast<float>(input().mouseY()),
                        static_cast<float>(renderer().width()),
                        static_cast<float>(renderer().height()));
                    Vector3f grab{};
                    if (translateDragPoint(gizmoAxis, ray, xf->position, m_camera.GetLook(), grab))
                    {
                        m_dragging         = true;
                        m_gizmoDragAxis    = gizmoAxis;
                        m_gizmoHover       = gizmoAxis;
                        m_gizmoHoverEntity = hit;
                        m_gizmoDragStart   = xf->position;
                        m_gizmoGrabPoint   = grab;
                    }
                }
            }
            else
            {
                m_dragging = false;
            }
        }
        else
        {
            m_selected = {};
            m_dragging = false;
            m_gizmoDragAxis    = TranslateGizmoAxis::None;
            m_gizmoHover       = TranslateGizmoAxis::None;
            m_gizmoHoverEntity = {};
        }
    }
    if (input().mouseReleased(MouseButton::Left))
    {
        m_dragging      = false;
        m_gizmoDragAxis = TranslateGizmoAxis::None;
    }

    if (!uiMouse && m_dragging && m_selected.valid() && input().mouseDown(MouseButton::Left)
        && !netClientLocked())
    {
        if (m_sceneMode == SceneMode::Scene2D)
        {
            Vector2f p{};
            worldFromMouse2D(p);
            if (m_gridSnap > 0.0f)
            {
                p.x = snap(p.x, m_gridSnap);
                p.y = snap(p.y, m_gridSnap);
            }
            if (auto* xf = world().get<TransformComponent>(m_selected))
            {
                xf->position.x = p.x;
                xf->position.y = p.y;
            }
        }
        else if (m_gizmoDragAxis != TranslateGizmoAxis::None)
        {
            const Ray3f ray = m_camera.ScreenPointToRay(
                static_cast<float>(input().mouseX()),
                static_cast<float>(input().mouseY()),
                static_cast<float>(renderer().width()),
                static_cast<float>(renderer().height()));
            applyGizmoDrag(ray);
        }
        else
        {
            Vector3f hit{};
            if (groundHitFromMouse(hit))
            {
                if (m_gridSnap > 0.0f)
                {
                    hit.x = snap(hit.x, m_gridSnap);
                    hit.z = snap(hit.z, m_gridSnap);
                }
                if (auto* xf = world().get<TransformComponent>(m_selected))
                {
                    xf->position.x = hit.x;
                    xf->position.z = hit.z;
                    EditorObjectComponent* so = findObject(m_selected);
                    if (!keepsPlacedHeight(world(), so, m_selected))
                        xf->position.y = 0.5f * xf->scale.y;
                    syncGlowPairPosition(world(), m_selected);
                }
            }
        }
    }
}

void EditorApp::drawStatusBar()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float          h  = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(UiPalette::kRaised));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##statusbar", nullptr, flags))
    {
        const std::string sceneName = m_scenePath.empty() ? std::string("(unsaved)") : m_scenePath.filename().string();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s  |  %s  |  %d objects  |  place: %s  |  %s%s", sceneName.c_str(), m_sceneMode == SceneMode::Scene2D ? "2D" : "3D",
                    static_cast<int>(editorObjectCount()), toString(m_placeType), netRoleLabel(network().role()),
                    m_playMode ? "  |  PLAY" : "");
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f fps", static_cast<double>(ImGui::GetIO().Framerate));
        const float fpsW = ImGui::CalcTextSize(fps).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - fpsW - 16.0f);
        ImGui::TextUnformatted(fps);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
