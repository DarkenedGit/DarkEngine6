#include "EditorApp.h"

#include "Assets/Model.h"
#include "Character/PlayerMotorComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsFile.h"
#include "Scene/EntityMaster.h"
#include "Scene/SceneTypes.h"
#include "Core/ContentRoots.h"
#include "Editor/EditorFileDialog.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace Dark::Math;

namespace
{
    bool scaleNear(const Dark::Math::Vector3f& a, const Dark::Math::Vector3f& b)
    {
        return std::fabs(a.x - b.x) < 1.0e-4f && std::fabs(a.y - b.y) < 1.0e-4f && std::fabs(a.z - b.z) < 1.0e-4f;
    }

    float estimatedMass(const Dark::PhysicsComponent& phys, const Dark::Math::Vector3f& scale)
    {
        if (phys.shape == Dark::PhysicsShapeKind::Box)
            return phys.density * std::fabs(scale.x * scale.y * scale.z);
        if (phys.shape == Dark::PhysicsShapeKind::Sphere)
        {
            const float r = 0.5f * std::max(std::fabs(scale.x), std::max(std::fabs(scale.y), std::fabs(scale.z)));
            return phys.density * (4.0f / 3.0f * 3.14159265f * r * r * r);
        }
        return 0.0f;
    }
}

void EditorApp::ensurePhysicsWorld()
{
    if (m_physics.valid())
        return;

    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    if (!m_physics.create(desc))
    {
        DE_LOG_ERROR(LogCategory::Collision, "Editor: physics world create failed");
        return;
    }
    m_surfaces.loadFromContent();
    rebuildPhysicsGround();
}

// Physics ground = the terrain's coarse height map as a Box3D height field.
// No terrain (or height field failed) → flat stand-in box with its top at Y = 0.
// Called when the world is created and again on Play, so Generate / sculpt edits are picked up.
void EditorApp::rebuildPhysicsGround()
{
    if (!m_physics.valid())
        return;

    if (m_physicsGround != Physics::kNullPhysicsBody)
    {
        m_physics.destroyBody(m_physicsGround);
        m_physicsGround = Physics::kNullPhysicsBody;
    }

    const Terrain::HeightMap& coarse = m_terrain.coarse();
    if (m_haveTerrain && coarse.valid())
    {
        Physics::PhysicsHeightFieldDesc hf;
        hf.heights     = coarse.samples();
        hf.countX      = coarse.width();
        hf.countZ      = coarse.height();
        hf.cellSize    = coarse.cellSize();
        hf.heightScale = coarse.heightScale();
        hf.origin      = coarse.origin();
        hf.friction    = 0.8f;
        m_physicsGround = m_physics.createHeightField(hf);
        if (m_physicsGround != Physics::kNullPhysicsBody)
        {
            DE_LOG_INFO(LogCategory::Collision, "Editor: physics ground = terrain height field {}x{} (cell {:.2f} m)", hf.countX, hf.countZ, hf.cellSize);
            return;
        }
        DE_LOG_WARN(LogCategory::Collision, "Editor: terrain height field failed; using flat ground");
    }

    Physics::PhysicsBoxDesc ground;
    ground.position    = Vector3f(0.0f, -1.0f, 0.0f);
    ground.halfExtents = Vector3f(2000.0f, 1.0f, 2000.0f);
    ground.dynamic     = false;
    ground.friction    = 0.8f;
    m_physicsGround    = m_physics.createBox(ground);
    if (m_physicsGround == Physics::kNullPhysicsBody)
        DE_LOG_WARN(LogCategory::Collision, "Editor: physics ground plane failed");
}

float EditorApp::playGroundHeight(float x, float z) const
{
    float groundY = m_terrain.heightAtWorld(x, z);
    if (!m_physics.valid())
        return groundY;

    Physics::PhysicsWorld::GroundProbe probe;
    probe.from       = Vector3f(x, m_playGroundProbeY, z);
    probe.radius     = 0.3f;
    probe.maxDrop    = (m_playGroundProbeY - groundY) + 1.0f;
    probe.ignore     = m_playGroundIgnore;
    probe.ignore2    = m_physicsGround;
    probe.staticOnly = true;

    float surfaceY = 0.0f;
    if (probe.maxDrop > 0.0f && m_physics.probeGround(probe, surfaceY) && surfaceY > groundY)
        groundY = surfaceY;
    return groundY;
}

void EditorApp::ensurePlayPhysicsVolumes()
{
    if (!m_playMode || !m_playPlayer.valid())
        return;
    ensurePhysicsWorld();
    if (!m_physics.valid())
        return;

    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const bool player = e.id() == m_playPlayer.id();
        const bool solid  = so.type == SceneObjectType::Cube || so.type == SceneObjectType::Sphere;
        if (!player && !solid)
            return;
        if (const PhysicsComponent* existing = world().get<PhysicsComponent>(e))
        {
            if (existing->enabled && !m_physics.isBound(e))
                rebuildPhysicsBody(e);
            return;
        }

        PhysicsComponent phys;
        phys.enabled      = true;
        phys.density      = 1.0f;
        phys.friction     = 0.6f;
        phys.gravityScale = 1.0f;
        if (player)
        {
            phys.mode          = PhysicsBodyMode::Kinematic;
            phys.shape         = PhysicsShapeKind::Capsule;
            phys.fixedRotation = true;
        }
        else if (so.type == SceneObjectType::Sphere)
        {
            phys.mode  = PhysicsBodyMode::Static;
            phys.shape = PhysicsShapeKind::Sphere;
        }
        else
        {
            phys.mode  = PhysicsBodyMode::Static;
            phys.shape = PhysicsShapeKind::Box;
        }
        world().emplace<PhysicsComponent>(e, phys);
        rebuildPhysicsBody(e);
    });
}

void EditorApp::releasePhysicsBody(Entity e)
{
    if (m_physics.valid())
        m_physics.destroyBody(world(), e);
}

const Model* EditorApp::modelForPhysics(Entity e, AssetRef<Model>& held)
{
    held.reset();
    const ModelComponent* mc = world().get<ModelComponent>(e);
    if (!mc || mc->modelAssetID == NULL_ASSET)
        return nullptr;
    held = assets().getAs<Model>(mc->modelAssetID);
    return held.get();
}

void EditorApp::rebuildPhysicsBody(Entity e)
{
    ensurePhysicsWorld();
    if (!m_physics.valid() || !world().alive(e))
        return;
    AssetRef<Model> held;
    const Model*    model = modelForPhysics(e, held);
    if (!Physics::bindPhysicsEntity(m_physics, world(), e, model, &m_surfaces))
        DE_LOG_WARN(LogCategory::Collision, "Editor: physics bind failed for #{}", e.id());
}

namespace
{
    std::string entityTypeKey(const EditorObjectComponent* so, const Model* model)
    {
        if (so)
        {
            switch (so->type)
            {
            case SceneObjectType::Cube:   return "cube";
            case SceneObjectType::Sphere: return "sphere";
            case SceneObjectType::Player: return "player";
            case SceneObjectType::Wolf:   return "wolf";
            default: break;
            }
        }
        if (model && !model->sourcePath().empty())
            return model->sourcePath().stem().string();
        return {};
    }
}

void EditorApp::tryLoadModelPhysics(Entity e)
{
    if (!world().alive(e) || world().has<PhysicsComponent>(e))
        return;
    AssetRef<Model> held;
    const Model*    model = modelForPhysics(e, held);
    const EditorObjectComponent* so = findObject(e);
    const std::string typeKey = entityTypeKey(so, model);
    const EntityMaster master = typeKey.empty() ? EntityMaster{} : loadEntityMasterType(typeKey);
    PhysicsComponent loaded;
    bool             loadedFile = false;
    if (!master.physics.empty())
    {
        const std::filesystem::path file = resolveContentFile(master.physics);
        loadedFile = !file.empty() && Physics::loadPhysicsSettingsFile(file, loaded);
    }
    if (!loadedFile && model && !model->sourcePath().empty())
        loadedFile = Physics::loadPhysicsSettingsForModel(model->sourcePath(), loaded);
    if (!loadedFile)
        return;
    world().emplace<PhysicsComponent>(e, std::move(loaded));
    rebuildPhysicsBody(e);
    DE_LOG_INFO(LogCategory::Collision, "Editor: loaded physics for '{}'", typeKey.empty() ? model->sourcePath().filename().string() : typeKey);
}

std::filesystem::path EditorApp::physicsFileSuggestion(Entity e)
{
    AssetRef<Model> held;
    const Model*    model = modelForPhysics(e, held);
    const EditorObjectComponent* so = findObject(e);
    const std::string typeKey = entityTypeKey(so, model);
    if (!typeKey.empty())
    {
        const EntityMaster master = loadEntityMasterType(typeKey);
        if (!master.physics.empty())
            return authoringContentFile(master.physics);
    }
    if (model && !model->sourcePath().empty())
        return Physics::physicsSidecarAuthoringPath(model->sourcePath());
    const std::filesystem::path root = authoringContentRoot();
    if (root.empty())
        return std::filesystem::path("object.physics.json");
    return root / "physics" / "object.physics.json";
}

bool EditorApp::savePhysicsWithDialog(Entity e)
{
    const PhysicsComponent* phys = world().get<PhysicsComponent>(e);
    if (!phys)
        return false;
    std::filesystem::path chosen;
    if (!pickEditorFile(window().nativeHandle(), true, L"Save Physics", L"Physics (*.physics.json;*.json)\0*.physics.json;*.json\0All files (*.*)\0*.*\0",
                        L"json", physicsFileSuggestion(e), chosen))
        return false;
    if (!Physics::savePhysicsSettingsFile(chosen, *phys))
        return false;
    if (m_sfxSave)
        audio().play2D(m_sfxSave, 0.45f);
    DE_LOG_INFO(LogCategory::Collision, "Editor: saved physics '{}'", chosen.string());
    return true;
}

bool EditorApp::loadPhysicsWithDialog(Entity e)
{
    if (!world().alive(e) || !world().get<PhysicsComponent>(e))
        return false;
    std::filesystem::path chosen;
    if (!pickEditorFile(window().nativeHandle(), false, L"Load Physics", L"Physics (*.physics.json;*.json)\0*.physics.json;*.json\0All files (*.*)\0*.*\0",
                        L"json", physicsFileSuggestion(e), chosen))
        return false;
    PhysicsComponent loaded;
    if (!Physics::loadPhysicsSettingsFile(chosen, loaded))
    {
        DE_LOG_WARN(LogCategory::Collision, "Editor: could not load physics '{}'", chosen.string());
        return false;
    }
    world().emplace<PhysicsComponent>(e, std::move(loaded));
    rebuildPhysicsBody(e);
    DE_LOG_INFO(LogCategory::Collision, "Editor: loaded physics '{}'", chosen.string());
    return true;
}

bool EditorApp::savePhysicsForEntity(Entity e)
{
    return savePhysicsWithDialog(e);
}

PhysicsComponent EditorApp::defaultPhysicsFor(Entity e)
{
    PhysicsComponent phys;
    phys.enabled        = true;
    phys.density        = 1.0f;
    phys.friction       = 0.6f;
    phys.angularDamping = 0.05f;
    phys.gravityScale   = 1.0f;
    const EditorObjectComponent* so = findObject(e);
    if (so && so->type == SceneObjectType::Sphere)
    {
        phys.shape = PhysicsShapeKind::Sphere;
        phys.mode  = PhysicsBodyMode::Dynamic;
    }
    else if (so && isPawnType(so->type))
    {
        phys.shape         = PhysicsShapeKind::Capsule;
        phys.mode          = PhysicsBodyMode::Kinematic;
        phys.fixedRotation = true;
    }
    else if (world().has<ModelComponent>(e))
    {
        phys.shape = PhysicsShapeKind::Model;
        phys.mode  = PhysicsBodyMode::Dynamic;
    }
    else
    {
        phys.shape = PhysicsShapeKind::Box;
        phys.mode  = PhysicsBodyMode::Dynamic;
    }
    return phys;
}

void EditorApp::syncEditPhysics()
{
    if (!m_physics.valid() || m_playMode)
        return;
    m_physics.step(0.0f, world());
    world().each<PhysicsComponent>([&](Entity e, PhysicsComponent& phys) {
        if (!phys.enabled)
        {
            if (m_physics.isBound(e))
                m_physics.destroyBody(world(), e);
            return;
        }
        const TransformComponent*   xf   = world().get<TransformComponent>(e);
        const PhysicsBodyComponent* body = world().get<PhysicsBodyComponent>(e);
        const bool                  scaleChanged = xf && body && body->valid && !scaleNear(body->bakedScale, xf->scale);
        if (!m_physics.isBound(e) || !body || !body->valid || scaleChanged)
            rebuildPhysicsBody(e);
    });
    m_physics.pushPoses(world(), true);
}

void EditorApp::simulatePhysics(float dt)
{
    if (!m_physics.valid())
        return;

    // The motor owns the player. Keep that pose even if a sidecar marked the body dynamic.
    Vector3f   playerPos;
    Quaternion playerRot;
    const bool holdPlayer = m_playMode && m_playPlayer.valid() && world().get<TransformComponent>(m_playPlayer) != nullptr;
    if (holdPlayer)
    {
        const TransformComponent* xf = world().get<TransformComponent>(m_playPlayer);
        playerPos                    = xf->position;
        playerRot                    = xf->rotation;
    }

    // Leave the player body where it was last frame. moveKinematicTo sweeps it to the
    // motor pose during the step, which is what shoves dynamic cubes and spheres.
    m_physics.pushPoses(world(), false, holdPlayer ? m_playPlayer : Entity{});
    if (holdPlayer && m_physics.isBound(m_playPlayer))
    {
        const Physics::PhysicsBodyId id = m_physics.bodyOf(m_playPlayer);
        if (!m_physics.moveKinematicTo(id, playerPos, playerRot, dt))
        {
            m_physics.setBodyPose(id, playerPos, playerRot);
            m_physics.clearBodyVelocity(id);
        }
    }
    m_physics.step(dt, world());
    m_physics.writeDynamicPoses(world());
    if (!holdPlayer)
        return;
    if (TransformComponent* xf = world().get<TransformComponent>(m_playPlayer))
    {
        xf->position = playerPos;
        xf->rotation = playerRot;
    }
    if (m_physics.isBound(m_playPlayer))
    {
        const Physics::PhysicsBodyId id = m_physics.bodyOf(m_playPlayer);
        m_physics.setBodyPose(id, playerPos, playerRot);
        m_physics.clearBodyVelocity(id);
    }
}

void EditorApp::drawPhysicsInspector(Entity e)
{
    if (!e.valid() || !world().alive(e) || !world().get<TransformComponent>(e))
        return;
    const EditorObjectComponent* so       = findObject(e);
    const bool                   hasModel = world().has<ModelComponent>(e);
    const bool solid = so && (so->type == SceneObjectType::Cube || so->type == SceneObjectType::Sphere || so->type == SceneObjectType::Model
                               || isPawnType(so->type));
    if (!hasModel && !solid)
        return;

    ImGui::Separator();
    ImGui::TextUnformatted("Physics");

    AssetRef<Model> held;
    const Model*    model = modelForPhysics(e, held);
    const std::filesystem::path sidecar = (model && !model->sourcePath().empty()) ? Physics::physicsSidecarAuthoringPath(model->sourcePath())
                                                                                    : std::filesystem::path{};
    if (!sidecar.empty())
        ImGui::TextWrapped("%s", sidecar.string().c_str());
    else
        ImGui::TextDisabled("Cubes and spheres keep physics until the scene closes. A model can save a .physics.json.");

    const bool locked = netClientLocked();
    ImGui::BeginDisabled(locked);

    PhysicsComponent* phys = world().get<PhysicsComponent>(e);
    if (!phys)
    {
        if (ImGui::Button("Add Physics"))
        {
            world().emplace<PhysicsComponent>(e, defaultPhysicsFor(e));
            rebuildPhysicsBody(e);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Dynamic bodies fall during Play onto the Y = 0 ground.");
        return;
    }

    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &phys->enabled);

    int body = 0;
    if (phys->mode == PhysicsBodyMode::Dynamic)
        body = 2;
    else if (phys->mode == PhysicsBodyMode::Kinematic || phys->mode == PhysicsBodyMode::Mover)
        body = 1;
    if (ImGui::Combo("Body", &body, "Static\0Kinematic\0Dynamic\0"))
    {
        phys->mode = body == 2 ? PhysicsBodyMode::Dynamic : (body == 1 ? PhysicsBodyMode::Kinematic : PhysicsBodyMode::Static);
        changed    = true;
    }

    int shape = static_cast<int>(phys->shape);
    if (shape < 0 || shape > 3)
        shape = 0;
    if (ImGui::Combo("Shape", &shape, "Box\0Sphere\0Capsule\0Model\0"))
    {
        phys->shape = static_cast<PhysicsShapeKind>(shape);
        changed     = true;
    }
    if (phys->shape == PhysicsShapeKind::Model)
        ImGui::TextDisabled("Model uses _col parts or a .collision.json. Otherwise a box from the mesh bounds.");
    if (phys->shape == PhysicsShapeKind::Capsule)
        ImGui::TextDisabled("Capsule stands on the object origin and extends upward.");

    changed |= ImGui::DragFloat("Density", &phys->density, 0.01f, 0.0f, 10000.0f, "%.3f");
    if (const TransformComponent* xf = world().get<TransformComponent>(e))
    {
        const float mass = estimatedMass(*phys, xf->scale);
        if (mass > 0.0f)
            ImGui::Text("Mass  %.2f", static_cast<double>(mass));
    }
    changed |= ImGui::DragFloat("Friction", &phys->friction, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= ImGui::DragFloat("Restitution", &phys->restitution, 0.01f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("Linear damping", &phys->linearDamping, 0.01f, 0.0f, 10.0f, "%.3f");
    changed |= ImGui::DragFloat("Angular damping", &phys->angularDamping, 0.01f, 0.0f, 10.0f, "%.3f");
    changed |= ImGui::DragFloat("Gravity scale", &phys->gravityScale, 0.01f, -2.0f, 5.0f, "%.2f");
    changed |= ImGui::Checkbox("Sensor", &phys->sensor);
    changed |= ImGui::Checkbox("Fixed rotation", &phys->fixedRotation);

    if (!m_surfaces.empty())
    {
        std::vector<std::string> names;
        names.emplace_back("(none)");
        for (const Physics::PhysicsSurface& surface : m_surfaces.surfaces())
            names.push_back(surface.id);
        int surfaceIndex = 0;
        for (int i = 1; i < static_cast<int>(names.size()); ++i)
        {
            if (names[static_cast<size_t>(i)] == phys->surface)
                surfaceIndex = i;
        }
        std::vector<const char*> labels;
        labels.reserve(names.size());
        for (const std::string& name : names)
            labels.push_back(name.c_str());
        if (ImGui::Combo("Surface", &surfaceIndex, labels.data(), static_cast<int>(labels.size())))
        {
            if (surfaceIndex <= 0)
                phys->surface.clear();
            else
            {
                phys->surface = names[static_cast<size_t>(surfaceIndex)];
                if (const Physics::PhysicsSurface* surface = m_surfaces.find(phys->surface))
                {
                    phys->friction    = surface->friction;
                    phys->restitution = surface->restitution;
                    phys->density     = surface->density;
                    phys->sensor      = surface->sensor;
                }
            }
            changed = true;
        }
        ImGui::TextDisabled("Picking a surface copies its density, friction, and restitution.");
    }

    if (phys->mode == PhysicsBodyMode::Dynamic && (world().has<PlayerMotorComponent>(e) || (so && isPawnType(so->type))))
        ImGui::TextDisabled("This character is driven by its motor. Kinematic keeps that pose.");
    if (phys->sensor)
        ImGui::TextDisabled("Sensors overlap without blocking or falling.");

    if (m_physics.isBound(e))
        ImGui::TextDisabled(m_playMode ? "Simulating" : "Bound. Falls while Play is on.");
    else
        ImGui::TextDisabled("Not in the physics world.");

    if (ImGui::Button("Save..."))
        savePhysicsWithDialog(e);
    ImGui::SameLine();
    if (ImGui::Button("Load..."))
    {
        if (loadPhysicsWithDialog(e))
            changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove"))
    {
        releasePhysicsBody(e);
        world().remove<PhysicsBodyComponent>(e);
        world().remove<PhysicsComponent>(e);
        phys    = nullptr;
        changed = false;
    }

    ImGui::EndDisabled();

    if (changed && phys && !locked)
        rebuildPhysicsBody(e);
}
