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

void EditorApp::registerActions()
{
    ActionMap& a = input().actions();
    a.clear();

    a.bindKey("quit", Key::Escape);
    a.bindButton("quit", GamepadButton::Back);
    a.bindKey("place", Key::P);
    a.bindButton("place", GamepadButton::X);
    a.bindKey("delete", Key::Delete);
    a.bindKey("delete", Key::Backspace);
    a.bindButton("delete", GamepadButton::B);
    a.bindKey("toggle_grid", Key::G);
    a.bindKey("toggle_solid", Key::F);
    a.bindKey("toggle_snap", Key::Tab);
    a.bindKey("select_next", Key::RightBracket);
    a.bindKey("select_prev", Key::LeftBracket);
    a.bindKey("type_cube", Key::Digit1);
    a.bindKey("type_sphere", Key::Digit2);
    a.bindKey("type_particle", Key::Digit3);
    a.bindKey("type_point_light", Key::Digit4);
    a.bindKey("type_spot_light", Key::Digit5);
    a.bindKey("cycle_type", Key::T);
    a.bindKey("cycle_color", Key::C);
    a.bindKey("toggle_particle_ui", Key::F2);
    a.bindKey("debug_fill", Key::F1);
    a.bindKey("debug_lighting", Key::F6);
    a.bindKey("debug_shadow_enable", Key::F7);

    a.bindKeyAsAxis("move_x", Key::D, 1.0f);
    a.bindKeyAsAxis("move_x", Key::A, -1.0f);
    a.bindAxis("move_x", GamepadAxis::LeftX, 1.0f);
    a.bindKeyAsAxis("move_z", Key::W, 1.0f);
    a.bindKeyAsAxis("move_z", Key::S, -1.0f);
    a.bindAxis("move_z", GamepadAxis::LeftY, 1.0f);
    a.bindKeyAsAxis("move_y", Key::E, 1.0f);
    a.bindKeyAsAxis("move_y", Key::Q, -1.0f);
    a.bindAxis("move_y", GamepadAxis::RightTrigger, 1.0f);
    a.bindAxis("move_y", GamepadAxis::LeftTrigger, -1.0f);
    a.bindAxis("look_x", GamepadAxis::RightX, 1.0f);
    a.bindAxis("look_y", GamepadAxis::RightY, 1.0f);

    DE_LOG_INFO(
        "Editor: F3 toggle 2D/3D | F2 particle UI | F1 fill F6 lighting F7 shadows | F11 G-buffer | "
        "1/2/3/4/5 place type | P place | MMB/RMB pan (2D) | wheel zoom | Ctrl+S/O save/load | C color | Del delete | -forward");
}

void EditorApp::onInit()
{
    DE_LOG_INFO("EditorApp: init");
    mountContentRoots(assets());
    registerActions();

    m_scenePath = defaultScenePath("level.json");
    m_sceneName = "level";

    if (!renderer().enableSceneBuffers(config().scenePath))
        DE_LOG_ERROR(LogCategory::Render, "EditorApp: SceneBuffers enable failed; SwapChainForward");

    const MeshPass meshPass = renderer().scenePath() == ScenePath::HybridDeferred ? MeshPass::GBuffer : MeshPass::ForwardUnorm;
    if (!m_meshPipeline.create(renderer().device(), meshPass) || !m_linePipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("EditorApp: mesh/line pipeline failed");
        requestQuit();
        return;
    }
    if (!m_meshTransparentPipeline.create(renderer().device(), MeshPass::ForwardTransparent, renderer().sceneColorFormat()))
    {
        DE_LOG_FATAL("EditorApp: transparent mesh pipeline failed");
        requestQuit();
        return;
    }
    {
        const SkinnedMeshPass skinnedPass =
            renderer().scenePath() == ScenePath::HybridDeferred ? SkinnedMeshPass::GBuffer : SkinnedMeshPass::Forward;
        if (!m_skinnedPipeline.create(renderer().device(), skinnedPass))
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: SkinnedMeshPipeline create failed; skinned parts skipped");
        if (!m_skinnedTransparentPipeline.create(renderer().device(), SkinnedMeshPass::ForwardTransparent, renderer().sceneColorFormat()))
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: skinned transparent pipeline create failed");
        if (!m_skinnedShadowPipeline.create(renderer().device(), SkinnedMeshPass::Shadow))
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: skinned shadow pipeline create failed");
        if (!m_skinRing.create(renderer().device()))
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: SkinningUploadRing create failed");
    }
    if (renderer().hasSceneBuffers())
    {
        if (!m_linePipeline3D.create(renderer().device(), renderer().sceneColorFormat()))
        {
            DE_LOG_FATAL("EditorApp: 3D line pipeline failed");
            requestQuit();
            return;
        }
        if (!m_tonemap.create(renderer().device()))
        {
            DE_LOG_FATAL("EditorApp: TonemapPipeline create failed");
            requestQuit();
            return;
        }
        if (renderer().scenePath() == ScenePath::HybridDeferred && !m_lighting.create(renderer().device()))
        {
            DE_LOG_FATAL("EditorApp: DeferredLightingPipeline create failed");
            requestQuit();
            return;
        }
        if (renderer().scenePath() == ScenePath::HybridDeferred)
        {
            if (!m_localLightVolumes.create(renderer().device()))
                DE_LOG_ERROR(LogCategory::Render, "EditorApp: LocalLightVolumePipeline create failed — local lights disabled");
            if (!m_localLightGpu.create(renderer().device()))
                DE_LOG_ERROR(LogCategory::Render, "EditorApp: LocalLightGpuList create failed — local lights disabled");
            MeshData sphereData;
            MeshData coneData;
            if (!CreateIcosahedronBounding(sphereData, 1.0f, 1) || !Mesh::tryCreate(renderer(), sphereData, m_pointVolumeMesh))
                DE_LOG_ERROR(LogCategory::Render, "EditorApp: point volume mesh failed — local lights skipped");
            if (!CreateSpotVolumeCone(coneData, 16, true) || !Mesh::tryCreate(renderer(), coneData, m_spotVolumeMesh))
                DE_LOG_ERROR(LogCategory::Render, "EditorApp: spot volume mesh failed — local lights skipped");
        }
        if (renderer().scenePath() == ScenePath::HybridDeferred)
        {
            if (!m_bloom.create(renderer().device(), renderer().width(), renderer().height()))
                DE_LOG_WARN(LogCategory::Render, "EditorApp: BloomPipeline create failed — bloom disabled");
            else
            {
                m_bloomW = renderer().width();
                m_bloomH = renderer().height();
            }
        }
        if (renderer().scenePath() == ScenePath::HybridDeferred && !m_motionBlur.create(renderer().device()))
            DE_LOG_WARN(LogCategory::Render, "EditorApp: MotionBlurPipeline create failed — motion blur disabled");
        if (renderer().scenePath() == ScenePath::HybridDeferred && !m_taa.create(renderer().device()))
            DE_LOG_WARN(LogCategory::Render, "EditorApp: TaaPipeline create failed — TAA disabled");
        if (!m_debugOverlay.create(renderer().device()))
            DE_LOG_WARN("EditorApp: DebugOverlay create failed — G-buffer tiles disabled");
    }
    if (!pumpBootFrame())
        return;
    if (!m_shadows.create(renderer().device()))
    {
        DE_LOG_FATAL("EditorApp: ShadowSystem create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_particleRenderer.create(renderer()))
    {
        DE_LOG_FATAL("EditorApp: particle renderer failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_imgui.init(window(), renderer(), "editor_imgui.ini", true, UiAccent::Editor))
    {
        DE_LOG_WARN("EditorApp: ImGui init failed — particle UI disabled");
    }

    {
        MeshData data;
        CreateCube(data, 1.0f);
        m_cubeMesh   = Mesh::Create(renderer(), data);
    }

    {
        MeshData data;
        CreateSphere(data, 0.5f, 16, 24);
        m_sphereMesh = Mesh::Create(renderer(), data);
    }

    {
        MeshData data;
        CreateGroundPlane(data, 40.0f, 0.0f, 10.0f);
        m_groundMesh = Mesh::Create(renderer(),data);
    }

    {
        LineMeshData data;
        CreateGridLines(data, 20.0f, 40, 0.01f);
        m_gridMesh = LineMesh::Create(renderer(), data);
    }

    {
        LineMeshData data;
        if (CreateSphereOutline(data, 3, 32))
            m_pointLightGizmo = LineMesh::Create(renderer(), data);
        else
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: point light gizmo outline failed");
    }
    {
        LineMeshData data;
        if (CreateConeOutline(data, 16))
            m_spotLightGizmo = LineMesh::Create(renderer(), data);
        else
            DE_LOG_ERROR(LogCategory::Render, "EditorApp: spot light gizmo outline failed");
    }

    m_propMaterial = std::make_shared<Material>();
    if (!m_propMaterial->createFromAlbedoPath(renderer(), assets(), "textures/dark_engine_cube.png", 80, 160, 220))
    {
        DE_LOG_FATAL("EditorApp: prop material failed");
        requestQuit();
        return;
    }
    assets().registerAsset(m_propMaterial);

    m_groundMaterial = std::make_shared<Material>();
    if (!m_groundMaterial->createSolid(renderer(), assets(), 48, 52, 60, 255))
    {
        DE_LOG_FATAL("EditorApp: ground material failed");
        requestQuit();
        return;
    }
    m_groundMaterial->setBaseColor(0.35f, 0.38f, 0.42f, 1.0f);
    assets().registerAsset(m_groundMaterial);
    m_propMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_groundMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    renderer().setShadowSrv(m_shadows.srvCpu());

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 1.0f;
    m_camera.SetLens(1.04719755f, aspect, 0.05f, 500.0f);
    m_camera.LookAt(Vector3f(8.0f, 6.0f, -10.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
    m_camera2D.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));
    m_camera2D.SetOrthoHeight(16.0f);
    m_camera2D.SetClipPlanes(0.0f, 80.0f);

    if (m_cliJoin)
    {
        applySceneMode(SceneMode::Scene3D);
        DE_LOG_INFO(LogCategory::Networking, "Editor: CLI join — starting with empty 3D scene");
    }
    else if (std::filesystem::exists(m_scenePath))
        loadScene();
    else
        DE_LOG_INFO("EditorApp: no default scene at {}", m_scenePath.string());

    m_sfxPlace  = audio().loadOrBlip(assets(), "audio/place.wav", 620.0f, 0.09f, 0.4f);
    m_sfxDelete = audio().loadOrBlip(assets(), "audio/delete.wav", 300.0f, 0.12f, 0.4f);
    m_sfxSave   = audio().loadOrBlip(assets(), "audio/ui_click.wav", 1400.0f, 0.06f, 0.35f);
    audio().setMasterVolume(0.8f);

    // Callbacks must exist before Application::applyNetConfig runs -host/-join.
    network().setWantsPawn(false);
    network().setSceneMode(m_sceneMode == SceneMode::Scene2D ? 1u : 0u);
    network().setPlayerName("Editor");
    network().setSpawnCallback(&EditorApp::onNetSpawn, this);
    network().setDespawnCallback(&EditorApp::onNetDespawn, this);
    network().setPeerCallback(&EditorApp::onNetPeer, this);

    DE_LOG_INFO("EditorApp: ready ({} objects)", editorObjectCount());
}

void EditorApp::updateCamera(float dt)
{
    const uint32_t rw = renderer().width();
    const uint32_t rh = renderer().height();
    if (rh > 0)
    {
        const float aspect = static_cast<float>(rw) / static_cast<float>(rh);
        if (std::fabs(m_camera.GetAspect() - aspect) > 1e-4f)
            m_camera.SetLens(m_camera.GetFovY(), aspect, m_camera.GetNearZ(), m_camera.GetFarZ());
    }

    if (m_sceneMode == SceneMode::Scene2D)
    {
        updateCamera2D(dt);
        return;
    }

    if (m_imgui.wantCaptureMouse() && !input().mouseDown(MouseButton::Right))
    {
        // Still allow pad look
    }
    else if (input().mouseDown(MouseButton::Right))
    {
        m_camera.RotateY(static_cast<float>(input().mouseDeltaX()) * m_lookSpeed);
        m_camera.Pitch(-static_cast<float>(input().mouseDeltaY()) * m_lookSpeed);
    }

    if (!m_imgui.wantCaptureKeyboard())
    {
        constexpr float kPadLook = 1.6f;
        if (!input().mouseDown(MouseButton::Right))
        {
            m_camera.RotateY(input().actionAxis("look_x") * kPadLook * dt);
            m_camera.Pitch(-input().actionAxis("look_y") * kPadLook * dt);
        }

        float speed = m_moveSpeed;
        if (input().keyDown(Key::LeftShift) || input().keyDown(Key::RightShift))
            speed *= 2.5f;
        m_camera.Strafe(input().actionAxis("move_x") * speed * dt);
        m_camera.Walk(input().actionAxis("move_z") * speed * dt);
        m_camera.Climb(input().actionAxis("move_y") * speed * dt);
    }
    else
    {
        // Gamepad still moves while typing in ImGui
        constexpr float kPadLook = 1.6f;
        m_camera.RotateY(input().actionAxis("look_x") * kPadLook * dt);
        m_camera.Pitch(-input().actionAxis("look_y") * kPadLook * dt);
        m_camera.Strafe(input().actionAxis("move_x") * m_moveSpeed * dt);
        m_camera.Walk(input().actionAxis("move_z") * m_moveSpeed * dt);
        m_camera.Climb(input().actionAxis("move_y") * m_moveSpeed * dt);
    }

    if (!m_imgui.wantCaptureMouse() && input().mouseWheel() != 0.0f)
        m_camera.Walk(input().mouseWheel() * 1.5f);
}

bool EditorApp::groundHitFromRay(const Ray3f& ray, Vector3f& outPoint) const
{
    if (std::fabs(ray.Direction.y) < 1e-6f)
        return false;
    const float t = (0.0f - ray.Origin.y) / ray.Direction.y;
    if (t < 0.0f)
        return false;
    outPoint = ray.PointAt(t);
    return true;
}

bool EditorApp::groundHitFromMouse(Vector3f& outPoint)
{
    const Ray3f ray = m_camera.ScreenPointToRay(
        static_cast<float>(input().mouseX()),
        static_cast<float>(input().mouseY()),
        static_cast<float>(renderer().width()),
        static_cast<float>(renderer().height()));
    return groundHitFromRay(ray, outPoint);
}

void EditorApp::onShutdown()
{
    network().shutdown();
    m_imgui.shutdown(renderer());
    m_particleRenderer.destroy(renderer());
    renderer().waitForGpu();
    if (m_propMaterial)
        assets().unload(m_propMaterial->id);
    if (m_groundMaterial)
        assets().unload(m_groundMaterial->id);
    m_propMaterial.reset();
    m_groundMaterial.reset();
    m_texPlatform = Texture2D{};
    m_texCoin     = Texture2D{};
    m_texSpawn    = Texture2D{};
    m_quadMesh    = Mesh{};
    m_grid2D      = LineMesh{};
    m_boxOutline2D = LineMesh{};
    m_shadows = ShadowSystem{};
    m_emitters.clear();
    audio().stopAll();
    m_sfxPlace.reset();
    m_sfxDelete.reset();
    m_sfxSave.reset();
    DE_LOG_INFO("EditorApp: shutdown");
}
