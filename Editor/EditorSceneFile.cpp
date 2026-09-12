#include "EditorApp.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
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

bool EditorApp::saveScene()
{
    SceneFileData data{};
    data.version  = 2;
    data.name     = m_sceneName;
    data.mode     = m_sceneMode;
    data.worldMin = m_worldMin;
    data.worldMax = m_worldMax;

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
        if (so.type == SceneObjectType::ParticleEmitter && so.emitterIndex >= 0
            && so.emitterIndex < static_cast<int>(m_emitters.size())
            && m_emitters[static_cast<size_t>(so.emitterIndex)])
        {
            sceneDataFromDesc(m_emitters[static_cast<size_t>(so.emitterIndex)]->desc(), d);
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
            d.color[0]          = light->color.x;
            d.color[1]          = light->color.y;
            d.color[2]          = light->color.z;
        }
        if (const auto* mc = world().get<MeshComponent>(e))
            d.emissive = mc->emissive;
        data.objects.push_back(d);
        saved.push_back(e);
    });
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
    SceneFileData data{};
    std::string err;
    if (!loadSceneFromJson(m_scenePath, data, &err))
    {
        DE_LOG_ERROR("Editor: load failed — {}", err);
        return false;
    }

    clearScene();
    m_worldMin  = data.worldMin;
    m_worldMax  = data.worldMax;
    applySceneMode(data.mode);
    m_sceneName = data.name.empty() ? (data.mode == SceneMode::Scene2D ? "level2d" : "level") : data.name;
    if (data.mode == SceneMode::Scene2D)
        rebuildGrid2D();

    std::vector<Entity> spawned;
    spawned.reserve(data.objects.size());
    for (const SceneObjectData& d : data.objects)
    {
        ParticleEmitterDesc pdesc = makeDefaultParticleDesc();
        if (d.hasParticle)
            descFromSceneData(d, pdesc);
        spawned.push_back(spawnObject(d.type, d.position, d.scale, d.rotation, d.color,
                    d.type == SceneObjectType::ParticleEmitter ? &pdesc : nullptr, &d));
    }
    for (size_t i = 0; i < data.objects.size() && i < spawned.size(); ++i)
    {
        const int idx = data.objects[i].emissiveMeshIndex;
        if (idx < 0 || idx >= static_cast<int>(spawned.size()) || !spawned[i].valid())
            continue;
        if (auto* light = world().get<LocalLightComponent>(spawned[i]))
            light->emissiveMesh = spawned[static_cast<size_t>(idx)];
    }
    m_selected = {};
    DE_LOG_INFO("Editor: loaded {} objects", editorObjectCount());
    return true;
}

void EditorApp::handleEditorCommands(float dt)
{
    (void)dt;
    const bool ctrl = input().keyDown(Key::LeftControl) || input().keyDown(Key::RightControl);
    const bool uiKey = m_imgui.wantCaptureKeyboard();
    const bool uiMouse = m_imgui.wantCaptureMouse();

    if (!uiKey && input().actionPressed("quit"))
    {
        if (m_selected.valid())
        {
            m_selected = {};
            m_dragging = false;
        }
        else
            requestQuit();
        return;
    }

    if (!uiKey)
    {
        if (input().keyPressed(Key::F5) || (ctrl && input().keyPressed(Key::S)))
            saveScene();
        if ((input().keyPressed(Key::F9) || (ctrl && input().keyPressed(Key::O))) && !netSceneLocked())
            loadScene();
        if (input().actionPressed("toggle_particle_ui"))
            m_showParticlePanel = !m_showParticlePanel;
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
            m_shadows.setDebugEnabled(renderer().debugState().shadows);
            DE_LOG_INFO("Editor: shadows = {}", renderer().debugState().shadows);
        }
        if (input().keyPressed(Key::F11) && renderer().hasGBuffer())
        {
            m_showGBuffer = !m_showGBuffer;
            DE_LOG_INFO("Editor: G-buffer overlay = {}", m_showGBuffer);
        }
        if (input().actionPressed("toggle_grid"))
            m_showGrid = !m_showGrid;
        if (input().actionPressed("toggle_solid"))
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
            DE_LOG_INFO("Editor: mode {}", toString(m_sceneMode));
        }
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
        if (input().actionPressed("cycle_type"))
            cyclePlaceType(+1);
        if (input().actionPressed("cycle_color"))
            cycleSelectedColor();
        if (input().actionPressed("delete") && !netClientLocked())
            deleteSelected();
        if (input().actionPressed("place") && !netClientLocked())
            placeAtCursor(m_placeType);
    }

    if (!uiMouse && input().mousePressed(MouseButton::Left))
    {
        m_lmbDownX = input().mouseX();
        m_lmbDownY = input().mouseY();
        Entity hit{};
        if (m_sceneMode == SceneMode::Scene2D)
        {
            Vector2f p{};
            worldFromMouse2D(p);
            hit = pickObject2D(p);
        }
        else
        {
            const Ray3f ray = m_camera.ScreenPointToRay(
                static_cast<float>(input().mouseX()),
                static_cast<float>(input().mouseY()),
                static_cast<float>(renderer().width()),
                static_cast<float>(renderer().height()));
            hit = pickObject(ray);
        }
        if (hit.valid())
        {
            m_selected = hit;
            m_dragging = true;
        }
        else
        {
            m_selected = {};
            m_dragging = false;
        }
    }
    if (input().mouseReleased(MouseButton::Left))
        m_dragging = false;

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
                    if (so && so->type == SceneObjectType::ParticleEmitter)
                        xf->position.y = 0.5f;
                    else if (!keepsPlacedHeight(world(), so, m_selected))
                        xf->position.y = 0.5f * xf->scale.y;
                    syncGlowPairPosition(world(), m_selected);

                    if (so && so->emitterIndex >= 0 && so->emitterIndex < static_cast<int>(m_emitters.size())
                        && m_emitters[static_cast<size_t>(so->emitterIndex)])
                    {
                        m_emitters[static_cast<size_t>(so->emitterIndex)]->setTransform(xf->position, xf->rotation);
                    }
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
        ImGui::Text("%s  |  %s  |  %d objects  |  place: %s  |  %s", sceneName.c_str(), m_sceneMode == SceneMode::Scene2D ? "2D" : "3D",
                    static_cast<int>(editorObjectCount()), toString(m_placeType), netRoleLabel(network().role()));
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
