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
#include "Render/DebugRenderState.h"
#include "Math/MathDefines.h"
#include "Assets/Model.h"
#include "Assets/Material.h"
#include "Particles/ParticleMaterials.h"
#include "Core/EntityPins.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Character/HealthComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/StatusDot.h"
#include "Particles/ParticleTick.h"
#include "Particles/StatusFxDriver.h"

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
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CUBE "  Load Model...", nullptr, false, sceneOk))
                loadGltfModel();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Model", nullptr, selectedModel() != nullptr))
                saveGltfModel();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Model As...", nullptr, selectedModel() != nullptr))
                saveGltfModelAs();
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
            {
                applySceneMode(mode2d ? SceneMode::Scene3D : SceneMode::Scene2D);
                if (m_sceneMode == SceneMode::Scene3D)
                    ensureGlobalLights();
            }
            ImGui::MenuItem(ICON_FA_BOLT "  Particle Panel", "F2", &m_showParticlePanel);
            ImGui::MenuItem(ICON_FA_PLAY "  Animation Panel", "F4", &m_showAnimPanel);
            ImGui::MenuItem(ICON_FA_LIST "  HSM Panel", "F8", &m_showHsmPanel);
            ImGui::MenuItem(ICON_FA_CUBE "  Model Parts", nullptr, &m_showModelParts);
            ImGui::MenuItem(ICON_FA_CUBE "  Material", nullptr, &m_showMaterialEditor);
            ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Terrain", nullptr, &m_showTerrainPanel);
            ImGui::SliderFloat(ICON_FA_GAUGE_HIGH "  Fly speed (m/s)", &m_moveSpeed, 1.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("WASD / climb. Shift still multiplies by 2.5. Default 8 for models.");
            ImGui::MenuItem(ICON_FA_EYE "  Grid", nullptr, &m_showGrid);
            ImGui::MenuItem(ICON_FA_CUBE "  Solid Ground", nullptr, &m_showSolid);
            if (renderer().hasSceneBuffers())
            {
                bool aces = renderer().debugState().aces;
                if (ImGui::MenuItem("ACES Tonemap", nullptr, aces))
                    renderer().debugState().aces = !aces;
            }
            bool legacyAlbedo = renderer().debugState().legacyUnormAlbedo;
            if (ImGui::MenuItem("Legacy UNORM albedo", nullptr, legacyAlbedo))
            {
                renderer().debugState().legacyUnormAlbedo = !legacyAlbedo;
                renderer().gpuResources().setAlbedoSamplingRaw(renderer().debugState().legacyUnormAlbedo);
                if (m_haveTerrain && m_terrainMaterial.isValid())
                    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
            }
            if (renderer().hasGBuffer())
            {
                bool albedoLinear = renderer().debugState().showAlbedoLinear;
                if (ImGui::MenuItem("Show albedo (linear)", nullptr, albedoLinear))
                    renderer().debugState().showAlbedoLinear = !albedoLinear;
                bool albedoRaw = renderer().debugState().showAlbedoRaw;
                if (ImGui::MenuItem("Show albedo (raw UNORM)", nullptr, albedoRaw))
                {
                    renderer().debugState().showAlbedoRaw = !albedoRaw;
                    renderer().setLightingAlbedoRaw(renderer().debugState().showAlbedoRaw);
                }
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
                    m_placeType          = SceneObjectType::Platform;
                    m_queuePlaceAtCursor = true;
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE "  Coin", "2+P", false, createOk))
                {
                    m_placeType          = SceneObjectType::Coin;
                    m_queuePlaceAtCursor = true;
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE_PLUS "  Player Spawn", "3+P", false, createOk))
                {
                    m_placeType          = SceneObjectType::Spawn;
                    m_queuePlaceAtCursor = true;
                }
            }
            else
            {
                if (ImGui::MenuItem(ICON_FA_CUBE "  Cube", "1+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::Cube;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE "  Sphere", "2+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::Sphere;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_BOLT "  Particle Emitter", "3+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::ParticleEmitter;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_LIGHTBULB "  Point Light", "4+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::PointLight;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_LIGHTBULB "  Spot Light", "5+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::SpotLight;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_CIRCLE_PLUS "  Player", "6+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::Player;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_BUG "  Hunter", "7+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::Hunter;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_BOLT "  Glow Prop", nullptr, false, createOk))
                    m_queueGlowProp = true;
            }
            if (!createOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Spectators cannot place or delete objects");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play"))
        {
            const bool playOk = m_sceneMode == SceneMode::Scene3D && !netClientLocked();
            if (ImGui::MenuItem(m_playMode ? ICON_FA_PAUSE "  Stop Play" : ICON_FA_PLAY "  Play Scene", "F12", m_playMode, playOk))
                togglePlayMode();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Possess the Player. WASD move, mouse look, Space jump, LMB attack.");
            ImGui::EndMenu();
        }
        drawNetworkMenu();
        drawDebugMenu();
        ImGui::EndMainMenuBar();
    }

    beginPassthruDockSpace("EditorDockHost", ImGui::GetFrameHeight());

    if (m_sceneMode == SceneMode::Scene3D && renderer().hasGBuffer())
    {
        ImGui::SetNextWindowSize(ImVec2(320.0f, 200.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IBL"))
        {
            DebugRenderState& iblDbg = renderer().debugState();
            if (ImGui::Checkbox("Enabled", &iblDbg.iblEnabled))
                DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
            ImGui::SliderFloat("Intensity", &m_ibl.intensity, 0.0f, 4.0f, "%.2f");
            float rotDeg = m_ibl.rotationRadY * Math::RadToDeg;
            if (ImGui::SliderFloat("Rotation", &rotDeg, -180.0f, 180.0f, "%.1f deg"))
                m_ibl.rotationRadY = rotDeg * Math::DegToRad;
            const char* debugViews[] = { "Off", "Irradiance", "Prefilter lod0", "LUT" };
            if (ImGui::Combo("Debug view", &iblDbg.iblDebug, debugViews, 4))
            {
                if (iblDbg.iblDebug < 0)
                    iblDbg.iblDebug = 0;
                if (iblDbg.iblDebug > 3)
                    iblDbg.iblDebug = 3;
                DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
            }
            ImGui::TextUnformatted(m_ibl.virtualPath.empty() ? "(off)" : m_ibl.virtualPath.c_str());
            ImGui::TextDisabled("Bake %s", m_ibl.enabled ? "ready" : "off / failed");
        }
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SSAO"))
        {
            DebugRenderState& ssaoDbg = renderer().debugState();
            if (ImGui::Checkbox("Enabled", &ssaoDbg.ssaoEnabled))
            {
                m_ssao.enabled = ssaoDbg.ssaoEnabled;
                DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
            }
            ImGui::SliderFloat("Intensity", &m_ssao.intensity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Radius", &m_ssao.radius, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("Power", &m_ssao.power, 0.5f, 4.0f, "%.2f");
            const char* ssaoDebugViews[] = { "Off", "SSAO factor" };
            if (ImGui::Combo("Debug view", &ssaoDbg.ssaoDebug, ssaoDebugViews, 2))
            {
                if (ssaoDbg.ssaoDebug < 0)
                    ssaoDbg.ssaoDebug = 0;
                if (ssaoDbg.ssaoDebug > 1)
                    ssaoDbg.ssaoDebug = 1;
                DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
            }
        }
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(320.0f, 280.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SSR"))
        {
            DebugRenderState& ssrDbg = renderer().debugState();
            if (ImGui::Checkbox("Enabled", &ssrDbg.ssrEnabled))
            {
                m_ssr.enabled = ssrDbg.ssrEnabled;
                DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
            }
            ImGui::SliderFloat("Max roughness", &m_ssr.maxRoughness, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Thickness", &m_ssr.thickness, 0.02f, 2.0f, "%.2f m");
            ImGui::SliderFloat("Stride", &m_ssr.stride, 1.0f, 8.0f, "%.1f px");
            ImGui::SliderFloat("Edge fade", &m_ssr.edgeFade, 0.0f, 0.25f, "%.2f");
            const char* ssrDebugViews[] = { "Off", "Radiance", "Confidence" };
            if (ImGui::Combo("Debug view", &ssrDbg.ssrDebug, ssrDebugViews, 3))
            {
                if (ssrDbg.ssrDebug < 0)
                    ssrDbg.ssrDebug = 0;
                if (ssrDbg.ssrDebug > 2)
                    ssrDbg.ssrDebug = 2;
                DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
            }
        }
        ImGui::End();
    }

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
            if (ParticleEmitterComponent* pe = world().get<ParticleEmitterComponent>(m_selected))
            {
                const bool ribbon = em->desc().renderMode == ParticleEmitterDesc::RenderMode::Ribbon;
                AssetRef<Material> want  = internParticleSpriteMaterial(assets(), ribbon);
                AssetRef<Material> other = internParticleSpriteMaterial(assets(), !ribbon);
                if (want && (pe->matAssetID == NULL_ASSET || (other && pe->matAssetID == other->id)))
                    setParticleMaterial(world(), pins(), assets(), m_selected, want->id);
            }
        }
        else
        {
            if (ImGui::Begin("Particle System", &m_showParticlePanel))
            {
                ImGui::TextWrapped(
                    "Select a particle emitter in the scene (place with Create menu or key 3 then P), "
                    "drag the RGB arrows to move it, or create one below.");
                if (ImGui::Button(ICON_FA_BOLT "  Create Emitter at Cursor") && !netClientLocked())
                {
                    m_placeType          = SceneObjectType::ParticleEmitter;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::Separator();
                ImGui::Text("Place type: %s", toString(m_placeType));
            }
            ImGui::End();
        }
    }

    if (m_showAnimPanel)
    {
        if (AnimGraphComponent* ag = selectedAnimGraph())
            m_animPanel.draw(*ag, assets(), &m_showAnimPanel);
        else
        {
            if (ImGui::Begin("Animation", &m_showAnimPanel))
            {
                ImGui::TextWrapped("Select a skinned glTF (File → Load Model) to preview clips and edit *.anim.json.");
                const ModelComponent* mc = m_selected.valid() ? world().get<ModelComponent>(m_selected) : nullptr;
                const AssetRef<Model> model = mc ? assets().getAs<Model>(mc->modelAssetID) : AssetRef<Model>{};
                if (model && model->skeleton())
                {
                    if (ImGui::Button(ICON_FA_PLAY "  Attach animation graph"))
                        ensureAnimGraphOnSelected();
                }
                else if (model)
                    ImGui::TextDisabled("Selected model has no skeleton.");
            }
            ImGui::End();
        }
    }

    if (m_showHsmPanel)
        m_hsmPanel.draw(assets(), &m_showHsmPanel);

    if (ImGui::Begin("Scene"))
    {
        ImGui::TextUnformatted(m_sceneMode == SceneMode::Scene2D ? "Mode: 2D" : "Mode: 3D");
        ImGui::TextWrapped("%s", m_scenePath.string().c_str());
        ImGui::Separator();
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            ImGui::PushID(static_cast<int>(e.id()));
            char label[128];
            const char* name = toString(so.type);
            if (so.type == SceneObjectType::AmbientLight)
                name = "Ambient Light";
            else if (so.type == SceneObjectType::DirectionalLight)
                name = "Directional Light";
            else if (so.type == SceneObjectType::PointLight)
                name = "Point Light";
            else if (so.type == SceneObjectType::SpotLight)
                name = "Spot Light";
            std::snprintf(label, sizeof(label), "%s##%u", name, e.id());
            if (ImGui::Selectable(label, selected))
                m_selected = e;
            ImGui::PopID();
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (world().has<EditorObjectComponent>(e))
                return;
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            ImGui::PushID(static_cast<int>(e.id()) + 100000);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            const std::string name = (model && !model->sourcePath().empty())
                ? model->sourcePath().filename().string()
                : std::string("glTF");
            char label[160];
            std::snprintf(label, sizeof(label), "Model  %s##%u", name.c_str(), e.id());
            if (ImGui::Selectable(label, selected))
            {
                m_selected           = e;
                m_showModelParts     = true;
                m_showMaterialEditor = true;
            }
            ImGui::PopID();
        });
    }
    ImGui::End();

    drawModelPartsPanel();
    drawMaterialPanel();
    drawTerrainPanel();

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
    {
        drawInspector3D();
        if (!m_playMode)
            drawTranslateGizmos();
        if (m_playMode)
            drawPlayHud();
    }

    drawStatusBar();
}

void EditorApp::drawInspector3D()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();
        return;
    }

    if (const ModelComponent* mc = m_selected.valid() ? world().get<ModelComponent>(m_selected) : nullptr)
    {
        auto* xf = world().get<TransformComponent>(m_selected);
        const auto model = assets().getAs<Model>(mc->modelAssetID);
        ImGui::TextUnformatted("Selected: glTF Model");
        if (model)
            ImGui::TextWrapped("%s", model->sourcePath().string().c_str());
        if (xf)
        {
            float pos[3] = { xf->position.x, xf->position.y, xf->position.z };
            if (ImGui::DragFloat3("Position", pos, 0.05f))
            {
                xf->position.x = pos[0];
                xf->position.y = pos[1];
                xf->position.z = pos[2];
            }
            float scale[3] = { xf->scale.x, xf->scale.y, xf->scale.z };
            if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.01f, 100.0f))
            {
                xf->scale.x = Max(0.01f, scale[0]);
                xf->scale.y = Max(0.01f, scale[1]);
                xf->scale.z = Max(0.01f, scale[2]);
            }
        }
        if (ImGui::Button("Open Parts / Material"))
        {
            m_showModelParts     = true;
            m_showMaterialEditor = true;
        }
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

    const bool locked   = netClientLocked();
    const bool envLight = isGlobalLightType(so->type);
    ImGui::Text("Selected: %s", toString(so->type));
    if (so->type == SceneObjectType::Player || so->type == SceneObjectType::Hunter)
    {
        if (HealthComponent* hp = world().get<HealthComponent>(m_selected))
            ImGui::Text("HP  %.0f / %.0f", static_cast<double>(hp->health.hp()), static_cast<double>(hp->health.maxHp()));
        if (JumpAttackComponent* jac = world().get<JumpAttackComponent>(m_selected))
            ImGui::Text("Jump cooldown  %.2fs", static_cast<double>(jac->jump.cooldownLeft()));
        if (so->type == SceneObjectType::Player && ImGui::Button(ICON_FA_PLAY "  Play from here") && !m_playMode)
            togglePlayMode();
        ImGui::TextDisabled("F12 plays the scene. Place hunters, then Play to fight.");
    }
    ImGui::BeginDisabled(locked);
    float pos[3] = { xf->position.x, xf->position.y, xf->position.z };
    if (ImGui::DragFloat3("Position", pos, 0.05f))
    {
        xf->position.x = pos[0];
        xf->position.y = pos[1];
        xf->position.z = pos[2];
        syncGlowPairPosition(world(), m_selected);
    }

    if (auto* amb = world().get<AmbientLightComponent>(m_selected))
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Ambient Light");
        ImGui::Checkbox("Enabled", &amb->enabled);
        if (ImGui::ColorEdit3("Color", &amb->color.x))
        {
            so->color[0] = amb->color.x;
            so->color[1] = amb->color.y;
            so->color[2] = amb->color.z;
        }
        ImGui::DragFloat("Intensity", &amb->intensity, 0.01f, 0.0f, 8.0f);
        ImGui::TextDisabled("Fills the whole scene. Position is only a handle.");
    }
    else if (auto* sun = world().get<DirectionalLightComponent>(m_selected))
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Directional Light");
        ImGui::Checkbox("Enabled", &sun->enabled);
        if (ImGui::ColorEdit3("Color", &sun->color.x))
        {
            so->color[0] = sun->color.x;
            so->color[1] = sun->color.y;
            so->color[2] = sun->color.z;
        }
        ImGui::DragFloat("Intensity", &sun->intensity, 0.01f, 0.0f, 8.0f);
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
        if (ImGui::Button("Aim down"))
            xf->rotation = Quaternion::FromLookRotation(Vector3f(0.0f, -1.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f));
        ImGui::SameLine();
        if (ImGui::Button("Reset sun"))
            xf->rotation = defaultDirectionalRotation();
        ImGui::SameLine();
        if (ImGui::Button("Aim at camera"))
        {
            Vector3f dirVec = m_camera.GetPosition() - xf->position;
            if (dirVec.MagnitudeSqrd() <= 1.0e-8f)
                dirVec = m_camera.GetLook();
            else
                dirVec.Normalize();
            const Vector3f up = (fabsf(dirVec.y) > 0.9f) ? Vector3f(Vector3f::X_AXIS) : Vector3f(Vector3f::Y_AXIS);
            xf->rotation      = Quaternion::FromLookRotation(dirVec, up);
        }
        ImGui::TextDisabled("Direction is +Z of this rotation. Shadows follow it.");
    }
    else if (world().get<ParticleEmitterComponent>(m_selected))
    {
        if (ImGui::Button("Open Material"))
            m_showMaterialEditor = true;
        ImGui::TextDisabled("Sprite albedo is on the Material panel. Emitter colors stay in Particle System.");
        if (auto* light = world().get<LocalLightComponent>(m_selected))
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Nearby illumination");
            ImGui::Checkbox("Enabled", &light->enabled);
            if (ImGui::ColorEdit3("Light color", &light->color.x))
            {
                so->color[0] = light->color.x;
                so->color[1] = light->color.y;
                so->color[2] = light->color.z;
            }
            ImGui::DragFloat("Intensity (cd)", &light->intensity, 10.0f, 0.0f, 50000.0f);
            ImGui::SliderFloat("Range (m)", &light->range, 0.25f, 80.0f);
            ImGui::TextDisabled("Point light on this emitter. Lights the ground and nearby meshes.");
        }
    }
    else if (auto* light = world().get<LocalLightComponent>(m_selected))
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
            if (ImGui::Button("Aim down"))
                xf->rotation = defaultSpotRotation();
            ImGui::SameLine();
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
    else if (!isPawnType(so->type))
    {
        if (ImGui::ColorEdit3("Tint", so->color))
        {
            if (AssetRef<Material> mat = ensureUniqueMeshMaterial(m_selected))
                mat->setBaseColor(so->color[0], so->color[1], so->color[2], mat->baseColor()[3]);
        }
        if (auto* mc = world().get<MeshComponent>(m_selected))
            ImGui::SliderFloat("Emissive", &mc->emissive, 0.0f, 1.0f);
        if (world().get<MeshComponent>(m_selected) && ImGui::Button("Open Material"))
            m_showMaterialEditor = true;
    }

    if (envLight)
    {
        ImGui::BeginDisabled();
        ImGui::Button(ICON_FA_TRASH "  Delete");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Ambient and directional lights are part of the scene");
        ImGui::EndDisabled();
    }
    else if (ImGui::Button(ICON_FA_TRASH "  Delete"))
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

    handleEditorCommands(dt);
    if (m_playMode)
    {
        updatePlay(dt);
        tickEditorHunters(dt);
        Combat::harvestAndResolveDots(world(), m_combat);
        tickStatusFx(world(), &audio(), &assets());
        updatePawnAnims();
    }
    else
        updateCamera(dt);
    if (m_sceneMode != SceneMode::Scene2D)
        syncTerrainLod();

    if (m_sceneMode != SceneMode::Scene2D)
    {
        const Entity selected = m_selected;
        world().each<AnimGraphComponent>([&](Entity e, AnimGraphComponent& ag) {
            const EditorObjectComponent* so = world().get<EditorObjectComponent>(e);
            const bool pawn = so && isPawnType(so->type);
            if (pawn && !m_playMode)
            {
                ag.graph.setPreviewPaused(true);
                return;
            }
            if (!pawn && selected.valid() && e.id() == selected.id())
                m_animPanel.applyPlayback(ag.graph);
            else
            {
                ag.graph.setPreviewPaused(false);
                ag.graph.setPreviewSpeedScale(1.0f);
            }
        });
        tickAnimGraphs(world(), assets(), dt);
    }

    tickParticleEmitters(world(), dt);

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
