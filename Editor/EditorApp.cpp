#include "EditorApp.h"

#include "Editor/SceneFile.h"
#include "ECS/Components.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Geometry/MeshGen.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"

#include <imgui.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

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
}

} // namespace

float EditorApp::snap(float v, float grid)
{
    if (grid <= 0.0f)
        return v;
    return std::floor(v / grid + 0.5f) * grid;
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
        "Editor: F2 particle UI | 1/2/3 cube/sphere/emitter | P place | Ctrl+S/O save/load | "
        "C color | Del delete");
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

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 1.0f;
    m_camera.SetLens(1.04719755f, aspect, 0.05f, 500.0f);
    m_camera.LookAt(Vector3f(8.0f, 6.0f, -10.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    if (std::filesystem::exists(m_scenePath))
        loadScene();
    else
        DE_LOG_INFO("EditorApp: no default scene at {}", m_scenePath.string());

    DE_LOG_INFO("EditorApp: ready ({} objects)", m_objects.size());
}

void EditorApp::updateCamera(float dt)
{
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
    if (type != SceneObjectType::ParticleEmitter && xf.position.y < 0.5f * xf.scale.y)
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
    int t = static_cast<int>(m_placeType) + delta;
    const int n = static_cast<int>(SceneObjectType::Count);
    t = ((t % n) + n) % n;
    m_placeType = static_cast<SceneObjectType>(t);
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
    data.version = 1;
    data.name    = m_sceneName;

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
    m_sceneName = data.name.empty() ? "level" : data.name;

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
        if (input().actionPressed("type_cube"))
            m_placeType = SceneObjectType::Cube;
        if (input().actionPressed("type_sphere"))
            m_placeType = SceneObjectType::Sphere;
        if (input().actionPressed("type_particle"))
            m_placeType = SceneObjectType::ParticleEmitter;
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
        const Ray3f ray = m_camera.ScreenPointToRay(
            static_cast<float>(input().mouseX()),
            static_cast<float>(input().mouseY()),
            static_cast<float>(renderer().width()),
            static_cast<float>(renderer().height()));
        Entity hit = pickObject(ray);
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

void EditorApp::drawEditorUi()
{
    if (!m_imgui.isReady())
        return;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                saveScene();
            if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
                loadScene();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))
                requestQuit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Particle Panel", "F2", &m_showParticlePanel);
            ImGui::MenuItem("Grid", nullptr, &m_showGrid);
            ImGui::MenuItem("Solid Ground", nullptr, &m_showSolid);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
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
            ImGui::EndMenu();
        }
        ImGui::Text("  |  objects:%d  place:%s", (int)m_objects.size(), toString(m_placeType));
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

    const Matrix4f viewProj = m_camera.GetViewProj();

    auto drawMesh = [&](const Mesh& mesh, const Matrix4f& world, Material* material, float cr, float cg, float cb) {
        m_meshPipeline.bind(cmd);
        if (material && material->isValid())
            material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        MeshFrameConstants cbData{};
        const Matrix4f wvp = world * viewProj;
        copyMatrix(cbData.worldViewProj, wvp);
        copyMatrix(cbData.world, world);
        cbData.color[0] = cr;
        cbData.color[1] = cg;
        cbData.color[2] = cb;
        cbData.color[3] = 1.0f;
        cbData.lightDirWS[0] = 0.35f;
        cbData.lightDirWS[1] = 0.85f;
        cbData.lightDirWS[2] = -0.35f;
        cbData.ambientScale  = 0.22f;
        cbData.lightColor[0] = 1.0f;
        cbData.lightColor[1] = 0.96f;
        cbData.lightColor[2] = 0.88f;
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

    if (m_imgui.isReady())
    {
        drawEditorUi();
        m_imgui.render(renderer());
    }

    renderer().stats().drawCalls = draws;
    renderer().endFrame();
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
    m_emitters.clear();
    m_objects.clear();
    DE_LOG_INFO("EditorApp: shutdown");
}
