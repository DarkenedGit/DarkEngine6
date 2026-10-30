#include "EditorApp.h"

#include "Play/AttachedLights.h"
#include "Play/Carry.h"
#include "Play/CharacterAttach.h"
#include "Play/ContactGrind.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/PowerSystem.h"
#include "Play/HunterCue.h"
#include "Play/ThrowableHost.h"
#include "Play/HunterTick.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayLook.h"
#include "Play/PlayMotor.h"
#include "Play/ShoulderCamera.h"
#include "Play/PlayerFlashlight.h"
#include "Play/WeaponFire.h"

#include "AI/AiComponents.h"
#include "AI/Brain.h"
#include "AI/HsmGraph.h"
#include "AI/HsmGraphComponent.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/Locomotion.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Audio/SoundComponents.h"
#include "Character/HealthComponent.h"
#include "Save/ProgressComponents.h"
#include "Character/HitReaction.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/ShieldView.h"
#include "Character/ThrowableView.h"
#include "Character/SkillComponent.h"
#include "Character/SkillSense.h"
#include "Character/SkillXp.h"
#include "Collision/Collision.h"
#include "Collision/HitResult.h"
#include "Collision/SweptCollision.h"
#include "Combat/DefenseComponent.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttack.h"
#include "Combat/Shield.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/WeaponHitAdapter.h"
#include "Particles/StatusFxDriver.h"
#include "Core/EntityPins.h"
#include "Scene/EntityMaster.h"
#include "Core/Log.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Weapons/ThrowableFlight.h"
#include "ECS/Components.h"
#include "Input/Input.h"
#include "Input/InputCodes.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Render/DecalBasis.h"
#include "Render/GpuUpload.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/WeaponLoadout.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <imgui.h>

#include <cmath>
#include <filesystem>
#include <cstring>
#include <memory>
#include <vector>

using namespace Dark;
using namespace Dark::Math;


bool EditorApp::attachEditorModel(Entity e, const char* gltfPath)
{
    if (!e.valid() || !world().alive(e) || !gltfPath || gltfPath[0] == '\0')
        return false;
    if (world().has<ModelComponent>(e) && world().get<ModelComponent>(e)->modelAssetID != NULL_ASSET)
        return true;

    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    const ContentModelResult result = attachContentModel(host, e, gltfPath, ModelAttach::AllowStatic);
    if (!result.attached)
        return false;
    if (result.skinned)
        tickAnimGraphs(world(), assets(), 1.0f / 60.0f);
    tryLoadModelPhysics(e);
    return true;
}

bool EditorApp::attachEditorPlayer(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return false;

    ensureHumanoidBody(world(), e, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    ensureLocalPlayerRig(host, e, &EditorApp::onPlayWeaponHitThunk, this);

    const EntityMaster playerMaster = loadEntityMasterType("player");
    const std::string  playerGltf = playerMaster.gltf.empty() ? std::string("models/human.gltf") : playerMaster.gltf;
    attachEditorModel(e, playerGltf.c_str());
    return true;
}

bool EditorApp::attachEditorNpc(Entity e, NpcKind kind, const char* proxyWarn)
{
    if (!m_ai.attachHunter(world(), e, pins(), assets()))
        return false;
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_ground };
    const bool haveModel = world().has<ModelComponent>(e) && world().get<ModelComponent>(e)->modelAssetID != NULL_ASSET;
    if (haveModel)
    {
        configureNpc(host, e, kind);
        return true;
    }
    const ContentModelResult vis = attachNpcVisual(host, e, kind, ModelAttach::AllowStatic);
    if (!vis.attached)
    {
        MeshComponent mc{};
        mc.matAssetID  = m_propMaterial ? m_propMaterial->id : NULL_ASSET;
        mc.meshAssetID = NULL_ASSET;
        setMeshComponent(world(), pins(), assets(), e, mc);
        DE_LOG_WARN("{}", proxyWarn ? proxyWarn : "Editor: npc using cube proxy");
    }
    else
    {
        if (vis.skinned)
            tickAnimGraphs(world(), assets(), 1.0f / 60.0f);
        tryLoadModelPhysics(e);
    }
    return true;
}

bool EditorApp::attachEditorHunter(Entity e)
{
    return attachEditorNpc(e, NpcKind::Hunter, "Editor: hunter using cube proxy (skeleton model missing)");
}

bool EditorApp::attachEditorWolf(Entity e)
{
    return attachEditorNpc(e, NpcKind::Wolf, "Editor: wolf using cube proxy (wolf model missing)");
}

Entity EditorApp::findPlayPlayer()
{
    if (const EditorObjectComponent* so = m_selected.valid() ? world().get<EditorObjectComponent>(m_selected) : nullptr)
    {
        if (so->type == SceneObjectType::Player)
            return m_selected;
    }
    Entity found{};
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type == SceneObjectType::Player && !found.valid())
            found = e;
    });
    return found;
}

void EditorApp::resetPlayCombat()
{
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isPawnType(so.type))
            return;
        if (HealthComponent* hp = world().get<HealthComponent>(e))
            hp->health.reset();
        if (HitReactionComponent* hr = world().get<HitReactionComponent>(e))
        {
            hr->hit.reset();
            if (Combat::HitVolumesComponent* volumes = world().get<Combat::HitVolumesComponent>(e))
                Combat::clearHitPartState(*volumes);
        }
        if (PlayerMotorComponent* pmc = world().get<PlayerMotorComponent>(e))
            pmc->motor.reset();
        if (JumpAttackComponent* jac = world().get<JumpAttackComponent>(e))
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
        if (Combat::StatusEffectComponent* st = world().get<Combat::StatusEffectComponent>(e))
            st->reset();
        if (Combat::PoiseComponent* po = world().get<Combat::PoiseComponent>(e))
            po->reset();
        if (BrainComponent* brain = world().get<BrainComponent>(e); brain && brain->brain)
            brain->brain->start();
        if (PathAgentComponent* path = world().get<PathAgentComponent>(e))
        {
            path->path.points.clear();
            path->waypoint = 0;
        }
        if (AiAgentComponent* ai = world().get<AiAgentComponent>(e))
        {
            ai->givenUp     = false;
            ai->hasLastSeen = false;
            ai->assistLeft  = 0.0f;
            ai->fleeLeft    = 0.0f;
            ai->deadFor     = 0.0f;
            ai->lureSearch  = false;
            ai->lureReached = false;
            ai->lureLeft    = 0.0f;
            ai->lurePoint   = {};
            ai->lureDest    = {};
        }
        const char* profileId = "hunter";
        if (so.type == SceneObjectType::Player)
            profileId = "player";
        else if (so.type == SceneObjectType::Wolf)
            profileId = "wolf";
        if (SkillComponent* sk = world().get<SkillComponent>(e))
        {
            if (const SkillProfile* profile = skillCatalog().profile(profileId))
                sk->resetToProfile(*profile);
        }
    });
}

void EditorApp::captureAuthoredPoses()
{
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isPawnType(so.type))
            return;
        TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        EditorAuthoredPoseComponent pose{};
        pose.position = xf->position;
        pose.rotation = xf->rotation;
        pose.scale    = xf->scale;
        world().emplace<EditorAuthoredPoseComponent>(e, pose);
    });
}

void EditorApp::restoreAuthoredPoses()
{
    world().each<EditorAuthoredPoseComponent>([&](Entity e, EditorAuthoredPoseComponent& pose) {
        if (TransformComponent* xf = world().get<TransformComponent>(e))
        {
            xf->position = pose.position;
            xf->rotation = pose.rotation;
            xf->scale    = pose.scale;
        }
    });
}

void EditorApp::bakePlayWalkability()
{
    m_playCubes.clear();
    m_playSpheres.clear();
    m_playCubeEntities.clear();
    m_playSphereEntities.clear();
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        if (so.type == SceneObjectType::Cube)
        {
            const Vector3f half{ 0.5f * std::fabs(xf->scale.x), 0.5f * std::fabs(xf->scale.y), 0.5f * std::fabs(xf->scale.z) };
            m_playCubes.push_back(AABox3f::FromCenterExtents(xf->position, half));
            m_playCubeEntities.push_back(e);
        }
        else if (so.type == SceneObjectType::Sphere)
        {
            // Unit sphere mesh is radius 0.5, then scaled. Use the largest axis so a stretched sphere still blocks.
            const float radius = 0.5f * Math::Max(std::fabs(xf->scale.x), Math::Max(std::fabs(xf->scale.y), std::fabs(xf->scale.z)));
            if (radius > 1.0e-4f)
            {
                m_playSpheres.push_back(Sphere3f(xf->position, radius));
                m_playSphereEntities.push_back(e);
            }
        }
    });
    const Terrain::HeightMap* height = nullptr;
    if (m_haveTerrain && m_terrain.valid() && m_terrain.coarse().valid())
        height = &m_terrain.coarse();
    else
    {
        // The default editor ground is the Y=0 grid from -20 to 20. Bake that as a flat walk map.
        if (!m_playGrid.valid())
        {
            if (!m_playGrid.create(41, 41, 1.0f, 1.0f))
            {
                DE_LOG_WARN(LogCategory::AI, "Editor: play without terrain — hunters will not path");
                return;
            }
            m_playGrid.setOrigin(Vector3f{ -20.0f, 0.0f, -20.0f });
        }
        height = &m_playGrid;
        DE_LOG_INFO(LogCategory::AI, "Editor: no terrain — hunters path the ground grid");
    }
    AI::WalkabilityDesc d;
    d.heightMap   = height;
    d.waterLevel  = -1.0e9f;
    d.agentRadius = 0.8f;
    setHunterBakeObstacles(d, m_playCubes.data(), static_cast<int>(m_playCubes.size()), m_playSpheres.data(), static_cast<int>(m_playSpheres.size()));
    if (!m_ai.bake(d))
        DE_LOG_WARN(LogCategory::AI, "Editor: walkability bake failed");
}



void EditorApp::fillPlayHost(void* user, PlayHost& host)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    host.world    = &app->world();
    host.renderer = &app->renderer();
    host.scene    = &app->m_scene;
    host.assets   = &app->assets();
    host.pins     = &app->pins();
    host.audio    = &app->audio();
    host.physics  = app->m_physics.valid() ? &app->m_physics : nullptr;
    host.ai       = &app->m_ai;
    host.camera   = &app->m_camera;
    host.input    = &app->input();
    host.terrain  = &app->m_terrain;
    host.ground   = &app->m_ground;
    host.groundBody = app->m_physicsGround;
    host.guardVoice = &app->m_guardVoice;
    host.crows    = &app->m_crowModels;
    host.foliageCollision = &app->m_foliageCollision;
    host.foliage  = app->m_foliage.empty() ? nullptr : &app->m_foliage;

    host.player  = app->m_playPlayer;
    host.shield  = app->m_playShield;
    host.session = app->m_session;

    host.waterLevel   = app->m_water.chunksX() > 0 ? app->m_water.params().waterLevel : -1.0e9f;
    host.sunElevation = app->m_env.sunElevation();
    host.sunDir       = app->m_env.sunDir();
    host.cloudTime    = app->m_cloudTime;

    host.groundProbeY = &app->m_playGroundProbeY;
    host.groundIgnore = &app->m_playGroundIgnore;
    host.lightLevel   = &app->m_lightLevel;
    host.exposure     = &app->m_exposure;
    host.strideLeft   = &app->m_strideLeft;
    host.stealth      = &app->m_stealth;
    host.camouflage   = false;

    const bool pointerFree = app->window().cursorCaptured() || !(app->m_imgui.isReady() && app->m_imgui.wantCaptureMouse());
    host.applyMouseLook    = pointerFree;
    host.allowMouseButtons = pointerFree;
    host.uiMouse           = !pointerFree;
    host.uiKeys            = app->m_imgui.isReady() && app->m_imgui.wantCaptureKeyboard();
    host.chaseReady        = app->m_playMode;

    host.cubes            = app->m_playCubes.empty() ? nullptr : app->m_playCubes.data();
    host.cubeCount        = static_cast<int>(app->m_playCubes.size());
    host.spheres          = app->m_playSpheres.empty() ? nullptr : app->m_playSpheres.data();
    host.sphereCount      = static_cast<int>(app->m_playSpheres.size());
    host.cubeEntities     = app->m_playCubeEntities.empty() ? nullptr : app->m_playCubeEntities.data();
    host.cubeEntityCount  = static_cast<int>(app->m_playCubeEntities.size());
    host.sphereEntities   = app->m_playSphereEntities.empty() ? nullptr : app->m_playSphereEntities.data();
    host.sphereEntityCount = static_cast<int>(app->m_playSphereEntities.size());

    host.preparePlayer = &EditorApp::onPlayPrepare;
    host.stepPhysics   = &EditorApp::onPlayStepPhysics;
    host.spawnUnit     = &EditorApp::onPlaySpawnUnit;
    host.heightAt      = &EditorApp::onPlayHeight;
    host.groundContact = &EditorApp::onPlayGroundContact;
    host.user          = app;
}

void EditorApp::onPlayPrepare(void* user)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->ensurePlayPhysicsVolumes();
}

void EditorApp::onPlayStepPhysics(void* user, float dt)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->simulatePhysics(dt);
}

Entity EditorApp::onPlaySpawnUnit(void* user, Entity spawner, const SpawnerRequest& req)
{
    auto* app = static_cast<EditorApp*>(user);
    return app ? app->spawnSpawnerUnit(spawner, req) : Entity{};
}

float EditorApp::onPlayHeight(void* user, float x, float z)
{
    auto* app = static_cast<EditorApp*>(user);
    return app ? app->playGroundHeight(x, z) : 0.0f;
}

Terrain::GroundContact EditorApp::onPlayGroundContact(void* user, float x, float z)
{
    auto* app = static_cast<EditorApp*>(user);
    return app ? app->groundContactAt(x, z) : Terrain::GroundContact{};
}

void EditorApp::onPlayWeaponHit(const WeaponHit& hit)
{
    m_play.onWeaponHit(hit);
}

void EditorApp::setPlayMode(bool play)
{
    if (m_sceneMode != SceneMode::Scene3D)
        play = false;
    if (play == m_playMode)
        return;
    if (play)
    {
        m_playPlayer = findPlayPlayer();
        if (!m_playPlayer.valid())
        {
            DE_LOG_WARN("Editor: place a Player before Play (Create → Player, then F12)");
            return;
        }
        captureAuthoredPoses();
        bakePlayWalkability();
        {
            std::vector<AABox3f> conceal;
            gatherConcealVolumes(world(), conceal);
            m_ai.setConcealVolumes(std::move(conceal));
        }
        m_play.bind(&EditorApp::fillPlayHost, this);
        m_play.resetPawn();
        if (const TransformComponent* xf = world().get<TransformComponent>(m_playPlayer))
        {
            Vector3f toCam = m_camera.GetPosition() - xf->position;
            toCam.y        = 0.0f;
            if (toCam.MagnitudeSqrd() > 1.0e-6f)
                toCam.Normalize();
            else
                toCam = Vector3f{ 0.0f, 0.0f, -1.0f };
            m_play.runtime().lookYaw   = std::atan2f(-toCam.x, -toCam.z);
            m_play.runtime().lookPitch = -0.25f;
        }
        clearAllStatusFx(world(), &audio());
        resetPlayCombat();
        m_play.quest().clear();
        if (!m_sceneQuest.empty())
            m_play.startQuest(m_sceneQuest);
        {
            GuardBarks  barks;
            std::string barkError;
            if (loadGuardBarks(resolveContentFile("ai/barks.json"), barks, &barkError))
                m_guardVoice.start(std::move(barks));
            else
                DE_LOG_WARN(LogCategory::AI, "Guard barks: {}", barkError);
        }
        ensurePlayerFlashlight(world(), m_playPlayer);
        // clearPlayArmor inside ensurePlayGear deletes every pickup. Throwables are copied so they can come back.
        const std::vector<ThrowablePickupSpot> authoredThrowables = captureThrowablePickups(world());
        ensurePlayGear();
        m_gizmoDragAxis    = EditorDetail::TranslateGizmoAxis::None;
        m_dragging         = false;
        m_playMode = true;
        ensurePhysicsWorld();
        rebuildPhysicsGround(); // pick up a terrain loaded since the last Play
        if (m_physics.valid())
            m_physics.pushPoses(world(), true);
        respawnThrowablePickups(world(), pins(), assets(), renderer(), m_physics, authoredThrowables);
        if (const TransformComponent* start = world().get<TransformComponent>(m_playPlayer))
        {
            bool haveRock   = false;
            bool haveBottle = false;
            for (const ThrowablePickupSpot& spot : authoredThrowables)
            {
                if (spot.id == "rock")
                    haveRock = true;
                else if (spot.id == "bottle")
                    haveBottle = true;
            }
            const ThrowableBeltComponent* belt         = world().get<ThrowableBeltComponent>(m_playPlayer);
            const int                     rockOnBelt   = belt ? belt->belt.count("rock") : 0;
            const int                     bottleOnBelt = belt ? belt->belt.count("bottle") : 0;
            haveRock   = throwableAlreadyAccounted(haveRock, rockOnBelt, throwableIdInFlightOrLanding(m_play.throws(), "rock"));
            haveBottle = throwableAlreadyAccounted(haveBottle, bottleOnBelt, throwableIdInFlightOrLanding(m_play.throws(), "bottle"));
            stockMissingThrowablePickups(world(), pins(), assets(), renderer(), m_physics, start->position, haveRock, haveBottle);
        }
        window().setCursorCaptured(window().isFocused());
        ensurePlaySession();
        installSaveHost();
        if (!world().has<LookComponent>(m_playPlayer))
            world().emplace<LookComponent>(m_playPlayer);
        if (!world().has<RunProgressComponent>(m_playPlayer))
            world().emplace<RunProgressComponent>(m_playPlayer);
        m_save.captureBaseline(world());
        DE_LOG_INFO("Editor: PLAY — WASD move, mouse look, Space jump, hold LMB/F to charge an attack and release to swing, hold RMB/V to block, Ctrl/C crouch, E pickup, L flashlight, Escape or F12 stop");
    }
    else
    {
        Physics::destroyFoliageCollision(m_physics, m_foliageCollision);
        if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
        destroySpawnedUnits({});
        restoreAuthoredPoses();
        resetPlayCombat();
        clearAllStatusFx(world(), &audio());
        if (AnimGraphComponent* ag = m_playPlayer.valid() ? world().get<AnimGraphComponent>(m_playPlayer) : nullptr)
            ag->graph.player().setLowerBodyYaw(0.0f);
        // clearPlayArmor deletes every pickup. Copy throwables, including throws still in the air, and put them back.
        std::vector<ThrowablePickupSpot> keptThrowables = captureThrowablePickups(world());
        appendUnresolvedThrowableSpots(keptThrowables, m_play.throws());
        m_play.throws().clear();
        m_play.quest().clear();
        m_guardVoice.stop();
        m_play.resetPawn();
        destroyPlayGear();
        respawnThrowablePickups(world(), pins(), assets(), renderer(), m_physics, keptThrowables);
        clearThrowablePlayVisuals(world(), pins());
        m_playMode   = false;
        m_playPlayer = {};
        if (m_physics.valid())
            m_physics.pushPoses(world(), true);
        window().setCursorCaptured(false);
        DE_LOG_INFO("Editor: play stopped — restored spawn poses");
    }
}

void EditorApp::togglePlayMode()
{
    setPlayMode(!m_playMode);
}

void EditorApp::ensurePlayGear()
{
    if (!m_playShield.valid() || !world().alive(m_playShield))
        m_playShield = spawnPlayerShield(world(), pins(), assets(), renderer());
    equipPlayArmor();
}

void EditorApp::destroyPlayGear()
{
    destroyPlayerShield(world(), pins(), m_playShield);
    clearPlayerFlashlight(world(), m_playPlayer);
    clearPlayArmor();
}

void EditorApp::equipPlayArmor()
{
    clearPlayArmor();
    std::vector<Entity> hunters;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type == SceneObjectType::Hunter)
            hunters.push_back(e);
    });
    for (Entity hunter : hunters)
        m_play.stockHunter(hunter);
}

void EditorApp::clearPlayArmor()
{
    std::vector<Entity> owners;
    world().each<Combat::ArmorPiecesComponent>([&](Entity e, Combat::ArmorPiecesComponent&) { owners.push_back(e); });
    for (Entity owner : owners)
        removeArmor(world(), pins(), m_physics, owner);

    std::vector<Entity> pickups;
    world().each<ItemPickupComponent>([&](Entity e, ItemPickupComponent&) { pickups.push_back(e); });
    for (Entity pickup : pickups)
        destroyPickup(world(), pins(), m_physics, pickup);
}



void EditorApp::onPlayWeaponHitThunk(void* user, const WeaponHit& hit)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->onPlayWeaponHit(hit);
}





ImpactFxHost EditorApp::impactHost()
{
    ImpactFxHost host;
    host.world       = &world();
    host.scene       = &m_scene;
    host.renderer    = &renderer();
    host.terrain     = &m_terrain;
    host.ground      = &m_ground;
    host.contactGrid = (m_haveTerrain && m_terrain.valid()) ? &m_terrain : nullptr;
    host.targets     = &m_play.targets();
    return host;
}

bool EditorApp::deferredDecals()
{
    return impactDecalsReady(impactHost());
}

bool EditorApp::isStepCue(Entity e, const char* cue)
{
    return impactIsStepCue(impactHost(), e, cue);
}

void EditorApp::spawnFootmark(const Vector3f& bodyPos, const Vector3f& facing)
{
    Dark::spawnFootmark(impactHost(), bodyPos, facing);
}

void EditorApp::spawnHunterFootmark(Entity hunter)
{
    Dark::spawnHunterFootmark(impactHost(), hunter);
}

void EditorApp::spawnWeaponImpact(const WeaponHit& hit)
{
    Dark::spawnWeaponImpact(impactHost(), hit);
}

void EditorApp::spawnLivingBloodDecal(Entity victim, const Vector3f& point, const Vector3f& hitNormal)
{
    Dark::spawnLivingBloodDecal(impactHost(), victim, point, hitNormal);
}

void EditorApp::spawnDeathBloodDecal(const Vector3f& point, const Vector3f& hitNormal)
{
    Dark::spawnDeathBloodDecal(impactHost(), point, hitNormal);
}





Terrain::GroundContact EditorApp::groundContactAt(float x, float z) const
{
    const Terrain::TerrainGrid* grid = (m_haveTerrain && m_terrain.valid()) ? &m_terrain : nullptr;
    return Terrain::groundContactAt(m_ground, grid, x, z);
}

void EditorApp::destroySpawnedUnits(Entity onlySpawner)
{
    std::vector<Entity> doomed;
    world().each<SpawnedUnitComponent>([&](Entity e, SpawnedUnitComponent& mark) {
        if (onlySpawner.valid() && mark.spawner.id() != onlySpawner.id())
            return;
        doomed.push_back(e);
    });
    for (Entity e : doomed)
    {
        if (!world().alive(e))
            continue;
        releasePhysicsBody(e);
        onEntityRemoved(world(), e, &pins());
        world().destroyEntity(e);
    }
    if (onlySpawner.valid())
    {
        if (SpawnerComponent* spawner = world().get<SpawnerComponent>(onlySpawner))
            resetSpawnerRuntime(spawner->runtime);
        return;
    }
    world().each<SpawnerComponent>([&](Entity, SpawnerComponent& spawner) {
        resetSpawnerRuntime(spawner.runtime);
    });
}

void EditorApp::tickCrows(float dt)
{
    if (!m_playMode || m_sceneMode != SceneMode::Scene3D || dt < 0.0f)
        return;
    const Terrain::TerrainGrid* grid = (m_haveTerrain && m_terrain.valid()) ? &m_terrain : nullptr;
    simulateCrows(world(), grid, &m_crowModels, &assets(), &pins(), &audio(), dt);
}

Entity EditorApp::spawnSpawnerUnit(Entity spawner, const SpawnerRequest& req)
{
    // No EditorObjectComponent and no persistent id. A play-mode scene save must not keep the army.
    if (req.unit == SpawnerUnit::Crow)
    {
        const TransformComponent* home = world().get<TransformComponent>(spawner);
        const float homeX = home ? home->position.x : req.x;
        const float homeZ = home ? home->position.z : req.z;
        const bool haveGround = m_haveTerrain && m_terrain.valid();
        const float groundY = haveGround ? m_terrain.heightAtWorld(req.x, req.z) : 0.0f;
        return spawnCrow(world(), renderer(), assets(), pins(), m_crowModels, haveGround ? &m_terrain : nullptr, spawner, req, groundY, homeX, homeZ);
    }
    const bool wolf = req.unit == SpawnerUnit::Wolf;
    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, wolf ? "Wolf" : "Hunter");
    TransformComponent xf{};
    xf.position = Vector3f{ req.x, m_terrain.heightAtWorld(req.x, req.z) + 0.5f, req.z };
    xf.scale    = Vector3f{ 1.0f, 1.0f, 1.0f };
    world().emplace<TransformComponent>(e, xf);
    const bool ok = wolf ? attachEditorWolf(e) : attachEditorHunter(e);
    if (!ok || !world().alive(e))
    {
        if (world().alive(e))
        {
            releasePhysicsBody(e);
            onEntityRemoved(world(), e, &pins());
            world().destroyEntity(e);
        }
        DE_LOG_WARN(LogCategory::AI, "Editor: spawner failed to create a {}", wolf ? "wolf" : "hunter");
        return {};
    }
    SpawnedUnitComponent mark{};
    mark.spawner = spawner;
    mark.slot    = req.slot;
    world().emplace<SpawnedUnitComponent>(e, mark);
    if (!wolf)
        m_play.stockHunter(e);
    return e;
}

void EditorApp::tickSpawners(float dt)
{
    if (!m_playMode || m_sceneMode != SceneMode::Scene3D || dt < 0.0f)
        return;
    tickSpawnerJobs(
        world(), dt,
        [](void* user, Entity spawner, const SpawnerRequest& req) { return static_cast<EditorApp*>(user)->spawnSpawnerUnit(spawner, req); },
        this);
}

void EditorApp::applyAuthoredStealth()
{
    applySceneStealth(m_stealth, m_ai, m_authoredStealth);
}



void EditorApp::drawPlayHud()
{
    if (!m_playMode || !m_imgui.isReady())
        return;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 48.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("##playhud", nullptr, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("PLAY");
    if (HealthComponent* hp = m_playPlayer.valid() ? world().get<HealthComponent>(m_playPlayer) : nullptr)
        ImGui::Text("Player HP  %.0f / %.0f", static_cast<double>(hp->health.hp()), static_cast<double>(hp->health.maxHp()));
    if (const SkillComponent* sk = m_playPlayer.valid() ? world().get<SkillComponent>(m_playPlayer) : nullptr)
        ImGui::Text("Run %d  Swim %d  Jump %d  Shoot %d  Hear %d  See %d", sk->level(SkillId::Run), sk->level(SkillId::Swim), sk->level(SkillId::Jump),
                    sk->level(SkillId::Shoot), sk->level(SkillId::Hear), sk->level(SkillId::See));
    if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
        ImGui::Text("Jump CD  %.2fs", static_cast<double>(jac->jump.cooldownLeft()));
    if (const Combat::StatusEffectComponent* st = m_playPlayer.valid() ? world().get<Combat::StatusEffectComponent>(m_playPlayer) : nullptr)
    {
        for (int i = 1; i < Combat::kStatusIdCount; ++i)
        {
            const auto id = static_cast<Combat::StatusId>(i);
            if (!st->has(id))
                continue;
            const Combat::StatusDef* def = Combat::statusDef(id);
            if (!def || !def->name || !def->name[0])
                continue;
            ImGui::Text("%s  %.2fs", def->name, static_cast<double>(st->remaining(id)));
        }
    }
    int hunters = 0;
    int huntersAlive = 0;
    int wolves = 0;
    int wolvesAlive = 0;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const HealthComponent* h = world().get<HealthComponent>(e);
        const bool living = h && h->health.alive();
        if (so.type == SceneObjectType::Hunter)
        {
            ++hunters;
            if (living)
                ++huntersAlive;
        }
        else if (so.type == SceneObjectType::Wolf)
        {
            ++wolves;
            if (living)
                ++wolvesAlive;
        }
    });
    const PlayRuntime& rt = m_play.runtime();
    const char* offhand = rt.offhand.lightOn ? "Flashlight" : "Off hand stowed";
    if (rt.blockCharge.isCharged())
        offhand = "Shield charged";
    else if (rt.offhand.shield.blocking())
        offhand = "Shield up";
    ImGui::TextUnformatted(offhand);
    if (rt.attackCharge.isCharged())
        ImGui::TextUnformatted("Attack charged");
    if (const PlayerMotorComponent* pmc = m_playPlayer.valid() ? world().get<PlayerMotorComponent>(m_playPlayer) : nullptr)
    {
        if (pmc->motor.state() == PlayerMoveState::Crouch)
            ImGui::TextUnformatted("Crouching");
    }
    ImGui::Text("Hunters  %d / %d", huntersAlive, hunters);
    if (wolves > 0)
        ImGui::Text("Wolves  %d / %d", wolvesAlive, wolves);
    ImGui::TextDisabled("RMB / V / LT shield    L light    F12 / Esc stop");
    ImGui::End();
}
