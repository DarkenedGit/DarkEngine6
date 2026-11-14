#include "EditorApp.h"

#include "Play/DialogueFacts.h"
#include "Play/EnvironmentTick.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/UiPalette.h"
#include "Editor/EditorTypeIcon.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Ui/ImGuiWidgets.h"
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
#include "Character/ThrowableView.h"
#include "Character/SkillXp.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/StatusDot.h"
#include "Particles/ParticleTick.h"
#include "Particles/StatusFxDriver.h"
#include "Sky/CloudVolume.h"
#include "Sky/Environment.h"

#include "Media/MovieCue.h"
#include "Media/MovieHud.h"

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
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Scene...", "Ctrl+S", false, !m_playMode))
                saveSceneWithDialog();
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Load Scene...", "Ctrl+O", false, sceneOk))
                loadSceneWithDialog(m_scenePath);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CUBE "  Load Model...", nullptr, false, sceneOk))
                loadGltfModel();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Model", nullptr, selectedModel() != nullptr))
                saveGltfModel();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save Model As...", nullptr, selectedModel() != nullptr))
                saveGltfModelAs();
            if (ImGui::MenuItem(ICON_FA_FILE "  Open 3D Level...", nullptr, false, sceneOk))
                loadSceneWithDialog(defaultScenePath("level.json"));
            if (ImGui::MenuItem(ICON_FA_FILE "  Open 2D Level...", nullptr, false, sceneOk))
                loadSceneWithDialog(defaultScenePath("level2d.json"));
            if (!sceneOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Disconnect before changing the scene");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Quit", "Esc"))
                requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Panels"))
        {
            ImGui::MenuItem(ICON_FA_BOLT "  Particle Panel", "F2", &m_showParticlePanel);
            ImGui::MenuItem(ICON_FA_PLAY "  Animation Panel", "F4", &m_showAnimPanel);
            ImGui::MenuItem(ICON_FA_LIST "  HSM Panel", "F8", &m_showHsmPanel);
            ImGui::MenuItem(ICON_FA_FILE "  Dialogue", "F9", &m_showDialoguePanel);
            ImGui::MenuItem(ICON_FA_VOLUME_HIGH "  Audio", nullptr, &m_showAudioPanel);
            ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Assets", "F10", &m_showAssetBrowser);
            ImGui::MenuItem(ICON_FA_CUBE "  Model Parts", nullptr, &m_showModelParts);
            ImGui::MenuItem(ICON_FA_CUBE "  Material", nullptr, &m_showMaterialEditor);
            ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Terrain", nullptr, &m_showTerrainPanel);
            ImGui::MenuItem("Water", nullptr, &m_showWaterTools);
            ImGui::MenuItem("Wind", nullptr, &m_showWindPanel);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Viewport"))
        {
            bool mode2d = m_sceneMode == SceneMode::Scene2D;
            if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  2D Scene", "F3", mode2d))
            {
                applySceneMode(mode2d ? SceneMode::Scene3D : SceneMode::Scene2D);
                if (m_sceneMode == SceneMode::Scene3D)
                    ensureGlobalLights();
            }
            ImGui::MenuItem(ICON_FA_EYE "  Grid", nullptr, &m_showGrid);
            ImGui::MenuItem(ICON_FA_CUBE "  Solid Ground", nullptr, &m_showSolid);
            ImGui::SliderFloat(ICON_FA_GAUGE_HIGH "  Fly speed (m/s)", &m_moveSpeed, 1.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("WASD / climb. Shift still multiplies by 2.5. Default 8 for models.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rendering"))
        {
            if (ImGui::MenuItem("Instanced static meshes", nullptr, &renderer().debugState().instancedStatic))
                DE_LOG_INFO("Editor: instanced static = {}", renderer().debugState().instancedStatic);
            if (ImGui::MenuItem("Indirect static draws", nullptr, &renderer().debugState().instancedIndirect))
                DE_LOG_INFO("Editor: indirect static draws = {}", renderer().debugState().instancedIndirect);
            if (ImGui::MenuItem("GPU particles", nullptr, &renderer().debugState().gpuParticles))
                DE_LOG_INFO("Editor: GPU particles = {}", renderer().debugState().gpuParticles);
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
                ImGui::MenuItem("Shadow map tiles", nullptr, &renderer().debugState().shadowMapTiles);
                {
                    const char* shadowDbg[] = { "Off", "Shadow factor", "Face id" };
                    int mode = renderer().debugState().localShadowDebug;
                    if (mode < 0 || mode > 2)
                        mode = 0;
                    if (ImGui::Combo("Local shadow", &mode, shadowDbg, 3))
                        renderer().debugState().localShadowDebug = mode;
                }
                ImGui::MenuItem("Cloud Volumes", nullptr, &renderer().debugState().clouds);
            }
            if (m_sceneMode == SceneMode::Scene3D)
            {
                DebugRenderState& decalDbg = renderer().debugState();
                if (ImGui::MenuItem("Decals", nullptr, &decalDbg.decalsEnabled))
                    DE_LOG_INFO("Editor: decals = {}", decalDbg.decalsEnabled);
                bool decalVolumes = decalDbg.decalsDebug != 0;
                if (ImGui::MenuItem("Decal volumes", nullptr, &decalVolumes))
                {
                    decalDbg.decalsDebug = decalVolumes ? 1 : 0;
                    DE_LOG_INFO("Editor: decal volumes = {}", decalDbg.decalsDebug);
                }
                const DecalFrameStats decalStats = m_scene.decalStats();
                ImGui::Text("Decals  alive %u  drawn %u  culled %u  inside %u  tris %u  recycle %u",
                    decalStats.alive, decalStats.drawn, decalStats.culled, decalStats.inside, decalStats.triangles, decalStats.recycleCount);
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
                if (ImGui::MenuItem(ICON_FA_BUG "  Wolf", "8+P", false, createOk))
                {
                    m_placeType            = SceneObjectType::Wolf;
                    m_queuePlaceAtCursor   = true;
                }
                if (ImGui::MenuItem(ICON_FA_FORWARD_STEP "  Spawner", nullptr, false, createOk))
                {
                    m_placeType          = SceneObjectType::Spawner;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::SetItemTooltip("Places a spawner. Maintain replaces dead units. Waves wait for a timer or until the wave is dead.");
                if (ImGui::MenuItem(ICON_FA_CUBE "  Movie", nullptr, false, createOk))
                {
                    m_placeType          = SceneObjectType::Movie;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::SetItemTooltip("Places a movie cue. The cube is a marker, not the screen.");
                if (ImGui::MenuItem(ICON_FA_BOLT "  Glow Prop", nullptr, false, createOk))
                    m_queueGlowProp = true;
                if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Water", nullptr, false, createOk))
                {
                    m_placeType          = SceneObjectType::Water;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::SetItemTooltip("Places a lake. Position Y is the surface. Width and depth are the footprint. Add several at different heights.");
                if (ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Stream", nullptr, false, createOk))
                {
                    m_placeType          = SceneObjectType::Stream;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::SetItemTooltip("Places a stream ribbon. Follow downhill in the inspector to run it to the lake.");
                if (ImGui::MenuItem(ICON_FA_CLOUD "  Cloud Volume", nullptr, false, createOk))
                {
                    m_placeType          = SceneObjectType::CloudVolume;
                    m_queuePlaceAtCursor = true;
                }
                ImGui::SetItemTooltip("Places a volumetric cloud. Scale is the volume size. Looks correct from inside the volume.");
            }
            if (!createOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Spectators cannot place or delete objects");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play"))
        {
            const bool playOk = m_sceneMode == SceneMode::Scene3D && !netClientLocked();
            if (ImGui::MenuItem(m_playMode ? ICON_FA_PAUSE "  Stop Play" : ICON_FA_PLAY "  Play Scene", "F12", m_playMode, playOk))
                togglePlayMode();
            ImGui::SetItemTooltip("Possess the Player. WASD move, mouse look, Space jump, LMB attack.");
            ImGui::EndMenu();
        }
        drawNetworkMenu();
        drawDebugMenu();
        ImGui::EndMainMenuBar();
    }

    const float toolbarH = ImGui::GetFrameHeight() + 4.0f;
    {
        const ImGuiViewport* barVp = ImGui::GetMainViewport();
        if (barVp)
        {
            ImGui::SetNextWindowPos(barVp->WorkPos);
            ImGui::SetNextWindowSize(ImVec2(barVp->WorkSize.x, toolbarH));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("##editorToolbar", nullptr, barFlags))
        {
            const bool playOk = m_sceneMode == SceneMode::Scene3D && !netClientLocked();
            ImGui::BeginDisabled(!playOk);
            if (ImGui::Button(m_playMode ? ICON_FA_PAUSE "  Stop" : ICON_FA_PLAY "  Play"))
                togglePlayMode();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_playMode);
            if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
                saveSceneWithDialog();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(m_sceneMode == SceneMode::Scene2D ? "3D" : "2D"))
            {
                applySceneMode(m_sceneMode == SceneMode::Scene2D ? SceneMode::Scene3D : SceneMode::Scene2D);
                if (m_sceneMode == SceneMode::Scene3D)
                    ensureGlobalLights();
            }
            ImGui::SameLine();
            if (ImGui::Button(m_showGrid ? ICON_FA_EYE "  Grid" : "Grid"))
                m_showGrid = !m_showGrid;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Fly", &m_moveSpeed, 1.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("WASD / climb. Shift still multiplies by 2.5. Default 8 for models.");
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    drawAssetBrowser();

    beginPassthruDockSpace("EditorDockHost", ImGui::GetFrameHeight(), toolbarH);

    if (m_sceneMode == SceneMode::Scene3D && renderer().hasGBuffer())
    {
        ImGui::SetNextWindowSize(ImVec2(uiPx(420.0f), uiPx(560.0f)), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Lighting"))
        {
            if (ImGui::BeginTabBar("##lighting"))
            {
                if (ImGui::BeginTabItem("IBL"))
                {
                    DebugRenderState& iblDbg = renderer().debugState();
                    const bool        props  = Dark::BeginPropTable("ibl");
                    if (Dark::PropCheckbox("Enabled", &iblDbg.iblEnabled))
                        DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
                    Dark::PropSlider("Intensity", &m_ibl.intensity, 0.0f, 4.0f, "%.2f");
                    float rotDeg = m_ibl.rotationRadY * Math::RadToDeg;
                    if (Dark::PropSlider("Rotation", &rotDeg, -180.0f, 180.0f, "%.1f deg"))
                        m_ibl.rotationRadY = rotDeg * Math::DegToRad;
                    const char* debugViews[] = { "Off", "Irradiance", "Prefilter lod0", "LUT" };
                    if (Dark::PropCombo("Debug view", &iblDbg.iblDebug, debugViews, 4))
                    {
                        if (iblDbg.iblDebug < 0)
                            iblDbg.iblDebug = 0;
                        if (iblDbg.iblDebug > 3)
                            iblDbg.iblDebug = 3;
                        DE_LOG_INFO(LogCategory::Render, "Ibl: enabled={} debug={}", iblDbg.iblEnabled, iblDbg.iblDebug);
                    }
                    if (props)
                        Dark::EndPropTable();
                    ImGui::TextUnformatted(m_ibl.virtualPath.empty() ? "(off)" : m_ibl.virtualPath.c_str());
                    ImGui::TextDisabled("Bake %s", m_ibl.enabled ? "ready" : "off / failed");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("SSAO"))
                {
                    DebugRenderState& ssaoDbg = renderer().debugState();
                    const bool        props   = Dark::BeginPropTable("ssao");
                    if (Dark::PropCheckbox("Enabled", &ssaoDbg.ssaoEnabled))
                    {
                        m_ssao.enabled = ssaoDbg.ssaoEnabled;
                        DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
                    }
                    Dark::PropSlider("Intensity", &m_ssao.intensity, 0.0f, 1.0f, "%.2f");
                    Dark::PropSlider("Radius", &m_ssao.radius, 0.05f, 2.0f, "%.2f");
                    Dark::PropSlider("Power", &m_ssao.power, 0.5f, 4.0f, "%.2f");
                    const char* ssaoDebugViews[] = { "Off", "SSAO factor" };
                    if (Dark::PropCombo("Debug view", &ssaoDbg.ssaoDebug, ssaoDebugViews, 2))
                    {
                        if (ssaoDbg.ssaoDebug < 0)
                            ssaoDbg.ssaoDebug = 0;
                        if (ssaoDbg.ssaoDebug > 1)
                            ssaoDbg.ssaoDebug = 1;
                        DE_LOG_INFO(LogCategory::Render, "Ssao: enabled={} debug={}", ssaoDbg.ssaoEnabled, ssaoDbg.ssaoDebug);
                    }
                    if (props)
                        Dark::EndPropTable();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("SSR"))
                {
                    DebugRenderState& ssrDbg = renderer().debugState();
                    const bool        props  = Dark::BeginPropTable("ssr");
                    if (Dark::PropCheckbox("Enabled", &ssrDbg.ssrEnabled))
                    {
                        m_ssr.enabled = ssrDbg.ssrEnabled;
                        DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
                    }
                    Dark::PropSlider("Max roughness", &m_ssr.maxRoughness, 0.05f, 1.0f, "%.2f");
                    Dark::PropSlider("Thickness", &m_ssr.thickness, 0.02f, 2.0f, "%.2f m");
                    Dark::PropSlider("Stride", &m_ssr.stride, 1.0f, 8.0f, "%.1f px");
                    Dark::PropSlider("Edge fade", &m_ssr.edgeFade, 0.0f, 0.25f, "%.2f");
                    const char* ssrDebugViews[] = { "Off", "Radiance", "Confidence" };
                    if (Dark::PropCombo("Debug view", &ssrDbg.ssrDebug, ssrDebugViews, 3))
                    {
                        if (ssrDbg.ssrDebug < 0)
                            ssrDbg.ssrDebug = 0;
                        if (ssrDbg.ssrDebug > 2)
                            ssrDbg.ssrDebug = 2;
                        DE_LOG_INFO(LogCategory::Render, "Ssr: enabled={} debug={}", ssrDbg.ssrEnabled, ssrDbg.ssrDebug);
                    }
                    if (props)
                        Dark::EndPropTable();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Sky"))
                {
                    drawSkyPanel();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
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
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false) && !ImGui::GetIO().WantTextInput)
        m_showDialoguePanel = !m_showDialoguePanel;
    if (m_showDialoguePanel)
        m_dialoguePanel.draw(&m_showDialoguePanel);

    m_audioPanel.draw(audio(), assets(), &m_showAudioPanel);

    if (ImGui::Begin("Scene"))
    {
        ImGui::TextUnformatted(m_sceneMode == SceneMode::Scene2D ? "Mode: 2D" : "Mode: 3D");
        ImGui::TextWrapped("%s", m_scenePath.string().c_str());
        ImGui::Separator();
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            ImGui::PushID(static_cast<int>(e.id()));
            char label[160];
            if (so.type == SceneObjectType::Model)
            {
                const char* modelName = "model";
                std::string  fileName;
                if (const ModelComponent* mc = world().get<ModelComponent>(e))
                {
                    if (const auto model = assets().getAs<Model>(mc->modelAssetID); model && !model->sourcePath().empty())
                    {
                        fileName  = model->sourcePath().filename().string();
                        modelName = fileName.c_str();
                    }
                }
                if (drawTypeIconImage(typeIconId(SceneObjectType::Model)))
                    std::snprintf(label, sizeof(label), "%s##%u", modelName, e.id());
                else
                    std::snprintf(label, sizeof(label), "%s  %s##%u", iconForType(SceneObjectType::Model), modelName, e.id());
            }
            else
            {
                const char* name = toString(so.type);
                if (so.type == SceneObjectType::AmbientLight)
                    name = "Ambient Light";
                else if (so.type == SceneObjectType::DirectionalLight)
                    name = "Directional Light";
                else if (so.type == SceneObjectType::PointLight)
                    name = "Point Light";
                else if (so.type == SceneObjectType::SpotLight)
                    name = "Spot Light";
                if (drawTypeIconImage(typeIconId(so.type)))
                    std::snprintf(label, sizeof(label), "%s##%u", name, e.id());
                else
                    std::snprintf(label, sizeof(label), "%s  %s##%u", iconForType(so.type), name, e.id());
            }
            if (ImGui::Selectable(label, selected))
                m_selected = e;
            ImGui::PopID();
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (world().has<EditorObjectComponent>(e))
                return;
            if (const TagComponent* tag = world().get<TagComponent>(e))
            {
                if (tag->name == "Shield")
                    return;
            }
            const bool selected = m_selected.valid() && m_selected.id() == e.id();
            ImGui::PushID(static_cast<int>(e.id()) + 100000);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            const std::string name = (model && !model->sourcePath().empty())
                ? model->sourcePath().filename().string()
                : std::string("glTF");
            char label[160];
            if (drawTypeIconImage(typeIconId(SceneObjectType::Model)))
                std::snprintf(label, sizeof(label), "%s##%u", name.c_str(), e.id());
            else
                std::snprintf(label, sizeof(label), "%s  %s##%u", iconForType(SceneObjectType::Model), name.c_str(), e.id());
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
    drawWaterTools();
    drawWindPanel();

    if (m_sceneMode == SceneMode::Scene2D)
    {
        const bool levelOpen = ImGui::Begin("2D Level");
        if (levelOpen)
        {
        ImGui::Text("MMB/RMB drag to pan, wheel to zoom, P to place.");
        float wmin[2] = { m_worldMin.x, m_worldMin.y };
        float wmax[2] = { m_worldMax.x, m_worldMax.y };
        if (Dark::PropFloat2("World min", wmin, 0.25f))
        {
            m_worldMin = Vector2f(wmin[0], wmin[1]);
            rebuildGrid2D();
        }
        if (Dark::PropFloat2("World max", wmax, 0.25f))
        {
            m_worldMax = Vector2f(wmax[0], wmax[1]);
            rebuildGrid2D();
        }
        Dark::PropSlider("Snap", &m_gridSnap, 0.0f, 4.0f, "%.2f");
        ImGui::Separator();
        if (EditorObjectComponent* so = findObject(m_selected))
        {
            if (auto* xf = world().get<TransformComponent>(m_selected))
            {
                ImGui::Text("Selected: %s", toString(so->type));
                float pos[2] = { xf->position.x, xf->position.y };
                float size[2] = { xf->scale.x, xf->scale.y };
                if (Dark::PropFloat2("Position", pos, 0.05f) && !netClientLocked())
                {
                    xf->position.x = pos[0];
                    xf->position.y = pos[1];
                }
                if (Dark::PropFloat2("Size", size, 0.05f, 0.05f, 200.0f) && !netClientLocked())
                {
                    xf->scale.x = Max(0.05f, size[0]);
                    xf->scale.y = Max(0.05f, size[1]);
                }
                Dark::PropColor3("Tint", so->color);
                if (ImGui::Button(ICON_FA_TRASH "  Delete") && !netClientLocked())
                    deleteSelected();
            }
        }
        else
        {
            ImGui::TextUnformatted("No selection. Click an object or press P to place.");
        }
        }
        ImGui::End();
    }

    if (m_sceneMode == SceneMode::Scene3D)
    {
        drawInspector3D();
        if (!m_playMode)
            drawTranslateGizmos();
        if (m_playMode)
        {
            drawPlayHud();
            drawLoot();
            drawUpgrades();
        }
        drawMovieHud(m_movie, true);
    }

    drawStatusBar();
    drawAssetDropTarget();
}

void EditorApp::drawSkyPanel()
{
    const Vector3f lc = m_env.lightColor();
    ImGui::ColorButton("##light", ImVec4(lc.x, lc.y, lc.z, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.0f, 18.0f));
    ImGui::SameLine();
    ImGui::Text("Sun  elev %.0f deg", static_cast<double>(RadiansToDegrees(m_env.sunElevation())));

    float tod = m_env.timeOfDay;
    if (Dark::PropSlider("Time of day", &tod, 0.0f, 23.99f, "%.2f h"))
    {
        m_env.timeOfDay = tod;
        m_env.evaluate();
        applySkyToLights();
    }
    bool animate = m_env.timeScale > 0.0f;
    if (Dark::PropCheckbox("Animate time", &animate))
    {
        m_env.timeScale = animate ? 0.35f : 0.0f;
        DE_LOG_INFO("Sky: time scale = {:.2f} h/s", m_env.timeScale);
    }

    auto weatherBtn = [&](const char* label, const Sky::WeatherState& w, const ImVec4& col) {
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.12f, col.y + 0.12f, col.z + 0.12f, 1.0f));
        if (ImGui::Button(label, ImVec2(uiPx(72.0f), uiPx(28.0f))))
        {
            m_env.weather = w;
            m_env.cloudLayer.windSpeedMps = Sky::cloudWindSpeedMpsFor(w);
            m_env.evaluate();
            applySkyToLights();
            DE_LOG_INFO("Sky: weather {}", label);
        }
        ImGui::PopStyleColor(2);
    };
    weatherBtn("Clear", Sky::WeatherState::Clear(), ImVec4(0.20f, 0.45f, 0.72f, 1.0f));
    ImGui::SameLine();
    weatherBtn("Partly", Sky::WeatherState::PartlyCloudy(), ImVec4(0.32f, 0.42f, 0.55f, 1.0f));
    ImGui::SameLine();
    weatherBtn("Overcast", Sky::WeatherState::Overcast(), ImVec4(0.38f, 0.40f, 0.42f, 1.0f));
    ImGui::SameLine();
    weatherBtn("Storm", Sky::WeatherState::Storm(), ImVec4(0.28f, 0.22f, 0.40f, 1.0f));
    ImGui::TextDisabled("Time drives the directional light and cloud lighting.");

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Clouds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Sky::CloudLayerDesc& cl = m_env.cloudLayer;
        if (Dark::PropSlider("Coverage", &m_env.weather.cloudCoverage, 0.0f, 1.0f, "%.2f"))
        {
            m_env.evaluate();
            applySkyToLights();
        }
        if (Dark::PropSlider("Altitude (m)", &cl.altitude, 500.0f, 8000.0f, "%.0f"))
            m_env.evaluate();
        if (Dark::PropSlider("Amount", &cl.amount, 0.0f, 2.0f, "%.2f"))
            m_env.evaluate();
        Dark::PropSlider("Wind speed (m/s)", &cl.windSpeedMps, 0.0f, 20.0f, "%.1f");
        Dark::PropFloat2("Wind dir", &m_env.weather.windDir.x, 0.01f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Sky deck, drawn with the sky. A Cloud Volume is a separate object. It draws only when that volume is enabled and Rendering → Cloud Volumes is on.");
        ImGui::PopStyleColor();
    }
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
            if (Dark::PropFloat3("Position", pos, 0.05f))
            {
                xf->position.x = pos[0];
                xf->position.y = pos[1];
                xf->position.z = pos[2];
            }
            float scale[3] = { xf->scale.x, xf->scale.y, xf->scale.z };
            if (Dark::PropFloat3("Scale", scale, 0.01f, 0.01f, 100.0f))
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
        drawPhysicsInspector(m_selected);
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
    if (so->type == SceneObjectType::Player || so->type == SceneObjectType::Hunter || so->type == SceneObjectType::Wolf)
    {
        if (HealthComponent* hp = world().get<HealthComponent>(m_selected))
            ImGui::Text("HP  %.0f / %.0f", static_cast<double>(hp->health.hp()), static_cast<double>(hp->health.maxHp()));
        if (JumpAttackComponent* jac = world().get<JumpAttackComponent>(m_selected))
            ImGui::Text("Jump cooldown  %.2fs", static_cast<double>(jac->jump.cooldownLeft()));
        if (so->type == SceneObjectType::Player && ImGui::Button(ICON_FA_PLAY "  Play from here") && !m_playMode)
            togglePlayMode();
        if (so->type == SceneObjectType::Hunter)
        {
            char dialoguePath[192];
            std::snprintf(dialoguePath, sizeof(dialoguePath), "%s", so->dialogue.c_str());
            ImGui::BeginDisabled(m_playMode);
            if (ImGui::InputText("Dialogue", dialoguePath, sizeof(dialoguePath)))
                so->dialogue = dialoguePath;
            ImGui::EndDisabled();
            ImGui::TextDisabled("Content path. Play, stand close, look at them, and press T.");
        }
        ImGui::TextDisabled("F12 plays the scene. Place hunters or wolves, then Play to fight.");
    }
    else if (so->type == SceneObjectType::Model)
    {
        if (const ModelComponent* mc = world().get<ModelComponent>(m_selected))
        {
            if (const auto model = assets().getAs<Model>(mc->modelAssetID))
                ImGui::TextWrapped("%s", model->sourcePath().string().c_str());
        }
        if (ImGui::Button("Open Model Parts"))
        {
            m_showModelParts     = true;
            m_showMaterialEditor = true;
        }
    }
    ImGui::BeginDisabled(locked);
    float pos[3] = { xf->position.x, xf->position.y, xf->position.z };
    if (Dark::PropFloat3("Position", pos, 0.05f))
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
        Dark::PropCheckbox("Enabled", &amb->enabled);
        if (Dark::PropColor3("Color", &amb->color.x))
        {
            so->color[0] = amb->color.x;
            so->color[1] = amb->color.y;
            so->color[2] = amb->color.z;
        }
        Dark::PropFloat("Intensity", &amb->intensity, 0.01f, 0.0f, 8.0f);
        ImGui::TextDisabled("Fills the whole scene. Position is only a handle.");
    }
    else if (auto* sun = world().get<DirectionalLightComponent>(m_selected))
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Directional Light");
        Dark::PropCheckbox("Enabled", &sun->enabled);
        ImGui::BeginDisabled();
        Dark::PropColor3("Color", &sun->color.x);
        ImGui::EndDisabled();
        Dark::PropFloat("Intensity", &sun->intensity, 0.01f, 0.0f, 8.0f);
        float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
        eulerXYZFromQuat(xf->rotation, pitch, yaw, roll);
        float eulerDeg[3] = {
            RadiansToDegrees(pitch),
            RadiansToDegrees(yaw),
            RadiansToDegrees(roll)
        };
        ImGui::BeginDisabled();
        Dark::PropFloat3("Euler (deg)", eulerDeg, 0.5f);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Direction and color follow Sky / time of day.");
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
            Dark::PropCheckbox("Enabled", &light->enabled);
            if (Dark::PropColor3("Light color", &light->color.x))
            {
                so->color[0] = light->color.x;
                so->color[1] = light->color.y;
                so->color[2] = light->color.z;
            }
            Dark::PropFloat("Intensity (cd)", &light->intensity, 10.0f, 0.0f, 50000.0f);
            Dark::PropSlider("Range (m)", &light->range, 0.25f, 80.0f);
            Dark::PropCheckbox("Cast shadow", &light->castShadow);
            ImGui::TextDisabled("Point light on this emitter. Lights the ground and nearby meshes.");
        }
    }
    else if (auto* light = world().get<LocalLightComponent>(m_selected))
    {
        ImGui::Separator();
        Dark::PropCheckbox("Enabled", &light->enabled);
        if (Dark::PropColor3("Color", &light->color.x))
        {
            so->color[0] = light->color.x;
            so->color[1] = light->color.y;
            so->color[2] = light->color.z;
        }
        Dark::PropFloat("Intensity (cd)", &light->intensity, 10.0f, 0.0f, 50000.0f);
        Dark::PropSlider("Range (m)", &light->range, 0.25f, 80.0f);
        Dark::PropCheckbox("Cast shadow", &light->castShadow);
        if (light->type == LocalLightType::Spot)
        {
            if (Dark::PropSlider("Inner (deg)", &light->innerConeDeg, 0.0f, 80.0f) && light->innerConeDeg > light->outerConeDeg)
                light->outerConeDeg = light->innerConeDeg;
            if (Dark::PropSlider("Outer (deg)", &light->outerConeDeg, 0.0f, 80.0f) && light->innerConeDeg > light->outerConeDeg)
                light->innerConeDeg = light->outerConeDeg;
            float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
            eulerXYZFromQuat(xf->rotation, pitch, yaw, roll);
            float eulerDeg[3] = {
                RadiansToDegrees(pitch),
                RadiansToDegrees(yaw),
                RadiansToDegrees(roll)
            };
            if (Dark::PropFloat3("Euler (deg)", eulerDeg, 0.5f))
            {
                xf->rotation = Quaternion::FromEulerXYZ(
                    DegreesToRadians(eulerDeg[0]),
                    DegreesToRadians(eulerDeg[1]),
                    DegreesToRadians(eulerDeg[2]));
            }
            Dark::PropCheckbox("Searchlight", &so->source.searchlight);
            if (so->source.searchlight)
            {
                Dark::PropSlider("Sweep (deg)", &so->source.sweepDeg, 0.0f, 180.0f);
                ImGui::TextDisabled("Swings around this aim in Play and sounds the alarm when it lights the player.");
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
        Dark::PropFloat("Source radius", &light->sourceRadius, 0.005f, 0.0f, 2.0f);
    }
    else if (so->type == SceneObjectType::Spawner)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Spawner");
        if (SpawnerComponent* spawner = world().get<SpawnerComponent>(m_selected))
        {
            SpawnerDesc& d = spawner->desc;
            Dark::PropCheckbox("Enabled", &d.enabled);
            int mode = static_cast<int>(d.mode);
            const char* modes[] = { "Maintain", "Waves" };
            if (Dark::PropCombo("Mode", &mode, modes, 2))
                d.mode = static_cast<SpawnerMode>(mode);
            int unit = static_cast<int>(d.unit);
            const char* units[] = { "Hunter", "Wolf", "Crow" };
            if (Dark::PropCombo("Unit", &unit, units, 3))
                d.unit = static_cast<SpawnerUnit>(unit);
            if (d.unit == SpawnerUnit::Crow)
                ImGui::TextDisabled("Crows flock, perch in trees, or hop for food. A nearby hunter or the player makes them call and fly off.");
            int count = d.count;
            if (ImGui::SliderInt("Count", &count, 1, kSpawnerSlotCount))
                d.count = count;
            Dark::PropFloat("Radius (m)", &d.radius, 0.1f, 0.0f, 64.0f, "%.1f");
            if (d.mode == SpawnerMode::Maintain)
            {
                Dark::PropFloat("Respawn delay", &d.respawnDelay, 0.05f, 0.0f, 60.0f, "%.2f s");
                ImGui::TextDisabled("Keeps Count units alive. Each death waits, then a new unit appears inside the radius.");
            }
            else
            {
                int gate = static_cast<int>(d.gate);
                const char* gates[] = { "After time", "When cleared" };
                if (Dark::PropCombo("Next wave", &gate, gates, 2))
                    d.gate = static_cast<SpawnerWaveGate>(gate);
                if (d.gate == SpawnerWaveGate::Time)
                {
                    Dark::PropFloat("Wave interval", &d.waveInterval, 0.1f, 0.25f, 120.0f, "%.2f s");
                    ImGui::TextDisabled("Spawns Count units on the interval. Earlier units stay in the level.");
                }
                else
                {
                    ImGui::TextDisabled("Spawns Count units. The next wave starts when that wave is dead.");
                }
                ImGui::SliderInt("Max waves", &d.maxWaves, 0, 32);
                ImGui::TextDisabled("0 keeps spawning.");
            }
            int seed = static_cast<int>(d.seed > 1000000000u ? 1000000000u : d.seed);
            if (ImGui::DragInt("Seed", &seed, 1.0f, 1, 1000000000))
                d.seed = static_cast<uint32_t>(seed < 1 ? 1 : seed);
            clampSpawnerDesc(d);
            if (m_playMode)
            {
                int live = 0;
                for (int i = 0; i < d.count && i < kSpawnerSlotCount; ++i)
                {
                    if (spawner->runtime.slots[i].tracked)
                        ++live;
                }
                if (d.mode == SpawnerMode::Waves)
                    ImGui::Text("Live %d    waves %d", live, spawner->runtime.wavesSpawned);
                else
                    ImGui::Text("Live %d / %d", live, d.count);
            }
        }
        if (Dark::PropColor3("Tint", so->color))
        {
            if (AssetRef<Material> mat = ensureUniqueMeshMaterial(m_selected))
                mat->setBaseColor(so->color[0], so->color[1], so->color[2], mat->baseColor()[3]);
        }
        ImGui::TextDisabled("The cube is a marker. Radius is the spawn area, not the cube size.");
    }
    else if (so->type == SceneObjectType::Movie)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Movie");
        if (MovieCueComponent* cue = world().get<MovieCueComponent>(m_selected))
        {
            ImGui::BeginDisabled(m_playMode);
            if (cue->path.size() >= 260)
                ImGui::TextWrapped("%s", cue->path.c_str());
            else
            {
                char moviePath[260];
                std::snprintf(moviePath, sizeof(moviePath), "%s", cue->path.c_str());
                if (ImGui::InputText("Movie", moviePath, sizeof(moviePath)))
                    cue->path = moviePath;
            }
            Dark::PropFloat("Radius (m)", &cue->radius, 0.1f, 0.0f, 64.0f);
            Dark::PropCheckbox("Loop", &cue->loop);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Fires once when the possessed pawn's XZ enters the radius during play. Fullscreen cutscene. The marker is not the screen.");
        }
    }
    else if (so->type == SceneObjectType::Water)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Water body");
        float width = xf->scale.x;
        float depth = xf->scale.z;
        if (Dark::PropFloat("Width (m)", &width, 0.5f, 4.0f, 1024.0f, "%.1f"))
            xf->scale.x = Max(4.0f, width);
        if (Dark::PropFloat("Depth (m)", &depth, 0.5f, 4.0f, 1024.0f, "%.1f"))
            xf->scale.z = Max(4.0f, depth);
        ImGui::TextDisabled("Y is this lake's surface. Each water object is its own body.");
    }
    else if (so->type == SceneObjectType::Stream)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Stream");
        if (StreamComponent* stream = world().get<StreamComponent>(m_selected))
        {
            float width = stream->width;
            if (Dark::PropFloat("Width (m)", &width, 0.1f, 1.5f, 12.0f, "%.1f"))
                stream->width = Clamp(width, 1.5f, 12.0f);
            Dark::PropSlider("Flow", &stream->flowSpeed, 0.0f, 6.0f, "%.2f m/s");
            if (ImGui::Button("Follow downhill") && !stream->points.empty() && m_haveTerrain && m_terrain.coarse().valid())
            {
                const float lake = m_water.chunksX() > 0 ? m_water.params().waterLevel : m_terrainSeaLevel;
                std::vector<Vector2f> walked;
                std::string reason;
                if (followDownhill(m_terrain.coarse(), stream->points.front(), lake, walked, &reason))
                {
                    stream->points = std::move(walked);
                    stream->snapToPoints = true;
                }
                stream->stopReason = reason;
            }
            if (!stream->stopReason.empty())
                ImGui::TextDisabled("%s", stream->stopReason.c_str());
            ImGui::TextDisabled("%d points", static_cast<int>(stream->points.size()));
        }
    }
    else if (so->type == SceneObjectType::CloudVolume)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Cloud volume");
        if (CloudVolumeComponent* cloud = world().get<CloudVolumeComponent>(m_selected))
        {
            CloudVolumeDesc& d = cloud->desc;
            Dark::PropCheckbox("Enabled", &d.enabled);
            int shape = (d.shape == CloudShape::Box) ? 0 : 1;
            const char* shapes[] = { "Box", "Ellipsoid" };
            if (Dark::PropCombo("Shape", &shape, shapes, 2))
                d.shape = (shape == 0) ? CloudShape::Box : CloudShape::Ellipsoid;
            float size[3] = { xf->scale.x, xf->scale.y, xf->scale.z };
            if (Dark::PropFloat3("Size (m)", size, 0.5f, 4.0f, 2048.0f, "%.1f"))
            {
                xf->scale.x = Max(4.0f, size[0]);
                xf->scale.y = Max(4.0f, size[1]);
                xf->scale.z = Max(4.0f, size[2]);
            }
            ImGui::TextDisabled("Width, height, depth of the volume. Position is the center.");
            Dark::PropSlider("Density", &d.density, 0.0f, 4.0f, "%.2f");
            Dark::PropSlider("Coverage", &d.coverage, 0.0f, 1.0f, "%.2f");
            Dark::PropSlider("Softness", &d.softness, 0.02f, 1.0f, "%.2f");
            Dark::PropColor3("Albedo", &d.albedo.x);
            Dark::PropSlider("Absorption", &d.absorption, 0.0f, 4.0f, "%.2f");
            Dark::PropSlider("Scattering", &d.scattering, 0.0f, 4.0f, "%.2f");
            Dark::PropSlider("Anisotropy", &d.anisotropy, -0.85f, 0.85f, "%.2f");
            Dark::PropSlider("Silver lining", &d.silverLining, 0.0f, 3.0f, "%.2f");
            Dark::PropSlider("Noise scale", &d.noiseScale, 0.01f, 0.25f, "%.3f");
            Dark::PropSlider("Detail scale", &d.detailScale, 1.0f, 8.0f, "%.2f");
            Dark::PropSlider("Detail strength", &d.detailStrength, 0.0f, 1.0f, "%.2f");
            Dark::PropSlider("Height falloff", &d.heightFalloff, 0.0f, 1.0f, "%.2f");
            Dark::PropSlider("Wind speed", &d.windSpeed, 0.0f, 8.0f, "%.2f");
            Dark::PropFloat3("Wind dir", &d.windDir.x, 0.01f);
        }
    }
    else if (!isPawnType(so->type) && so->type != SceneObjectType::Model)
    {
        if (Dark::PropColor3("Tint", so->color))
        {
            if (AssetRef<Material> mat = ensureUniqueMeshMaterial(m_selected))
                mat->setBaseColor(so->color[0], so->color[1], so->color[2], mat->baseColor()[3]);
        }
        if (auto* mc = world().get<MeshComponent>(m_selected))
            Dark::PropSlider("Emissive", &mc->emissive, 0.0f, 1.0f);
        if (world().get<MeshComponent>(m_selected) && ImGui::Button("Open Material"))
            m_showMaterialEditor = true;
    }

    drawPhysicsInspector(m_selected);

    if (envLight)
    {
        ImGui::BeginDisabled();
        ImGui::Button(ICON_FA_TRASH "  Delete");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
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
    pollSceneOpen();
    m_movie.tick(dt);
    if (m_playMode)
        tickMovieCues(world(), m_movie, m_playPlayer);
    if (m_movie.blocksWorld() && !m_movieWasBlocking)
    {
        audio().stopMusic();
        audio().stopClipVoices();
    }
    m_movieWasBlocking = m_movie.blocksWorld();

    if (m_playMode)
        dt = m_play.scaleDt(dt);
    const NetRole role = network().role();
    if (role == NetRole::Joining && m_lastNetRole != NetRole::Joining)
        discardLocalSceneForJoin();
    m_lastNetRole = role;

    m_cloudTime += dt;
    if (m_sceneMode == SceneMode::Scene3D)
    {
        m_env.tick(dt);
        applySkyToLights();
    }
    handleEditorCommands(dt);
    if (m_playMode)
    {
        if (!m_playPlayer.valid() || !world().alive(m_playPlayer))
            setPlayMode(false);
        else
        {
            if (!m_dialogue.locksWorld() && !m_movie.blocksWorld())
            {
                bindPlayFeedback();
                tickPlayMuzzle(m_playFeedback, dt);
                m_play.tickPawn(dt);
                m_play.tickCamera();
                m_play.tickWorld(dt);
                m_blood.update(dt);
            }
            if (!m_movie.blocksWorld())
                m_dialogue.tick(dt);
            m_save.service(world());
            if (m_defenseTextLeft > 0.0f)
            {
                m_defenseTextLeft -= dt;
                if (m_defenseTextLeft < 0.0f)
                    m_defenseTextLeft = 0.0f;
            }
            if (!m_questBanners.empty())
            {
                m_questBanners.front().timeLeft -= dt;
                if (m_questBanners.front().timeLeft <= 0.0f)
                    m_questBanners.erase(m_questBanners.begin());
            }
        }
    }
    else
    {
        updateCamera(dt);
        syncEditPhysics();
    }
    const bool movieHolds = m_playMode && m_movie.blocksWorld();
    window().setCursorCaptured(m_playMode && window().isFocused() && !m_lootTarget.valid() && !m_showUpgrades && !movieHolds);
    if (m_sceneMode != SceneMode::Scene2D)
    {
        if (!movieHolds)
            m_water.tick(dt);
        const bool terrainChanged = m_terrainHeightDirty;
        syncTerrainLod();
        syncPlacedWater(terrainChanged);
        if (!movieHolds)
            updateEditorGrass(dt);
    }

    if (m_sceneMode != SceneMode::Scene2D && !movieHolds)
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
        if (m_playMode)
            m_play.tickHeldItems();
    }

    // Scene2D never calls simulateAndDraw. Leave the GPU clock and burst queue alone.
    if (!movieHolds && m_sceneMode != SceneMode::Scene2D)
    {
        m_gpuParticles.applyIntegratorFallback(world(), renderer().debugState().gpuParticles);
        tickParticleEmitters(world(), dt);
        m_gpuParticles.prepare(world(), dt);
    }
    else if (!movieHolds)
        tickParticleEmitters(world(), dt);

    if (m_sceneMode == SceneMode::Scene2D)
    {
        Audio::AudioListener lis{};
        lis.distanceScale = 1.0f;
        lis.position      = Vector3f(m_camera2D.GetPosition().x, m_camera2D.GetPosition().y, 0.0f);
        lis.forward       = Vector3f(0.0f, 0.0f, 1.0f);
        lis.up            = Vector3f(0.0f, 1.0f, 0.0f);
        audio().setListener(lis);
    }
    else
        setViewAudioListener(audio(), m_camera, &world(), m_playPlayer, m_playMode && skillCatalog().enabled());
    m_scene.tickDecals(world(), dt);
}
