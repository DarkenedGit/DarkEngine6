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

EditorApp::EditorApp(const AppConfig& cfg)
    : Application(cfg)
    , m_cliJoin(cfg.netJoin.ipv4 != 0 || cfg.netJoin.port != 0)
{
}

float EditorApp::snap(float v, float grid)
{
    if (grid <= 0.0f)
        return v;
    return std::floor(v / grid + 0.5f) * grid;
}

bool EditorApp::ensure2DResources()
{
    if (m_2dReady)
        return true;
    if (!m_spritePipe.create(renderer().device()))
    {
        DE_LOG_ERROR("Editor: SpritePipeline failed");
        return false;
    }
    {
        MeshData mesh_data;
        CreateQuadXY(mesh_data, 1.0f, 1.0f);
        m_quadMesh = Mesh::Create(renderer(), mesh_data);
    }

    {
        LineMeshData mesh_data;
        CreateBoxOutlineXY(mesh_data);
        m_boxOutline2D = LineMesh::Create(renderer(), mesh_data);
    }

    if (!m_quadMesh.valid())
    {
        DE_LOG_ERROR("Editor: 2D quad mesh failed");
        return false;
    }
    if (!createChecker(renderer(), m_texPlatform, 118, 86, 52, 92, 66, 40)
        || !m_texCoin.createSolidColor(renderer(), 236, 196, 64)
        || !m_texSpawn.createSolidColor(renderer(), 48, 196, 168))
    {
        DE_LOG_ERROR("Editor: 2D textures failed");
        return false;
    }
    rebuildGrid2D();
    m_2dReady = true;
    return true;
}

void EditorApp::rebuildGrid2D()
{
    renderer().waitForGpu();
    const float x0 = std::floor(m_worldMin.x) - 1.0f;
    const float y0 = std::floor(m_worldMin.y) - 1.0f;
    const float x1 = std::ceil(m_worldMax.x) + 1.0f;
    const float y1 = std::ceil(m_worldMax.y) + 1.0f;
    {
        LineMeshData mesh_data;
        CreateGridLinesXY(mesh_data,x0, y0, x1, y1, 1.0f, 3.0f);
        m_grid2D = LineMesh::Create(renderer(), mesh_data);
    }
}

void EditorApp::clampCamera2D()
{
    const float halfW = m_camera2D.GetVisibleWidth() * 0.5f;
    const float halfH = m_camera2D.GetVisibleHeight() * 0.5f;
    const float worldW = m_worldMax.x - m_worldMin.x;
    const float worldH = m_worldMax.y - m_worldMin.y;
    Vector2f cam = m_camera2D.GetPosition();
    if (m_camera2D.GetVisibleWidth() >= worldW)
        cam.x = 0.5f * (m_worldMin.x + m_worldMax.x);
    else
        cam.x = Clamp(cam.x, m_worldMin.x + halfW, m_worldMax.x - halfW);
    if (m_camera2D.GetVisibleHeight() >= worldH)
        cam.y = 0.5f * (m_worldMin.y + m_worldMax.y);
    else
        cam.y = Clamp(cam.y, m_worldMin.y + halfH, m_worldMax.y - halfH);
    m_camera2D.SetPosition(cam);
}

void EditorApp::applySceneMode(SceneMode mode)
{
    m_sceneMode = mode;
    if (mode == SceneMode::Scene2D)
    {
        ensure2DResources();
        renderer().setClearColor(0.38f, 0.62f, 0.86f, 1.0f);
        m_placeType = SceneObjectType::Platform;
        m_camera2D.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));
        m_camera2D.SetOrthoHeight(16.0f);
        m_camera2D.SetClipPlanes(0.0f, 80.0f);
        m_camera2D.SetZoom(1.0f);
        m_camera2D.SetPosition(0.5f * (m_worldMin.x + m_worldMax.x), 0.5f * (m_worldMin.y + m_worldMax.y));
        clampCamera2D();
    }
    else
    {
        renderer().setClearColor(UiPalette::kVoid.r, UiPalette::kVoid.g, UiPalette::kVoid.b, 1.0f);
        m_placeType = SceneObjectType::Cube;
    }
}

void EditorApp::newScene3D()
{
    if (netSceneLocked())
        return;
    applySceneMode(SceneMode::Scene3D);
    m_scenePath = defaultScenePath("level.json");
    m_sceneName = "level";
    clearScene();
    DE_LOG_INFO("Editor: new 3D scene");
}

void EditorApp::newScene2D()
{
    if (netSceneLocked())
        return;
    applySceneMode(SceneMode::Scene2D);
    m_scenePath = defaultScenePath("level2d.json");
    m_sceneName = "level2d";
    clearScene();
    m_worldMin = Vector2f(0.0f, 0.0f);
    m_worldMax = Vector2f(96.0f, 22.0f);
    rebuildGrid2D();
    float platCol[4]{};
    float spawnCol[4]{};
    defaultColor2D(SceneObjectType::Platform, platCol);
    defaultColor2D(SceneObjectType::Spawn, spawnCol);
    spawnObject(SceneObjectType::Platform, Vector3f(14.0f, 0.7f, 0.0f), Vector3f(28.0f, 1.4f, 1.0f), Quaternion::IDENTITY, platCol);
    spawnObject(SceneObjectType::Spawn, Vector3f(3.0f, 3.5f, 0.0f), defaultScale2D(SceneObjectType::Spawn), Quaternion::IDENTITY, spawnCol);
    m_camera2D.SetPosition(14.0f, 6.0f);
    clampCamera2D();
    DE_LOG_INFO("Editor: new 2D scene");
}

bool EditorApp::worldFromMouse2D(Vector2f& out)
{
    out = m_camera2D.ScreenToWorld(
        Vector2f(static_cast<float>(input().mouseX()), static_cast<float>(input().mouseY())),
        static_cast<float>(renderer().width()),
        static_cast<float>(renderer().height()));
    return true;
}

AABox2f EditorApp::objectBounds2D(SceneObjectType type, const Vector3f& pos, const Vector3f& scale) const
{
    Vector2f half(0.5f * std::fabs(scale.x), 0.5f * std::fabs(scale.y));
    if (type == SceneObjectType::Coin)
        half = Vector2f(0.28f, 0.28f);
    return AABox2f::FromCenterExtents(Vector2f(pos.x, pos.y), half);
}

void EditorApp::updateCamera2D(float dt)
{
    m_camera2D.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));

    const bool uiMouse = m_imgui.wantCaptureMouse();
    const bool uiKey   = m_imgui.wantCaptureKeyboard();

    if (!uiMouse && (input().mouseDown(MouseButton::Middle) || input().mouseDown(MouseButton::Right)))
    {
        if (!m_panning)
        {
            m_panning   = true;
            m_panMouseX = input().mouseX();
            m_panMouseY = input().mouseY();
        }
        const float dx = static_cast<float>(input().mouseX() - m_panMouseX);
        const float dy = static_cast<float>(input().mouseY() - m_panMouseY);
        m_panMouseX    = input().mouseX();
        m_panMouseY    = input().mouseY();
        const float vw = static_cast<float>(renderer().width());
        const float vh = static_cast<float>(renderer().height());
        if (vw > 1.0f && vh > 1.0f)
        {
            m_camera2D.Move(Vector2f(
                -dx / vw * m_camera2D.GetVisibleWidth(),
                dy / vh * m_camera2D.GetVisibleHeight()));
        }
    }
    else
    {
        m_panning = false;
    }

    if (!uiKey)
    {
        float speed = m_moveSpeed;
        if (input().keyDown(Key::LeftShift) || input().keyDown(Key::RightShift))
            speed *= 2.5f;
        m_camera2D.Move(Vector2f(input().actionAxis("move_x") * speed * dt, input().actionAxis("move_z") * speed * dt));
    }

    if (!uiMouse && input().mouseWheel() != 0.0f)
    {
        Vector2f before{};
        worldFromMouse2D(before);
        m_camera2D.ZoomBy(input().mouseWheel() > 0.0f ? 1.12f : 1.0f / 1.12f);
        Vector2f after{};
        worldFromMouse2D(after);
        m_camera2D.Move(Vector2f(before.x - after.x, before.y - after.y));
    }

    clampCamera2D();
}

void EditorApp::drawSprite2D(
    ID3D12GraphicsCommandList* cmd,
    const Texture2D& texture,
    const Vector2f& pos,
    const Vector2f& size,
    float z,
    float cr,
    float cg,
    float cb,
    float uvSx,
    float uvSy)
{
    if (!cmd || !texture.valid() || !m_quadMesh.valid())
        return;
    const Matrix4f world = Matrix4f::ScaleMatrixXYZ(size.x, size.y, 1.0f)
        * Matrix4f::TranslationMatrix(pos.x, pos.y, z);
    const Matrix4f wvp = world * m_camera2D.GetViewProj();
    SpriteConstants sc{};
    copyMatrix(sc.worldViewProj, wvp);
    sc.color[0]    = cr;
    sc.color[1]    = cg;
    sc.color[2]    = cb;
    sc.color[3]    = 1.0f;
    sc.uvScale[0]  = uvSx;
    sc.uvScale[1]  = uvSy;
    texture.bind(cmd, SpritePipeline::kRootAlbedoSrv);
    m_spritePipe.setConstants(cmd, sc);
    m_quadMesh.draw(cmd);
}

const Mesh* EditorApp::meshForType(SceneObjectType type) const
{
    switch (type)
    {
    case SceneObjectType::Sphere:
        return m_sphereMesh.valid() ? &m_sphereMesh : &m_cubeMesh;
    case SceneObjectType::ParticleEmitter:
        // Small proxy cube marks emitter origin
        return &m_cubeMesh;
    case SceneObjectType::PointLight:
    case SceneObjectType::SpotLight:
        return nullptr;
    case SceneObjectType::Platform:
    case SceneObjectType::Coin:
    case SceneObjectType::Spawn:
    case SceneObjectType::Cube:
    default:
        return &m_cubeMesh;
    }
}
