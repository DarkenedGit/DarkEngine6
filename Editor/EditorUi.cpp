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

void EditorApp::drawEditorUi()
{
    if (!m_imgui.isReady())
        return;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            const bool sceneOk = !netSceneLocked();
            if (ImGui::MenuItem(ICON_FA_CUBE "  New 3D Scene", nullptr, false, sceneOk))
                newScene3D();
            if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  New 2D Scene", nullptr, false, sceneOk))
                newScene2D();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Scene", "Ctrl+S"))
                saveScene();
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Load Scene", "Ctrl+O", false, sceneOk))
                loadScene();
            if (ImGui::MenuItem(ICON_FA_FILE "  Open 3D Level", nullptr, false, sceneOk))
            {
                m_scenePath = defaultScenePath("level.json");
                loadScene();
            }
            if (ImGui::MenuItem(ICON_FA_FILE "  Open 2D Level", nullptr, false, sceneOk))
            {
                m_scenePath = defaultScenePath("level2d.json");
                loadScene();
            }
            if (!sceneOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Disconnect before changing the scene");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Quit", "Esc"))
                requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            bool mode2d = m_sceneMode == SceneMode::Scene2D;
            if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  2D Scene", "F3", mode2d))
                applySceneMode(mode2d ? SceneMode::Scene3D : SceneMode::Scene2D);
            ImGui::MenuItem(ICON_FA_BOLT "  Particle Panel", "F2", &m_showParticlePanel);
            ImGui::MenuItem(ICON_FA_EYE "  Grid", nullptr, &m_showGrid);
            ImGui::MenuItem(ICON_FA_CUBE "  Solid Ground", nullptr, &m_showSolid);
            if (renderer().hasSceneBuffers())
            {
                bool aces = renderer().debugState().aces;
                if (ImGui::MenuItem("ACES Tonemap", nullptr, aces))
                    renderer().debugState().aces = !aces;
            }
            if (renderer().hasGBuffer())
            {
                ImGui::MenuItem("G-buffer Tiles", "F11", &m_showGBuffer);
                ImGui::MenuItem("Velocity Tile", nullptr, &m_showVelocity);
                ImGui::MenuItem("Bloom", nullptr, &renderer().debugState().bloom);
                ImGui::MenuItem("TAA", nullptr, &renderer().debugState().taa);
                ImGui::MenuItem("Motion Blur", nullptr, &renderer().debugState().motionBlur);
                ImGui::MenuItem("Local Lights", nullptr, &renderer().debugState().localLights);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
        {
            const bool createOk = !netClientLocked();
            if (m_sceneMode == SceneMode::Scene2D)
            {
                if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Platform", "1+P", false, createOk))
                {
                    m_placeType = SceneObjectType::Platform;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE "  Coin", "2+P", false, createOk))
                {
                    m_placeType = SceneObjectType::Coin;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE_PLUS "  Player Spawn", "3+P", false, createOk))
                {
                    m_placeType = SceneObjectType::Spawn;
                    placeAtCursor(m_placeType);
                }
            }
            else
            {
                if (ImGui::MenuItem(ICON_FA_CUBE "  Cube", "1+P", false, createOk))
                {
                    m_placeType = SceneObjectType::Cube;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE "  Sphere", "2+P", false, createOk))
                {
                    m_placeType = SceneObjectType::Sphere;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_BOLT "  Particle Emitter", "3+P", false, createOk))
                {
                    m_placeType = SceneObjectType::ParticleEmitter;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_LIGHTBULB "  Point Light", "4+P", false, createOk))
                {
                    m_placeType = SceneObjectType::PointLight;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_LIGHTBULB "  Spot Light", "5+P", false, createOk))
                {
                    m_placeType = SceneObjectType::SpotLight;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem(ICON_FA_BOLT "  Glow Prop", nullptr, false, createOk))
                    placeGlowProp();
            }
            if (!createOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Spectators cannot place or delete objects");
            ImGui::EndMenu();
        }
        drawNetworkMenu();
        drawDebugMenu();
        ImGui::EndMainMenuBar();
    }

    beginPassthruDockSpace("EditorDockHost", ImGui::GetFrameHeight());

    if (m_showParticlePanel)
    {
        if (ParticleEmitter* em = selectedEmitter())
        {
            m_particlePanel.draw(*em, &m_showParticlePanel);
            uint32_t burst = 0;
            if (m_particlePanel.consumeBurstRequest(burst))
                em->emitBurst(burst);
            if (const auto* xf = world().get<TransformComponent>(m_selected))
                em->setTransform(xf->position, xf->rotation);
        }
        else
        {
            if (ImGui::Begin("Particle System", &m_showParticlePanel))
            {
                ImGui::TextWrapped(
                    "Select a particle emitter in the scene (place with Create menu or key 3 then P), "
                    "or create one below.");
                if (ImGui::Button(ICON_FA_BOLT "  Create Emitter at Cursor") && !netClientLocked())
                {
                    m_placeType = SceneObjectType::ParticleEmitter;
                    placeAtCursor(m_placeType);
                }
                ImGui::Separator();
                ImGui::Text("Place type: %s", toString(m_placeType));
            }
            ImGui::End();
        }
    }

    if (ImGui::Begin("Scene"))
    {
        ImGui::TextUnformatted(m_sceneMode == SceneMode::Scene2D ? "Mode: 2D" : "Mode: 3D");
        ImGui::TextWrapped("%s", m_scenePath.string().c_str());
        ImGui::Separator();
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            ImGui::PushID(static_cast<int>(e.id()));
            char label[128];
            std::snprintf(label, sizeof(label), "%s##%u", toString(so.type), e.id());
            if (ImGui::Selectable(label, selected))
                m_selected = e;
            ImGui::PopID();
        });
    }
    ImGui::End();

    if (m_sceneMode == SceneMode::Scene2D && ImGui::Begin("2D Level"))
    {
        ImGui::Text("MMB/RMB drag to pan, wheel to zoom, P to place.");
        float wmin[2] = { m_worldMin.x, m_worldMin.y };
        float wmax[2] = { m_worldMax.x, m_worldMax.y };
        if (ImGui::DragFloat2("World min", wmin, 0.25f))
        {
            m_worldMin = Vector2f(wmin[0], wmin[1]);
            rebuildGrid2D();
        }
        if (ImGui::DragFloat2("World max", wmax, 0.25f))
        {
            m_worldMax = Vector2f(wmax[0], wmax[1]);
            rebuildGrid2D();
        }
        ImGui::SliderFloat("Snap", &m_gridSnap, 0.0f, 4.0f, "%.2f");
        ImGui::Separator();
        if (EditorObjectComponent* so = findObject(m_selected))
        {
            if (auto* xf = world().get<TransformComponent>(m_selected))
            {
                ImGui::Text("Selected: %s", toString(so->type));
                float pos[2] = { xf->position.x, xf->position.y };
                float size[2] = { xf->scale.x, xf->scale.y };
                if (ImGui::DragFloat2("Position", pos, 0.05f) && !netClientLocked())
                {
                    xf->position.x = pos[0];
                    xf->position.y = pos[1];
                }
                if (ImGui::DragFloat2("Size", size, 0.05f, 0.05f, 200.0f) && !netClientLocked())
                {
                    xf->scale.x = Max(0.05f, size[0]);
                    xf->scale.y = Max(0.05f, size[1]);
                }
                ImGui::ColorEdit3("Tint", so->color);
                if (ImGui::Button(ICON_FA_TRASH "  Delete") && !netClientLocked())
                    deleteSelected();
            }
        }
        else
        {
            ImGui::TextUnformatted("No selection. Click an object or press P to place.");
        }
        ImGui::End();
    }

    if (m_sceneMode == SceneMode::Scene3D)
        drawInspector3D();

    drawStatusBar();
}

void EditorApp::drawInspector3D()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();
        return;
    }

    EditorObjectComponent* so = findObject(m_selected);
    if (!so)
    {
        ImGui::TextUnformatted("No selection. Click an object or press 4+P / 5+P to place a light.");
        ImGui::End();
        return;
    }

    auto* xf = world().get<TransformComponent>(m_selected);
    if (!xf)
    {
        ImGui::TextUnformatted("Selected entity has no transform.");
        ImGui::End();
        return;
    }

    const bool locked = netClientLocked();
    ImGui::Text("Selected: %s", toString(so->type));
    ImGui::BeginDisabled(locked);
    float pos[3] = { xf->position.x, xf->position.y, xf->position.z };
    if (ImGui::DragFloat3("Position", pos, 0.05f))
    {
        xf->position.x = pos[0];
        xf->position.y = pos[1];
        xf->position.z = pos[2];
        syncGlowPairPosition(world(), m_selected);
    }

    if (auto* light = world().get<LocalLightComponent>(m_selected))
    {
        ImGui::Separator();
        ImGui::Checkbox("Enabled", &light->enabled);
        if (ImGui::ColorEdit3("Color", &light->color.x))
        {
            so->color[0] = light->color.x;
            so->color[1] = light->color.y;
            so->color[2] = light->color.z;
        }
        ImGui::DragFloat("Intensity (cd)", &light->intensity, 10.0f, 0.0f, 50000.0f);
        ImGui::SliderFloat("Range (m)", &light->range, 0.25f, 80.0f);
        if (light->type == LocalLightType::Spot)
        {
            if (ImGui::SliderFloat("Inner (deg)", &light->innerConeDeg, 0.0f, 80.0f) && light->innerConeDeg > light->outerConeDeg)
                light->outerConeDeg = light->innerConeDeg;
            if (ImGui::SliderFloat("Outer (deg)", &light->outerConeDeg, 0.0f, 80.0f) && light->innerConeDeg > light->outerConeDeg)
                light->innerConeDeg = light->outerConeDeg;
            float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
            eulerXYZFromQuat(xf->rotation, pitch, yaw, roll);
            float eulerDeg[3] = {
                RadiansToDegrees(pitch),
                RadiansToDegrees(yaw),
                RadiansToDegrees(roll)
            };
            if (ImGui::DragFloat3("Euler (deg)", eulerDeg, 0.5f))
            {
                xf->rotation = Quaternion::FromEulerXYZ(
                    DegreesToRadians(eulerDeg[0]),
                    DegreesToRadians(eulerDeg[1]),
                    DegreesToRadians(eulerDeg[2]));
            }
            if (ImGui::Button("Aim at camera"))
            {
                Vector3f dir = m_camera.GetPosition() - xf->position;
                if (dir.MagnitudeSqrd() <= 1.0e-8f)
                    dir = m_camera.GetLook();
                else
                    dir.Normalize();
                const Vector3f up = (fabsf(dir.y) > 0.9f) ? Vector3f(Vector3f::X_AXIS) : Vector3f(Vector3f::Y_AXIS);
                xf->rotation      = Quaternion::FromLookRotation(dir, up);
            }
        }
        ImGui::DragFloat("Source radius", &light->sourceRadius, 0.005f, 0.0f, 2.0f);
    }
    else
    {
        ImGui::ColorEdit3("Tint", so->color);
        if (auto* mc = world().get<MeshComponent>(m_selected))
            ImGui::SliderFloat("Emissive", &mc->emissive, 0.0f, 1.0f);
    }

    if (ImGui::Button(ICON_FA_TRASH "  Delete"))
        deleteSelected();
    ImGui::EndDisabled();

    ImGui::End();
}

void EditorApp::onUpdate(float dt)
{
    const NetRole role = network().role();
    if (role == NetRole::Joining && m_lastNetRole != NetRole::Joining)
        discardLocalSceneForJoin();
    m_lastNetRole = role;

    updateCamera(dt);
    handleEditorCommands(dt);

    if (m_sceneMode != SceneMode::Scene2D)
        tickAnimGraphs(world(), assets(), dt);

    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::ParticleEmitter || so.emitterIndex < 0)
            return;
        if (so.emitterIndex >= static_cast<int>(m_emitters.size()) || !m_emitters[static_cast<size_t>(so.emitterIndex)])
            return;
        if (const auto* xf = world().get<TransformComponent>(e))
            m_emitters[static_cast<size_t>(so.emitterIndex)]->setTransform(xf->position, xf->rotation);
        m_emitters[static_cast<size_t>(so.emitterIndex)]->update(dt);
    });

    Audio::AudioListener lis{};
    if (m_sceneMode == SceneMode::Scene2D)
    {
        lis.position = Vector3f(m_camera2D.GetPosition().x, m_camera2D.GetPosition().y, 0.0f);
        lis.forward  = Vector3f(0.0f, 0.0f, 1.0f);
        lis.up       = Vector3f(0.0f, 1.0f, 0.0f);
    }
    else
    {
        lis.position = m_camera.GetPosition();
        lis.forward  = m_camera.GetLook();
        lis.up       = m_camera.GetUp();
    }
    audio().setListener(lis);
}
