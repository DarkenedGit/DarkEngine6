#include "EditorApp.h"

#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Geometry/MeshGen.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/Aabb2f.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"
#include "Geometry/LineMesh.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Geometry;

namespace {

const float kPalette[][4] = {
    { 1.00f, 1.00f, 1.00f, 1.0f },
    { 0.95f, 0.35f, 0.30f, 1.0f },
    { 0.35f, 0.80f, 0.40f, 1.0f },
    { 0.30f, 0.55f, 0.95f, 1.0f },
    { 0.95f, 0.80f, 0.25f, 1.0f },
    { 0.75f, 0.40f, 0.90f, 1.0f },
    { 0.25f, 0.85f, 0.85f, 1.0f },
    { 0.95f, 0.55f, 0.20f, 1.0f },
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

void mountContentRoots(AssetManager& assets)
{
    namespace fs = std::filesystem;
    const fs::path exeDir = executableDirectory();
    const fs::path cwd    = fs::current_path();
    const fs::path candidates[] = {
        exeDir / "content",
        cwd / "content",
        exeDir / ".." / ".." / ".." / "content",
        cwd / ".." / ".." / ".." / "content",
    };
    for (const fs::path& c : candidates)
    {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
            assets.mountDirectory(c);
    }
}

void copyMatrix(float dst[16], const Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

Matrix4f makeWorldMatrix(const TransformComponent& xf)
{
    const Matrix4f S = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z);
    const Matrix4f R = xf.rotation.ToMatrix4();
    const Matrix4f T = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
    return S * R * T;
}

void copyColor(float dst[4], const float src[4])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

bool createChecker(
    Renderer& renderer,
    Texture2D& out,
    uint8_t r0, uint8_t g0, uint8_t b0,
    uint8_t r1, uint8_t g1, uint8_t b1,
    uint32_t size = 32,
    uint32_t cell = 8)
{
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4u);
    for (uint32_t y = 0; y < size; ++y)
    {
        for (uint32_t x = 0; x < size; ++x)
        {
            const bool   alt = ((x / cell) + (y / cell)) & 1u;
            const size_t i   = (static_cast<size_t>(y) * size + x) * 4u;
            px[i + 0]        = alt ? r1 : r0;
            px[i + 1]        = alt ? g1 : g0;
            px[i + 2]        = alt ? b1 : b0;
            px[i + 3]        = 255;
        }
    }
    return out.createFromRGBA(renderer, px.data(), size, size, size * 4u);
}

Vector3f defaultScale2D(SceneObjectType type)
{
    switch (type)
    {
    case SceneObjectType::Platform: return Vector3f(4.0f, 0.6f, 1.0f);
    case SceneObjectType::Coin:     return Vector3f(0.5f, 0.5f, 1.0f);
    case SceneObjectType::Spawn:    return Vector3f(0.8f, 1.4f, 1.0f);
    default:                        return Vector3f(1.0f, 1.0f, 1.0f);
    }
}

void defaultColor2D(SceneObjectType type, float out[4])
{
    switch (type)
    {
    case SceneObjectType::Platform:
        out[0] = 0.72f; out[1] = 0.52f; out[2] = 0.32f; out[3] = 1.0f;
        break;
    case SceneObjectType::Coin:
        out[0] = 0.95f; out[1] = 0.80f; out[2] = 0.25f; out[3] = 1.0f;
        break;
    case SceneObjectType::Spawn:
        out[0] = 0.20f; out[1] = 0.80f; out[2] = 0.70f; out[3] = 1.0f;
        break;
    default:
        out[0] = 1.0f; out[1] = 1.0f; out[2] = 1.0f; out[3] = 1.0f;
        break;
    }
}

void descFromSceneData(const SceneObjectData& d, ParticleEmitterDesc& out)
{
    out = ParticleEmitterDesc{};
    out.name            = d.particleName;
    out.maxParticles    = d.maxParticles;
    out.emissionRate    = d.emissionRate;
    out.duration        = d.duration;
    out.looping         = d.looping;
    out.lifetime        = { d.lifetimeMin, d.lifetimeMax };
    out.startSpeed      = { d.startSpeedMin, d.startSpeedMax };
    out.startSize       = { d.startSizeMin, d.startSizeMax };
    out.endSize         = { d.endSizeMin, d.endSizeMax };
    copyColor(out.startColor, d.startColor);
    copyColor(out.endColor, d.endColor);
    out.gravity         = d.gravity;
    out.direction       = d.direction;
    out.spreadDegrees   = d.spreadDegrees;
    out.shape           = static_cast<ParticleEmitterDesc::Shape>(d.shape);
    out.shapeSize       = d.shapeSize;
    out.additiveBlend   = d.additiveBlend;
    out.simulationSpeed = d.simulationSpeed;
    out.renderMode      = static_cast<ParticleEmitterDesc::RenderMode>(d.renderMode);
    out.ribbonCount     = d.ribbonCount;
    out.ribbonUvScale   = d.ribbonUvScale;
}

void sceneDataFromDesc(const ParticleEmitterDesc& src, SceneObjectData& d)
{
    d.hasParticle     = true;
    d.particleName    = src.name;
    d.maxParticles    = src.maxParticles;
    d.emissionRate    = src.emissionRate;
    d.duration        = src.duration;
    d.looping         = src.looping;
    d.lifetimeMin     = src.lifetime.min;
    d.lifetimeMax     = src.lifetime.max;
    d.startSpeedMin   = src.startSpeed.min;
    d.startSpeedMax   = src.startSpeed.max;
    d.startSizeMin    = src.startSize.min;
    d.startSizeMax    = src.startSize.max;
    d.endSizeMin      = src.endSize.min;
    d.endSizeMax      = src.endSize.max;
    copyColor(d.startColor, src.startColor);
    copyColor(d.endColor, src.endColor);
    d.gravity         = src.gravity;
    d.direction       = src.direction;
    d.spreadDegrees   = src.spreadDegrees;
    d.shape           = static_cast<int>(src.shape);
    d.shapeSize       = src.shapeSize;
    d.additiveBlend   = src.additiveBlend;
    d.simulationSpeed = src.simulationSpeed;
    d.renderMode      = static_cast<int>(src.renderMode);
    d.ribbonCount     = src.ribbonCount;
    d.ribbonUvScale   = src.ribbonUvScale;
}

} // namespace

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
    m_quadMesh      = Mesh::Create(renderer(), CreateQuadXY(1.0f, 1.0f));
    m_boxOutline2D  = LineMesh::Create(renderer(), CreateBoxOutlineXY());
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
    m_grid2D = LineMesh::Create(renderer(), CreateGridLinesXY(x0, y0, x1, y1, 1.0f, 3.0f));
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
        renderer().setClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        m_placeType = SceneObjectType::Cube;
    }
}

void EditorApp::newScene3D()
{
    applySceneMode(SceneMode::Scene3D);
    m_scenePath = defaultScenePath("level.json");
    m_sceneName = "level";
    clearScene();
    DE_LOG_INFO("Editor: new 3D scene");
}

void EditorApp::newScene2D()
{
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

Aabb2f EditorApp::objectBounds2D(SceneObjectType type, const Vector3f& pos, const Vector3f& scale) const
{
    Vector2f half(0.5f * std::fabs(scale.x), 0.5f * std::fabs(scale.y));
    if (type == SceneObjectType::Coin)
        half = Vector2f(0.28f, 0.28f);
    return Aabb2f::FromCenterExtents(Vector2f(pos.x, pos.y), half);
}

Entity EditorApp::pickObject2D(const Vector2f& worldPos)
{
    Entity best{};
    float  bestArea = 1.0e30f;
    for (const SceneObject& so : m_objects)
    {
        if (!isScene2DType(so.type))
            continue;
        const auto* xf = this->world().get<TransformComponent>(so.entity);
        if (!xf)
            continue;
        const Aabb2f box = objectBounds2D(so.type, xf->position, xf->scale);
        if (!box.Contains(worldPos))
            continue;
        const float area = box.Area();
        if (area < bestArea)
        {
            bestArea = area;
            best     = so.entity;
        }
    }
    return best;
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
    case SceneObjectType::Platform:
    case SceneObjectType::Coin:
    case SceneObjectType::Spawn:
    case SceneObjectType::Cube:
    default:
        return &m_cubeMesh;
    }
}

SceneObject* EditorApp::findObject(Entity e)
{
    for (SceneObject& o : m_objects)
        if (o.entity.id() == e.id())
            return &o;
    return nullptr;
}

const SceneObject* EditorApp::findObject(Entity e) const
{
    for (const SceneObject& o : m_objects)
        if (o.entity.id() == e.id())
            return &o;
    return nullptr;
}

ParticleEmitter* EditorApp::selectedEmitter()
{
    SceneObject* so = findObject(m_selected);
    if (!so || so->type != SceneObjectType::ParticleEmitter)
        return nullptr;
    if (so->emitterIndex < 0 || so->emitterIndex >= static_cast<int>(m_emitters.size()))
        return nullptr;
    return m_emitters[static_cast<size_t>(so->emitterIndex)].get();
}

ParticleEmitterDesc EditorApp::makeDefaultParticleDesc() const
{
    ParticleEmitterDesc d{};
    d.name = "Emitter";
    return d;
}

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
    a.bindKey("cycle_type", Key::T);
    a.bindKey("cycle_color", Key::C);
    a.bindKey("toggle_particle_ui", Key::F2);

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
        "Editor: F3 toggle 2D/3D | F2 particle UI | 1/2/3 place type | P place | "
        "MMB/RMB pan (2D) | wheel zoom | Ctrl+S/O save/load | C color | Del delete");
}

void EditorApp::onInit()
{
    DE_LOG_INFO("EditorApp: init");
    mountContentRoots(assets());
    registerActions();

    m_scenePath = defaultScenePath("level.json");
    m_sceneName = "level";

    if (!m_meshPipeline.create(renderer().device()) || !m_linePipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("EditorApp: mesh/line pipeline failed");
        return;
    }
    if (!m_shadows.create(renderer().device()))
    {
        DE_LOG_FATAL("EditorApp: ShadowSystem create failed");
        return;
    }
    if (!m_particleRenderer.create(renderer()))
    {
        DE_LOG_FATAL("EditorApp: particle renderer failed");
        return;
    }
    if (!m_imgui.init(window(), renderer()))
    {
        DE_LOG_WARN("EditorApp: ImGui init failed — particle UI disabled");
    }

    m_cubeMesh   = Mesh::Create(renderer(), CreateCube(1.0f));
    m_sphereMesh = Mesh::Create(renderer(), CreateSphere(0.5f, 16, 24));
    m_groundMesh = Mesh::Create(renderer(), CreateGroundPlane(40.0f, 0.0f, 10.0f));
    m_gridMesh   = LineMesh::Create(renderer(), CreateGridLines(20.0f, 40, 0.01f));

    m_propMaterial = std::make_shared<Material>();
    if (!m_propMaterial->createFromAlbedoPath(renderer(), assets(), "textures/dark_engine_cube.png", 80, 160, 220))
    {
        DE_LOG_FATAL("EditorApp: prop material failed");
        return;
    }
    assets().registerAsset(m_propMaterial);

    m_groundMaterial = std::make_shared<Material>();
    if (!m_groundMaterial->createSolid(renderer(), assets(), 48, 52, 60, 255))
    {
        DE_LOG_FATAL("EditorApp: ground material failed");
        return;
    }
    m_groundMaterial->setBaseColor(0.35f, 0.38f, 0.42f, 1.0f);
    assets().registerAsset(m_groundMaterial);
    m_propMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_groundMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 1.0f;
    m_camera.SetLens(1.04719755f, aspect, 0.05f, 500.0f);
    m_camera.LookAt(Vector3f(8.0f, 6.0f, -10.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
    m_camera2D.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));
    m_camera2D.SetOrthoHeight(16.0f);
    m_camera2D.SetClipPlanes(0.0f, 80.0f);

    if (std::filesystem::exists(m_scenePath))
        loadScene();
    else
        DE_LOG_INFO("EditorApp: no default scene at {}", m_scenePath.string());

    DE_LOG_INFO("EditorApp: ready ({} objects)", m_objects.size());
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

Entity EditorApp::pickObject(const Ray3f& ray)
{
    Entity best{};
    float bestT = 1e30f;
    for (const SceneObject& so : m_objects)
    {
        const auto* xf = world().get<TransformComponent>(so.entity);
        if (!xf)
            continue;
        Vector3f half(0.5f * xf->scale.x, 0.5f * xf->scale.y, 0.5f * xf->scale.z);
        if (so.type == SceneObjectType::ParticleEmitter)
            half = Vector3f(0.25f, 0.25f, 0.25f);
        const Aabb3f box = Aabb3f::FromCenterExtents(xf->position, half);
        Collision::RayHit3D hit = Collision::Intersect(ray, box);
        if (hit.hit && hit.t >= 0.0f && hit.t < bestT)
        {
            bestT = hit.t;
            best  = so.entity;
        }
    }
    return best;
}

Entity EditorApp::spawnObject(
    SceneObjectType type,
    const Vector3f& pos,
    const Vector3f& scale,
    const Quaternion& rot,
    const float color[4],
    const ParticleEmitterDesc* particleDesc)
{
    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, toString(type));

    TransformComponent xf{};
    xf.position = pos;
    xf.scale    = scale;
    xf.rotation = rot;
    if (m_sceneMode == SceneMode::Scene3D && isScene3DType(type) && type != SceneObjectType::ParticleEmitter
        && xf.position.y < 0.5f * xf.scale.y)
        xf.position.y = 0.5f * xf.scale.y;
    world().emplace<TransformComponent>(e, xf);

    auto& mc = world().emplace<MeshComponent>(e);
    mc.matAssetID  = m_propMaterial ? m_propMaterial->id : NULL_ASSET;
    mc.meshAssetID = NULL_ASSET;

    SceneObject so{};
    so.entity = e;
    so.type   = type;
    copyColor(so.color, color);
    so.emitterIndex = -1;

    if (type == SceneObjectType::ParticleEmitter)
    {
        auto emitter = std::make_unique<ParticleEmitter>();
        ParticleEmitterDesc desc = particleDesc ? *particleDesc : makeDefaultParticleDesc();
        emitter->setDesc(desc);
        emitter->setTransform(xf.position, xf.rotation);
        emitter->play();
        so.emitterIndex = static_cast<int>(m_emitters.size());
        m_emitters.push_back(std::move(emitter));
        // Visual marker scale
        if (auto* t = world().get<TransformComponent>(e))
            t->scale = Vector3f(0.35f, 0.35f, 0.35f);
    }

    m_objects.push_back(so);
    m_selected = e;
    DE_LOG_INFO("Editor: spawn {} #{} (emitters={})", toString(type), e.id(), m_emitters.size());
    return e;
}

Entity EditorApp::placeAtCursor(SceneObjectType type)
{
    if (m_sceneMode == SceneMode::Scene2D)
    {
        if (!isScene2DType(type))
            type = SceneObjectType::Platform;
        Vector2f p{};
        worldFromMouse2D(p);
        if (m_gridSnap > 0.0f)
        {
            p.x = snap(p.x, m_gridSnap);
            p.y = snap(p.y, m_gridSnap);
        }
        float col[4]{};
        defaultColor2D(type, col);
        return spawnObject(type, Vector3f(p.x, p.y, 0.0f), defaultScale2D(type), Quaternion::IDENTITY, col, nullptr);
    }

    Vector3f hit{};
    bool ok = groundHitFromMouse(hit);
    if (!ok)
    {
        Ray3f ray(m_camera.GetPosition(), m_camera.GetLook());
        ok = groundHitFromRay(ray, hit);
    }
    if (!ok)
    {
        DE_LOG_WARN("Editor: place failed (no ground hit)");
        return {};
    }
    if (m_gridSnap > 0.0f)
    {
        hit.x = snap(hit.x, m_gridSnap);
        hit.z = snap(hit.z, m_gridSnap);
    }
    const float* col = kPalette[m_colorIndex % kPaletteCount];
    Vector3f scale(1, 1, 1);
    if (type == SceneObjectType::ParticleEmitter)
        hit.y = 0.5f;
    else
        hit.y = 0.5f * scale.y;
    return spawnObject(type, hit, scale, Quaternion::IDENTITY, col, nullptr);
}

void EditorApp::deleteSelected()
{
    if (!m_selected.valid())
        return;
    if (SceneObject* so = findObject(m_selected))
    {
        if (so->type == SceneObjectType::ParticleEmitter && so->emitterIndex >= 0)
        {
            // Null out emitter slot (keep indices stable)
            if (so->emitterIndex < static_cast<int>(m_emitters.size()))
                m_emitters[static_cast<size_t>(so->emitterIndex)].reset();
        }
    }
    world().destroyEntity(m_selected);
    std::erase_if(m_objects, [&](const SceneObject& o) { return o.entity.id() == m_selected.id(); });
    m_selected = {};
    m_dragging = false;
    DE_LOG_INFO("Editor: deleted ({} remaining)", m_objects.size());
}

void EditorApp::selectNext(int delta)
{
    if (m_objects.empty())
    {
        m_selected = {};
        return;
    }
    int idx = 0;
    if (m_selected.valid())
    {
        for (size_t i = 0; i < m_objects.size(); ++i)
            if (m_objects[i].entity.id() == m_selected.id())
            {
                idx = static_cast<int>(i);
                break;
            }
        idx += delta;
    }
    const int n = static_cast<int>(m_objects.size());
    idx = ((idx % n) + n) % n;
    m_selected = m_objects[static_cast<size_t>(idx)].entity;
}

void EditorApp::cyclePlaceType(int delta)
{
    const SceneObjectType types3D[] = {
        SceneObjectType::Cube, SceneObjectType::Sphere, SceneObjectType::ParticleEmitter
    };
    const SceneObjectType types2D[] = {
        SceneObjectType::Platform, SceneObjectType::Coin, SceneObjectType::Spawn
    };
    const SceneObjectType* types = (m_sceneMode == SceneMode::Scene2D) ? types2D : types3D;
    const int n = 3;
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (types[i] == m_placeType)
            idx = i;
    idx = ((idx + delta) % n + n) % n;
    m_placeType = types[idx];
    DE_LOG_INFO("Editor: place type = {}", toString(m_placeType));
}

void EditorApp::cycleSelectedColor()
{
    m_colorIndex = (m_colorIndex + 1) % kPaletteCount;
    const float* col = kPalette[m_colorIndex];
    if (SceneObject* so = findObject(m_selected))
        copyColor(so->color, col);
}

void EditorApp::clearScene()
{
    for (SceneObject& o : m_objects)
        world().destroyEntity(o.entity);
    m_objects.clear();
    m_emitters.clear();
    m_selected = {};
    m_dragging = false;
}

bool EditorApp::saveScene()
{
    SceneFileData data{};
    data.version  = 1;
    data.name     = m_sceneName;
    data.mode     = m_sceneMode;
    data.worldMin = m_worldMin;
    data.worldMax = m_worldMax;

    for (const SceneObject& so : m_objects)
    {
        const auto* xf = world().get<TransformComponent>(so.entity);
        if (!xf)
            continue;
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
        data.objects.push_back(d);
    }

    std::string err;
    if (!saveSceneToJson(m_scenePath, data, &err))
    {
        DE_LOG_ERROR("Editor: save failed — {}", err);
        return false;
    }
    DE_LOG_INFO("Editor: saved {} objects → {}", data.objects.size(), m_scenePath.string());
    return true;
}

bool EditorApp::loadScene()
{
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

    for (const SceneObjectData& d : data.objects)
    {
        ParticleEmitterDesc pdesc = makeDefaultParticleDesc();
        if (d.hasParticle)
            descFromSceneData(d, pdesc);
        spawnObject(d.type, d.position, d.scale, d.rotation, d.color,
                    d.type == SceneObjectType::ParticleEmitter ? &pdesc : nullptr);
    }
    m_selected = {};
    DE_LOG_INFO("Editor: loaded {} objects", m_objects.size());
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
        if (input().keyPressed(Key::F9) || (ctrl && input().keyPressed(Key::O)))
            loadScene();
        if (input().actionPressed("toggle_particle_ui"))
            m_showParticlePanel = !m_showParticlePanel;
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
        if (input().actionPressed("cycle_type"))
            cyclePlaceType(+1);
        if (input().actionPressed("cycle_color"))
            cycleSelectedColor();
        if (input().actionPressed("delete"))
            deleteSelected();
        if (input().actionPressed("place"))
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

    if (!uiMouse && m_dragging && m_selected.valid() && input().mouseDown(MouseButton::Left))
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
                    SceneObject* so = findObject(m_selected);
                    if (so && so->type == SceneObjectType::ParticleEmitter)
                        xf->position.y = 0.5f;
                    else
                        xf->position.y = 0.5f * xf->scale.y;

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

void EditorApp::drawEditorUi()
{
    if (!m_imgui.isReady())
        return;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New 3D Scene"))
                newScene3D();
            if (ImGui::MenuItem("New 2D Scene"))
                newScene2D();
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                saveScene();
            if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
                loadScene();
            if (ImGui::MenuItem("Open 3D Level"))
            {
                m_scenePath = defaultScenePath("level.json");
                loadScene();
            }
            if (ImGui::MenuItem("Open 2D Level"))
            {
                m_scenePath = defaultScenePath("level2d.json");
                loadScene();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))
                requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            bool mode2d = m_sceneMode == SceneMode::Scene2D;
            if (ImGui::MenuItem("2D Scene", "F3", mode2d))
                applySceneMode(mode2d ? SceneMode::Scene3D : SceneMode::Scene2D);
            ImGui::MenuItem("Particle Panel", "F2", &m_showParticlePanel);
            ImGui::MenuItem("Grid", nullptr, &m_showGrid);
            ImGui::MenuItem("Solid Ground", nullptr, &m_showSolid);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
        {
            if (m_sceneMode == SceneMode::Scene2D)
            {
                if (ImGui::MenuItem("Platform", "1+P"))
                {
                    m_placeType = SceneObjectType::Platform;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem("Coin", "2+P"))
                {
                    m_placeType = SceneObjectType::Coin;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem("Player Spawn", "3+P"))
                {
                    m_placeType = SceneObjectType::Spawn;
                    placeAtCursor(m_placeType);
                }
            }
            else
            {
                if (ImGui::MenuItem("Cube", "1+P"))
                {
                    m_placeType = SceneObjectType::Cube;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem("Sphere", "2+P"))
                {
                    m_placeType = SceneObjectType::Sphere;
                    placeAtCursor(m_placeType);
                }
                if (ImGui::MenuItem("Particle Emitter", "3+P"))
                {
                    m_placeType = SceneObjectType::ParticleEmitter;
                    placeAtCursor(m_placeType);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Text("  |  %s  objects:%d  place:%s", toString(m_sceneMode), (int)m_objects.size(), toString(m_placeType));
        ImGui::EndMainMenuBar();
    }

    if (m_showParticlePanel)
    {
        if (ParticleEmitter* em = selectedEmitter())
        {
            m_particlePanel.draw(*em, &m_showParticlePanel);
            // Keep pool size / desc in sync when UI mutates desc fields that need re-capacity
            // (panel already calls setDesc on maxParticles change)
            uint32_t burst = 0;
            if (m_particlePanel.consumeBurstRequest(burst))
                em->emitBurst(burst);

            // Push transform from entity
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
                if (ImGui::Button("Create Emitter at Cursor"))
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

    // Hierarchy
    if (ImGui::Begin("Scene"))
    {
        ImGui::TextUnformatted(m_sceneMode == SceneMode::Scene2D ? "Mode: 2D" : "Mode: 3D");
        ImGui::TextWrapped("%s", m_scenePath.string().c_str());
        ImGui::Separator();
        for (size_t i = 0; i < m_objects.size(); ++i)
        {
            SceneObject& so = m_objects[i];
            const bool selected = m_selected.valid() && m_selected.id() == so.entity.id();
            ImGui::PushID(static_cast<int>(so.entity.id()));
            char label[128];
            std::snprintf(label, sizeof(label), "%s##%u", toString(so.type), so.entity.id());
            if (ImGui::Selectable(label, selected))
                m_selected = so.entity;
            ImGui::PopID();
        }
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
        if (SceneObject* so = findObject(m_selected))
        {
            if (auto* xf = world().get<TransformComponent>(so->entity))
            {
                ImGui::Text("Selected: %s", toString(so->type));
                float pos[2] = { xf->position.x, xf->position.y };
                float size[2] = { xf->scale.x, xf->scale.y };
                if (ImGui::DragFloat2("Position", pos, 0.05f))
                {
                    xf->position.x = pos[0];
                    xf->position.y = pos[1];
                }
                if (ImGui::DragFloat2("Size", size, 0.05f, 0.05f, 200.0f))
                {
                    xf->scale.x = Max(0.05f, size[0]);
                    xf->scale.y = Max(0.05f, size[1]);
                }
                ImGui::ColorEdit3("Tint", so->color);
                if (ImGui::Button("Delete"))
                    deleteSelected();
            }
        }
        else
        {
            ImGui::TextUnformatted("No selection. Click an object or press P to place.");
        }
        ImGui::End();
    }
}

void EditorApp::onUpdate(float dt)
{
    updateCamera(dt);
    handleEditorCommands(dt);

    // Simulate particles
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        SceneObject& so = m_objects[i];
        if (so.type != SceneObjectType::ParticleEmitter || so.emitterIndex < 0)
            continue;
        if (so.emitterIndex >= static_cast<int>(m_emitters.size()) || !m_emitters[static_cast<size_t>(so.emitterIndex)])
            continue;
        if (const auto* xf = world().get<TransformComponent>(so.entity))
            m_emitters[static_cast<size_t>(so.emitterIndex)]->setTransform(xf->position, xf->rotation);
        m_emitters[static_cast<size_t>(so.emitterIndex)]->update(dt);
    }
}

void EditorApp::onRender()
{
    renderer().beginFrame();
    auto* cmd = renderer().commandList();

    if (m_imgui.isReady())
        m_imgui.beginFrame();

    if (m_sceneMode == SceneMode::Scene2D)
        renderScene2D(cmd);
    else
        renderScene3D(cmd);

    if (m_imgui.isReady())
    {
        drawEditorUi();
        m_imgui.render(renderer());
    }

    renderer().endFrame();
}

void EditorApp::renderScene2D(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !ensure2DResources())
        return;

    m_spritePipe.bind(cmd);

    auto drawObj = [&](const SceneObject& so, const TransformComponent& xf) {
        const bool selected = m_selected.valid() && m_selected.id() == so.entity.id();
        float cr = so.color[0], cg = so.color[1], cb = so.color[2];
        if (selected)
        {
            cr = cr * 0.55f + 1.0f * 0.45f;
            cg = cg * 0.55f + 0.85f * 0.45f;
            cb = cb * 0.55f + 0.20f * 0.45f;
        }
        const Vector2f pos(xf.position.x, xf.position.y);
        const Vector2f size(std::fabs(xf.scale.x), std::fabs(xf.scale.y));
        const Texture2D* tex = &m_texPlatform;
        float z = 2.0f;
        float uvx = size.x;
        float uvy = size.y;
        if (so.type == SceneObjectType::Coin)
        {
            tex = &m_texCoin;
            z   = 1.2f;
            uvx = 1.0f;
            uvy = 1.0f;
        }
        else if (so.type == SceneObjectType::Spawn)
        {
            tex = &m_texSpawn;
            z   = 1.0f;
            uvx = 1.0f;
            uvy = 1.0f;
        }
        drawSprite2D(cmd, *tex, pos, size, z, cr, cg, cb, uvx, uvy);
    };

    for (const SceneObject& so : m_objects)
    {
        if (!isScene2DType(so.type))
            continue;
        const auto* xf = world().get<TransformComponent>(so.entity);
        if (xf)
            drawObj(so, *xf);
    }

    if (m_showGrid && m_grid2D.valid())
    {
        m_linePipeline.bind(cmd);
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, m_camera2D.GetViewProj());
        lc.color[0] = 0.20f;
        lc.color[1] = 0.35f;
        lc.color[2] = 0.50f;
        lc.color[3] = 1.0f;
        m_linePipeline.setConstants(cmd, lc);
        m_grid2D.draw(cmd);
    }

    if (m_selected.valid() && m_boxOutline2D.valid())
    {
        if (const SceneObject* so = findObject(m_selected))
        {
            if (const auto* xf = world().get<TransformComponent>(so->entity))
            {
                const Aabb2f box = objectBounds2D(so->type, xf->position, xf->scale);
                const Vector2f c = box.Center();
                const Vector2f s = box.Size();
                const Matrix4f world = Matrix4f::ScaleMatrixXYZ(s.x, s.y, 1.0f)
                    * Matrix4f::TranslationMatrix(c.x, c.y, 0.4f);
                m_linePipeline.bind(cmd);
                LineFrameConstants lc{};
                copyMatrix(lc.worldViewProj, world * m_camera2D.GetViewProj());
                lc.color[0] = 1.0f;
                lc.color[1] = 0.85f;
                lc.color[2] = 0.15f;
                lc.color[3] = 1.0f;
                m_linePipeline.setConstants(cmd, lc);
                m_boxOutline2D.draw(cmd);
            }
        }
    }

    renderer().stats().drawCalls = static_cast<uint32_t>(m_objects.size() + 2);
}

void EditorApp::renderScene3D(ID3D12GraphicsCommandList* cmd)
{
    const Matrix4f viewProj = m_camera.GetViewProj();
    const Vector3f lightDir(0.35f, 0.85f, -0.35f);
    Aabb3f sceneBounds(Vector3f(-22.0f, -2.0f, -22.0f), Vector3f(22.0f, 16.0f, 22.0f));
    for (const SceneObject& so : m_objects)
    {
        if (const auto* xf = world().get<TransformComponent>(so.entity))
            sceneBounds.ExpandToInclude(xf->position);
    }
    m_shadows.update(m_camera, lightDir, sceneBounds, 0.8f, 0.20f);
    if (m_shadows.isValid() && m_shadows.enabled())
    {
        m_shadows.beginCapture(cmd);
        for (int i = 0; i < m_shadows.cascadeCount(); ++i)
        {
            m_shadows.beginCascade(cmd, i);
            if (m_showSolid && m_groundMesh.valid())
                m_groundMesh.draw(cmd);
            for (const SceneObject& so : m_objects)
            {
                const auto* xf = world().get<TransformComponent>(so.entity);
                const Mesh* mesh = meshForType(so.type);
                if (!xf || !mesh || !mesh->valid())
                    continue;
                const Matrix4f worldMat = makeWorldMatrix(*xf);
                const Matrix4f wvp      = worldMat * m_shadows.cascade(i).viewProj;
                m_shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
                mesh->draw(cmd);
            }
        }
        m_shadows.endCapture(cmd);
        renderer().bindSceneTargets();
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    auto drawMesh = [&](const Mesh& mesh, const Matrix4f& world, Material* material, float cr, float cg, float cb) {
        m_meshPipeline.bind(cmd);
        if (material && material->isValid())
            material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
        MeshFrameConstants cbData{};
        const Matrix4f wvp = world * viewProj;
        copyMatrix(cbData.worldViewProj, wvp);
        copyMatrix(cbData.world, world);
        cbData.color[0] = cr;
        cbData.color[1] = cg;
        cbData.color[2] = cb;
        cbData.color[3] = 1.0f;
        cbData.lightDirWS[0] = lightDir.x;
        cbData.lightDirWS[1] = lightDir.y;
        cbData.lightDirWS[2] = lightDir.z;
        cbData.ambientScale  = 0.22f;
        cbData.lightColor[0] = 1.0f;
        cbData.lightColor[1] = 0.96f;
        cbData.lightColor[2] = 0.88f;
        const Vector3f cam = m_camera.GetPosition();
        cbData.cameraPos[0] = cam.x;
        cbData.cameraPos[1] = cam.y;
        cbData.cameraPos[2] = cam.z;
        m_meshPipeline.setConstants(cmd, cbData);
        mesh.draw(cmd);
    };

    if (m_showSolid && m_groundMesh.valid())
        drawMesh(m_groundMesh, Matrix4f{}, m_groundMaterial.get(), 0.45f, 0.48f, 0.52f);

    if (m_showGrid && m_gridMesh.valid())
    {
        m_linePipeline.bind(cmd);
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, viewProj);
        lc.color[0] = 0.25f;
        lc.color[1] = 0.55f;
        lc.color[2] = 0.75f;
        lc.color[3] = 1.0f;
        m_linePipeline.setConstants(cmd, lc);
        m_gridMesh.draw(cmd);
    }

    uint32_t draws = 0;
    for (const SceneObject& so : m_objects)
    {
        if (!isScene3DType(so.type))
            continue;
        const auto* xf = world().get<TransformComponent>(so.entity);
        if (!xf)
            continue;
        const Mesh* mesh = meshForType(so.type);
        if (!mesh || !mesh->valid())
            continue;

        const bool selected = m_selected.valid() && m_selected.id() == so.entity.id();
        float cr = so.color[0], cg = so.color[1], cb = so.color[2];
        if (so.type == SceneObjectType::ParticleEmitter)
        {
            cr = 0.2f;
            cg = 0.9f;
            cb = 1.0f;
        }
        if (selected)
        {
            cr = cr * 0.55f + 1.0f * 0.45f;
            cg = cg * 0.55f + 0.85f * 0.45f;
            cb = cb * 0.55f + 0.20f * 0.45f;
        }
        drawMesh(*mesh, makeWorldMatrix(*xf), m_propMaterial.get(), cr, cg, cb);
        ++draws;
    }

    // Particles (after opaque, depth write off)
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        const SceneObject& so = m_objects[i];
        if (so.type != SceneObjectType::ParticleEmitter || so.emitterIndex < 0)
            continue;
        if (so.emitterIndex >= static_cast<int>(m_emitters.size()) || !m_emitters[static_cast<size_t>(so.emitterIndex)])
            continue;
        ParticleEmitter& em = *m_emitters[static_cast<size_t>(so.emitterIndex)];
        m_particleRenderer.draw(cmd, m_camera, em, em.desc().additiveBlend);
        ++draws;
    }

    renderer().stats().drawCalls = draws;
}

void EditorApp::onShutdown()
{
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
    m_objects.clear();
    DE_LOG_INFO("EditorApp: shutdown");
}
