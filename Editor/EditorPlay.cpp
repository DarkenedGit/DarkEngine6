#include "EditorApp.h"

#include "Play/CharacterAttach.h"
#include "Play/ContactGrind.h"
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
#include "Character/SkillComponent.h"
#include "Character/SkillSense.h"
#include "Character/SkillXp.h"
#include "Collision/Collision.h"
#include "Collision/HitResult.h"
#include "Collision/SweptCollision.h"
#include "Combat/DefenseComponent.h"
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

namespace
{
    constexpr float kJumpBuf = 0.12f;

} // namespace

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
            hr->hit.reset();
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
        m_ai.setJumpAttackHits(&EditorApp::onPlayJumpHitsThunk, this);
        m_ai.setHunterCue(&EditorApp::onPlayHunterCueThunk, this);
        if (const TransformComponent* xf = world().get<TransformComponent>(m_playPlayer))
        {
            Vector3f toCam = m_camera.GetPosition() - xf->position;
            toCam.y        = 0.0f;
            if (toCam.MagnitudeSqrd() > 1.0e-6f)
                toCam.Normalize();
            else
                toCam = Vector3f{ 0.0f, 0.0f, -1.0f };
            m_playLookYaw   = std::atan2f(-toCam.x, -toCam.z);
            m_playLookPitch = -0.25f;
        }
        clearAllStatusFx(world(), &audio());
        resetPlayCombat();
        m_offhand.reset();
        m_attackCharge.reset();
        m_blockCharge.reset();
        m_chargeWindup = {};
        m_fireQuick   = false;
        m_fireCharged = false;
        ensurePlayerFlashlight(world(), m_playPlayer);
        // clearPlayArmor inside ensurePlayGear deletes every pickup. Throwables are copied so they can come back.
        const std::vector<ThrowablePickupSpot> authoredThrowables = captureThrowablePickups(world());
        ensurePlayGear();
        m_jumpAttackBuffer = 0.0f;
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
        m_playLowerBodyYaw = 0.0f;
        destroyPlayGear();
        m_offhand.reset();
        m_attackCharge.reset();
        m_blockCharge.reset();
        m_chargeWindup = {};
        m_fireQuick   = false;
        m_fireCharged = false;
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
        equipHunterArmor(world(), pins(), assets(), renderer(), hunter);
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

void EditorApp::updatePlayCamera()
{
    const TransformComponent* xf = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
    if (!xf)
        return;
    const PlayerMotor* motor = nullptr;
    if (const PlayerMotorComponent* pmc = world().get<PlayerMotorComponent>(m_playPlayer))
        motor = &pmc->motor;
    const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
    const bool rayOk = m_haveTerrain && m_terrain.valid();
    const ShoulderView view = solveShoulderCamera(xf->position, m_playLookYaw, m_playLookPitch, crouched, 0.0f, 0.0f, false, 0.0f,
                                                  rayOk ? &shoulderTerrainRay : nullptr, rayOk ? &m_terrain : nullptr);
    m_camera.LookAt(view.eye, view.aim, Vector3f::Y_AXIS);
}

WeaponWorldQuery EditorApp::makePlayWeaponQuery()
{
    m_playWeaponTargets.gather(world(), m_playPlayer);

    WeaponWorldQuery q{};
    q.terrainUser = this;
    q.raycastTerrain = [](void* user, const Ray3f& ray, float) -> Collision::RayHit3D {
        return static_cast<EditorApp*>(user)->m_terrain.raycast(ray);
    };
    q.heightAt = [](void* user, float x, float z) {
        return static_cast<EditorApp*>(user)->m_terrain.heightAtWorld(x, z);
    };
    m_playWeaponTargets.bind(q);
    q.maxRange = 90.0f;
    return q;
}

void EditorApp::onPlayWeaponHitThunk(void* user, const WeaponHit& hit)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->onPlayWeaponHit(hit);
}

void EditorApp::onPlayWeaponHit(const WeaponHit& hit)
{
    // Terrain hits are !hitTarget. The mark still lands; damage stays behind that gate.
    spawnWeaponImpact(hit);
    if (!hit.hitTarget || !hit.targetEntity.valid())
        return;
    Combat::DamageEvent ev = Combat::damageEventFromWeaponHit(hit, m_playPlayer);
    Combat::ArmorBreak  broke;
    if (Combat::ArmorPiecesComponent* armor = world().get<Combat::ArmorPiecesComponent>(hit.targetEntity))
    {
        if (const TransformComponent* txf = world().get<TransformComponent>(hit.targetEntity))
            ev.amount = Combat::absorbArmorHit(*armor, txf->position, txf->rotation, ev.hitPoint, ev.hitDir, ev.amount, broke);
    }
    if (broke.index >= 0)
        knockOffArmor(world(), m_physics, hit.targetEntity, broke);
    Combat::ResolveResult r = m_combat.resolve(world(), ev);
    if (!r.applied)
        return;
    playSoundCueAt(world(), audio(), assets(), hit.targetEntity, "pain", hit.point);
    if (world().has<AiAgentComponent>(hit.targetEntity))
    {
        const TransformComponent* px = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
        m_ai.onHunterAttacked(world(), hit.targetEntity, px ? px->position : Vector3f{});
        spawnLivingBloodDecal(hit.targetEntity, hit.point, hit.normal);
        if (r.killed)
        {
            spawnDeathBloodDecal(hit.point, hit.normal);
            m_ai.onHunterKilled(world(), hit.targetEntity);
        }
    }
}

void EditorApp::onPlayJumpHitsThunk(void* user, const Combat::DamageEvent* events, int count)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->resolvePlayHits(events, count);
}

void EditorApp::onPlayHunterCueThunk(void* user, Entity hunter, const char* cue)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app || !cue)
        return;
    // playSoundCue returns when the bank has the cue, so the mark has to be first.
    if (app->isStepCue(hunter, cue))
        app->spawnHunterFootmark(hunter);
    playSoundCue(app->world(), app->audio(), app->assets(), hunter, cue);
}

void EditorApp::resolvePlayHits(const Combat::DamageEvent* events, int count)
{
    if (count <= 0 || !events)
        return;
    Combat::resolveJumpAttackEvents(world(), m_combat, events, count);
    for (int i = 0; i < count; ++i)
    {
        const Entity t = events[i].target;
        if (!t.valid() || !world().alive(t))
            continue;
        playSoundCueAt(world(), audio(), assets(), t, "pain", events[i].hitPoint);
        if (world().has<AiAgentComponent>(t))
        {
            const TransformComponent* px = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
            m_ai.onHunterAttacked(world(), t, px ? px->position : Vector3f{});
            spawnLivingBloodDecal(t, events[i].hitPoint, -events[i].hitDir);
            if (HealthComponent* hp = world().get<HealthComponent>(t); hp && !hp->health.alive())
            {
                // Jump events have no surface normal. A flat hint keeps the stain on the height field.
                spawnDeathBloodDecal(events[i].hitPoint, Vector3f(0.0f, 0.0f, 1.0f));
                m_ai.onHunterKilled(world(), t);
            }
        }
    }
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
    host.targets     = &m_playWeaponTargets;
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

bool EditorApp::firePlayLoadout(bool charged)
{
    const Entity body = m_playPlayer;
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    WeaponLoadoutComponent* wlc = body.valid() ? world().get<WeaponLoadoutComponent>(body) : nullptr;
    if (!wlc || !wlc->loadout)
        return false;
    WeaponFireRequest req = makeCameraFireRequest(m_camera.GetPosition(), m_camera.GetLook(), xf ? &xf->position : nullptr,
                                                  charged ? m_chargeSettings.attackDamageScale : 1.0f);
    SkillComponent* skill = nullptr;
    if (wlc->loadout->activeKind() == WeaponKind::Projectile)
        skill = world().get<SkillComponent>(body);
    applyShootSkill(req, skill);
    if (!wlc->loadout->fire(req, makePlayWeaponQuery()))
        return false;
    if (skill)
        noteShotXp(*skill, true);
    if (AnimGraphComponent* ag = body.valid() ? world().get<AnimGraphComponent>(body) : nullptr)
        ag->graph.setTrigger(wlc->loadout->activeKind() == WeaponKind::Melee ? "swing" : "shoot");
    return true;
}

void EditorApp::updatePlay(float dt)
{
    if (!m_playMode || !m_playPlayer.valid() || !world().alive(m_playPlayer))
    {
        if (m_playMode)
            setPlayMode(false);
        return;
    }

    Entity body = m_playPlayer;
    if (WorldClockComponent* clock = m_session.valid() ? world().get<WorldClockComponent>(m_session) : nullptr)
        clock->playTimeSec += static_cast<double>(dt);
    TransformComponent* xf = world().get<TransformComponent>(body);
    if (!xf)
        return;

    const bool uiKeys  = m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    const bool uiMouse = m_imgui.isReady() && m_imgui.wantCaptureMouse();
    // Captured play matches the game: the cursor is hidden and clipped, so it is not an ImGui pointer.
    // Pad pitch is negated here. The shared look step adds the value it is given.
    accumulatePlayLook(m_playLookYaw, m_playLookPitch, static_cast<float>(input().mouseDeltaX()), static_cast<float>(input().mouseDeltaY()),
                       window().cursorCaptured() || !uiMouse, input().actionAxis("look_x"), -input().actionAxis("look_y"), dt);

    const float mx = uiKeys ? 0.0f : input().actionAxis("move_x");
    const float mz = uiKeys ? 0.0f : input().actionAxis("move_z");
    const PlayBasis basis = playBasis(m_playLookYaw, m_playLookPitch, mx, mz);
    const Vector3f& flat  = basis.flat;
    const Vector3f& right = basis.right;
    const Vector3f& wish  = basis.wish;

    HealthComponent* hpComp = world().get<HealthComponent>(body);
    Health*          hp     = hpComp ? &hpComp->health : nullptr;
    HitReactionComponent* hrComp = world().get<HitReactionComponent>(body);
    HitReaction*          hitRx  = hrComp ? &hrComp->hit : nullptr;
    PlayerMotorComponent* pmc    = world().get<PlayerMotorComponent>(body);
    PlayerMotor*          motor  = pmc ? &pmc->motor : nullptr;
    JumpAttackComponent*  jac    = world().get<JumpAttackComponent>(body);
    Combat::JumpAttack*   jump   = jac ? &jac->jump : nullptr;
    Combat::StatusEffectComponent* status = world().get<Combat::StatusEffectComponent>(body);
    Combat::PoiseComponent*        poise  = world().get<Combat::PoiseComponent>(body);

    if (status)
        status->tick(dt);
    SkillComponent* skills = world().get<SkillComponent>(body);
    if (skills)
        tickSkill(*skills, dt);
    if (poise)
        poise->tick(dt);
    if (hp)
        hp->tick(dt);
    // Alive at entry. A contact kill later in this function still notes senses once.
    const bool senseAlive = hp && hp->alive();

    if (jump && (!hp || !hp->alive()))
        jump->cancel(Combat::JumpAttackCancel::NoPound);

    const bool ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
    const bool jumpBusy    = jump && jump->busy();
    const bool inAirCommit = jump && jump->inAirCommit();
    const bool canSteer    = hp && hp->alive() && !ccLocked && !(jump && jump->phase() == Combat::JumpAttackPhase::Pound);
    const bool pointerFree = window().cursorCaptured() || !uiMouse;
    const bool holdShield = canSteer
        && ((input().actionMouseDown("shield") && pointerFree) || (!uiKeys && input().actionDown("shield"))
            || input().actionAxisHeld("shield"));
    const bool carriesLight = flashlightEquipped(world().get<InventoryComponent>(body));
    const bool toggleLight = carriesLight && canSteer && !uiKeys && input().actionPressed("flashlight");
    if (canSteer && !uiKeys && input().actionPressed("pickup"))
    {
        const Entity closest = findThrowablePickupNear(world(), xf->position, 2.2f);
        if (closest.valid())
        {
            const ItemPickupComponent* picked = world().get<ItemPickupComponent>(closest);
            const std::string          id = (picked && picked->item.def) ? picked->item.def->id : std::string{};
            const CarryPickupResult    took = takeCarryPickup(world(), pins(), m_physics, closest, throwableBelt(world(), body), nullptr);
            if (took.throwable)
            {
                DE_LOG_INFO("Throwable: picked up {}", id);
                playSoundCue(world(), audio(), assets(), body, "click");
            }
        }
    }
    if (toggleLight)
    {
        if (SkillComponent* sk = world().get<SkillComponent>(body))
            sk->seeArmed = true;
    }
    const bool attackDown = canSteer && !uiKeys && (input().actionDown("attack") || (pointerFree && input().actionMouseDown("attack")));
    const bool airborneNow = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
    Combat::DefenseComponent* defense = world().get<Combat::DefenseComponent>(body);
    if (defense)
        defense->tick(dt);
    Combat::PlayerChargeInput chargeIn{};
    chargeIn.dt              = dt;
    chargeIn.attackDown      = attackDown;
    chargeIn.canChargeAttack = canSteer && !airborneNow && !jumpBusy;
    chargeIn.holdShield      = holdShield;
    chargeIn.canHoldShield   = canSteer;
    chargeIn.chargedParryUp  = defense && defense->chargedParry && defense->inParryWindow();
    Combat::PlayerChargeStep charge{};
    Combat::stepPlayerCharge(m_attackCharge, m_blockCharge, m_chargeSettings, chargeIn, charge);
    m_fireQuick   = charge.fireQuick;
    m_fireCharged = charge.fireCharged;
    const bool wasBlocking = m_offhand.shield.blocking();
    m_offhand.tick(dt, charge.wantShield, toggleLight);
    if (defense)
    {
        if (charge.openChargedParry)
            Combat::openShieldParry(*defense, true, m_chargeSettings);
        else if (!wasBlocking && m_offhand.shield.blocking() && !m_blockCharge.isCharged())
            Combat::openShieldParry(*defense, false, m_chargeSettings);
        Combat::syncShieldDefense(*defense, m_offhand.shield, m_playLookYaw);
    }

    PlayerMotorInput motorIn{};
    PlayMotorEdges edges{};
    edges.sprintDown    = input().actionDown("sprint");
    edges.crouchPressed = input().actionPressed("crouch");
    edges.crouchDown    = input().actionDown("crouch");
    edges.jumpPressed   = input().actionPressed("jump");
    edges.jumpDown      = input().actionDown("jump");
    edges.forward       = input().actionPressed("dodge_forward");
    edges.back          = input().actionPressed("dodge_back");
    edges.left          = input().actionPressed("dodge_left");
    edges.right         = input().actionPressed("dodge_right");
    const bool sprintAllowed = canSteer && !jumpBusy && !uiKeys;
    buildPlayMotorInput(motorIn, m_crouchLatch, wish, flat, right, canSteer, jumpBusy, uiKeys, inAirCommit, jump, sprintAllowed, edges);
    const bool onGround = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch || motor->state() == PlayerMoveState::Dodge);
    const Terrain::GroundContact groundSurf = (onGround && xf) ? groundContactAt(xf->position.x, xf->position.z) : Terrain::GroundContact{};
    motorIn.speedScale      = (status ? status->moveSpeedScale() : 1.0f) * m_offhand.shield.speedScale() * (onGround ? groundSurf.moveSpeed : 1.0f);
    if (skills)
    {
        motorIn.runScale  = skillScalar(SkillId::Run, SkillScalar::RunScale, skills->level(SkillId::Run));
        motorIn.swimScale = skillScalar(SkillId::Swim, SkillScalar::SwimScale, skills->level(SkillId::Swim));
        motorIn.jumpScale = skillScalar(SkillId::Jump, SkillScalar::JumpScale, skills->level(SkillId::Jump));
    }

    m_playGroundProbeY = xf->position.y;
    m_playGroundIgnore = m_physics.valid() ? m_physics.bodyOf(body) : Physics::kNullPhysicsBody;

    PlayerGroundQuery ground{};
    ground.user    = this;
    ground.waterY  = -1.0e9f;
    ground.heightAt = [](void* user, float x, float z) {
        return static_cast<const EditorApp*>(user)->playGroundHeight(x, z);
    };
    const PlayWallProbe walls{ &m_physics, m_playGroundIgnore, m_physicsGround };
    bindWallProbe(ground, walls);

    const Vector3f before = xf->position;
    PlayerMotorResult motorOut{};
    if (motor)
        motorOut = motor->tick(xf->position, motorIn, dt, ground);
    Vector3f hitSlide{};
    if (hitRx)
    {
        hitSlide = hitRx->tick(dt);
        xf->position += hitSlide;
    }

    if (inAirCommit && jump && motor)
    {
        Vector3f vel = motor->velocity();
        jump->applyAirSteering(vel, xf->position, flat, nullptr, false, dt);
        motor->setHorizontalVelocity(vel.x, vel.z);
        if (jump->phase() == Combat::JumpAttackPhase::Connected)
        {
            Entity tgt = jump->connectedTarget();
            if (const TransformComponent* tx = tgt.valid() ? world().get<TransformComponent>(tgt) : nullptr)
                jump->applyConnectSnap(xf->position, tx->position, dt);
        }
    }

    ensurePlayPhysicsVolumes();
    Physics::syncFoliageCollision(m_physics, m_foliageCollision, m_terrain, &m_foliage, xf->position.x, xf->position.z);

    Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
    if (delta.MagnitudeSqrd() > 1.0e-10f && !motorOut.climbed)
    {
        auto livePhysics = [&](Entity solid) {
            const PhysicsBodyComponent* body = world().get<PhysicsBodyComponent>(solid);
            return body && body->valid && body->mode != PhysicsBodyMode::Static && m_physics.bodyValid(body->body);
        };
        Sphere3f ball{ Vector3f{ before.x, xf->position.y, before.z }, kPlayBodyRadius };
        float bestT = 1.0f;
        for (size_t i = 0; i < m_playCubes.size(); ++i)
        {
            if (i < m_playCubeEntities.size() && livePhysics(m_playCubeEntities[i]))
                continue;
            if (Dark::Collision::Intersects(ball, m_playCubes[i]))
                continue;
            const Dark::Collision::SweptHit3D hit = Dark::Collision::SweptIntersects(ball, delta, m_playCubes[i]);
            if (hit.hit && hit.t < bestT)
                bestT = hit.t;
        }
        for (size_t i = 0; i < m_playSpheres.size(); ++i)
        {
            if (i < m_playSphereEntities.size() && livePhysics(m_playSphereEntities[i]))
                continue;
            if (Dark::Collision::Intersects(ball, m_playSpheres[i]))
                continue;
            const Dark::Collision::SweptHit3D hit = Dark::Collision::SweptIntersects(ball, delta, m_playSpheres[i]);
            if (hit.hit && hit.t < bestT)
                bestT = hit.t;
        }
        // Origin sits groundOffset (0.5 m) above the feet. The capsule bottom is just above the floor.
        bestT = tightenPlanarClip(m_physics, m_physics.bodyOf(body), m_physicsGround, Vector3f{ before.x, xf->position.y, before.z }, delta, bestT);
        commitPlanarClip(xf->position, before, delta, bestT, dt, motor);
    }
    if (motor && skills)
    {
        MotorXpSample sample{};
        sample.state        = motor->state();
        sample.sprint       = motorIn.sprint;
        sample.crouch       = motorIn.crouch;
        sample.dodged       = motorOut.dodged;
        sample.jumped       = motorOut.jumped;
        sample.doubleJump   = motorOut.doubleJumped;
        sample.jumpBusy     = jump && jump->busy();
        sample.planarMetres = locomotionMetres(xf->position - before, hitSlide);
        noteMotorXp(*skills, sample);
    }

    if (jump)
        jump->tick(dt);
    if (jump && motorOut.splashed)
        jump->onSplashed();
    else if (jump && motorOut.landed)
    {
        jump->onLanded(xf->position);
        Combat::DamageEvent pound[8]{};
        const int n = jump->tryPound(makePlayWeaponQuery(), xf->position, pound, 8);
        if (n > 0)
            resolvePlayHits(pound, n);
    }
    else if (jump && jump->phase() == Combat::JumpAttackPhase::Leap)
    {
        Combat::DamageEvent ev{};
        if (jump->tryConnect(makePlayWeaponQuery(), xf->position, flat, ev))
            resolvePlayHits(&ev, 1);
    }
    if (poise)
        poise->hyperArmor = inAirCommit;

    if (canSteer && flat.MagnitudeSqrd() > 1.0e-6f)
        xf->rotation = Quaternion::FromLookRotation(flat, Vector3f::Y_AXIS);
    const bool blockStriking = defense && defense->chargedParry && defense->inParryWindow();
    stepChargeWindup(m_chargeWindup, dt,
        m_attackCharge.phase() != Combat::HoldChargePhase::Idle, m_attackCharge.heldSeconds(), m_chargeSettings.attackWindowSeconds,
        m_blockCharge.phase() != Combat::HoldChargePhase::Idle, m_blockCharge.heldSeconds(), m_chargeSettings.blockWindowSeconds,
        blockStriking);
    if (TransformComponent* shieldXf = m_playShield.valid() ? world().get<TransformComponent>(m_playShield) : nullptr)
        placePlayerShield(*shieldXf, *xf, m_offhand.shield.alpha(), m_chargeWindup.block, m_chargeWindup.blockStrike);

    if (motorOut.jumped)
        playSoundCue(world(), audio(), assets(), body, "jump");
    if (motorOut.landed)
        playSoundCue(world(), audio(), assets(), body, "land");
    if (motorOut.landed)
        m_playFootstepAcc = 0.0f;
    const float stepSpeed = Vector3f{ xf->position.x - before.x, 0.0f, xf->position.z - before.z }.Magnitude() / Math::Max(dt, 1.0e-4f);
    const bool  stepping  = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch) && stepSpeed > 2.0f;
    if (stepping)
    {
        const Terrain::GroundContact stepGround = groundContactAt(xf->position.x, xf->position.z);
        m_playFootstepAcc += dt * stepSpeed * 0.35f;
        if (m_playFootstepAcc >= 1.0f)
        {
            m_playFootstepAcc = 0.0f;
            const char* cue = (stepGround.cue && stepGround.cue[0]) ? stepGround.cue : "step";
            Vector3f facing = flat;
            if (facing.MagnitudeSqrd() <= 1.0e-6f)
                facing = xf->rotation.Rotate(Vector3f::Z_AXIS);
            spawnFootmark(xf->position, facing);
            if (!playSoundCue(world(), audio(), assets(), body, cue))
                playSoundCue(world(), audio(), assets(), body, "step");
        }
    }
    else
        m_playFootstepAcc = 0.0f;

    if (AnimGraphComponent* ag = world().get<AnimGraphComponent>(body))
    {
        AimLocomotion aim{};
        if (motor && !(hp && !hp->alive()))
            aim = aimLocomotion(motor->velocity(), xf->rotation);
        const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
        if (crouched)
        {
            aim.strafe   = 0.0f;
            aim.backward = false;
        }
        ag->graph.setFloat("speed", aim.speed);
        ag->graph.setFloat("strafe", aim.strafe);
        ag->graph.setBool("backward", aim.backward);
        ag->graph.setBool("crouch", crouched);
        m_playLowerBodyYaw = approachAngle(m_playLowerBodyYaw, aim.lowerYaw, 10.0f, dt);
        ag->graph.player().setLowerBodyYaw(m_playLowerBodyYaw);
        ag->graph.player().setChargeWindup(m_chargeWindup.attack, m_chargeWindup.block, m_chargeWindup.blockStrike);
        const float playScale = (motor && motor->state() == PlayerMoveState::Dodge) ? motor->settings().dodgeAnimSpeed : 1.0f;
        ag->graph.setPlaybackScale(playScale);
    }

    updatePlayCamera();
    syncPlayerFlashlight(world(), body, m_camera.GetPosition(), m_camera.GetLook(), m_camera.GetRight(), m_camera.GetUp(), m_offhand.lightOn);

    world().each<Combat::PoiseComponent>([&](Entity e, Combat::PoiseComponent& p) {
        if (body.valid() && e.id() == body.id())
            return;
        p.tick(dt);
    });

    if (hp && hp->alive() && !inAirCommit)
        applyContactGrind(world(), m_combat, body, xf->position, dt);

    WeaponLoadoutComponent* wlc = world().get<WeaponLoadoutComponent>(body);
    if (wlc && wlc->loadout)
        wlc->loadout->tick(dt, makePlayWeaponQuery());

    if (input().actionPressed("weapon_1") && wlc && wlc->loadout)
        wlc->loadout->selectMelee();
    if (input().actionPressed("weapon_2") && wlc && wlc->loadout)
        wlc->loadout->selectProjectile();

    if (m_jumpAttackBuffer > 0.0f)
        m_jumpAttackBuffer = Math::Max(0.0f, m_jumpAttackBuffer - dt);

    const bool attackPressed = input().actionPressed("attack") || (pointerFree && input().actionMousePressed("attack"));
    const bool fireQuick     = m_fireQuick;
    const bool fireCharged   = m_fireCharged;
    m_fireQuick   = false;
    m_fireCharged = false;
    const bool airborne = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
    auto noteSenses = [&]() {
        if (!senseAlive)
            return;
        if (SkillComponent* sk = world().get<SkillComponent>(body))
        {
            const bool foreign = audio().liveForeignSpatialVoices(body.id()) > 0;
            notePlayerSenseXp(*sk, sk->seeArmed, m_offhand.lightOn, foreign, dt);
        }
    };
    if (ccLocked || jumpBusy)
    {
        if (ccLocked)
            m_jumpAttackBuffer = 0.0f;
        noteSenses();
        return;
    }
    auto tryBegin = [&]() -> bool {
        if (!jump || !motor)
            return false;
        const float groundY     = m_terrain.heightAtWorld(xf->position.x, xf->position.z);
        const float heightAbove = xf->position.y - groundY - motor->settings().groundOffset;
        if (heightAbove < jump->def().minHeight && motor->airTime() < jump->def().minAirTime)
            return false;
        Combat::JumpAttackBegin req{};
        req.attacker          = body;
        req.position          = xf->position;
        req.lookFlat          = flat;
        req.velocity          = motor->velocity();
        req.heightAboveGround = heightAbove;
        req.airTime           = motor->airTime();
        if (!jump->begin(req))
            return false;
        motor->clearJumpBuffer();
        if (AnimGraphComponent* ag = world().get<AnimGraphComponent>(body))
            ag->graph.setTrigger("jump_attack");
        return true;
    };
    if (airborne)
    {
        if (attackPressed || m_jumpAttackBuffer > 0.0f)
        {
            if (tryBegin())
                m_jumpAttackBuffer = 0.0f;
            else if (attackPressed)
                m_jumpAttackBuffer = kJumpBuf;
        }
        noteSenses();
        return;
    }
    if (fireCharged)
    {
        m_jumpAttackBuffer = 0.0f;
        firePlayLoadout(true);
    }
    else if (fireQuick || m_jumpAttackBuffer > 0.0f)
    {
        m_jumpAttackBuffer = 0.0f;
        firePlayLoadout(false);
    }
    noteSenses();
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
    return e;
}

void EditorApp::tickSpawners(float dt)
{
    if (!m_playMode || m_sceneMode != SceneMode::Scene3D || dt < 0.0f)
        return;

    struct Job
    {
        Entity          spawner{};
        SpawnerRequest  req{};
    };
    std::vector<Job> jobs;
    world().each<SpawnerComponent>([&](Entity e, SpawnerComponent& spawner) {
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        bool alive[kSpawnerSlotCount]{};
        for (int i = 0; i < kSpawnerSlotCount; ++i)
            alive[i] = spawnerOccupantAlive(world(), spawner.runtime.slots[i].occupant);
        SpawnerRequest reqs[kSpawnerSlotCount];
        const int n = tickSpawner(spawner.desc, spawner.runtime, xf->position.x, xf->position.z, dt, alive, reqs, kSpawnerSlotCount);
        for (int i = 0; i < n; ++i)
            jobs.push_back(Job{ e, reqs[i] });
    });
    for (const Job& job : jobs)
    {
        const Entity unit = spawnSpawnerUnit(job.spawner, job.req);
        SpawnerComponent* spawner = world().get<SpawnerComponent>(job.spawner);
        if (!spawner || job.req.slot < 0 || job.req.slot >= kSpawnerSlotCount)
            continue;
        if (unit.valid())
            spawner->runtime.slots[job.req.slot].occupant = unit;
        else
            spawner->runtime.slots[job.req.slot].tracked = false;
    }
}

void EditorApp::applyAuthoredStealth()
{
    applySceneStealth(m_stealth, m_ai, m_authoredStealth);
}

void EditorApp::tickEditorHunters(float dt)
{
    if (m_sceneMode != SceneMode::Scene3D || dt <= 0.0f)
        return;
    Entity player = (m_playPlayer.valid() && world().alive(m_playPlayer)) ? m_playPlayer : findPlayPlayer();
    if (!player.valid())
        return;
    if (const PlayerMotorComponent* pmc = world().get<PlayerMotorComponent>(player))
        applyPreyFromMotor(m_ai, m_stealth, &pmc->motor, input().actionDown("sprint"));
    if (!m_ai.walkability().valid())
        bakePlayWalkability();
    m_ai.setJumpAttackHits(&EditorApp::onPlayJumpHitsThunk, this);
    m_ai.setHunterCue(&EditorApp::onPlayHunterCueThunk, this);
    const Terrain::TerrainGrid* grid = (m_haveTerrain && m_terrain.valid()) ? &m_terrain : nullptr;
    bindHunterGround(m_ai, &m_ground, grid);
    m_ai.tickHunters(world(), m_terrain, false, dt, player, m_playCubes.empty() ? nullptr : m_playCubes.data(),
                     static_cast<int>(m_playCubes.size()), m_playSpheres.empty() ? nullptr : m_playSpheres.data(),
                     static_cast<int>(m_playSpheres.size()));
}

void EditorApp::updatePawnAnims()
{
    Entity player = (m_playPlayer.valid() && world().alive(m_playPlayer)) ? m_playPlayer : findPlayPlayer();
    const TransformComponent* pxf = player.valid() ? world().get<TransformComponent>(player) : nullptr;
    world().each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
        AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
        if (!ag)
            return;
        const HealthComponent* hp    = world().get<HealthComponent>(e);
        const bool             alive = hp && hp->health.alive();
        if (ag->graphDef)
            ag->graph.setBool("dead", !alive);
        TransformComponent* xf = world().get<TransformComponent>(e);
        if (alive && xf)
        {
            Vector3f fwd = ai.forward;
            fwd.y        = 0.0f;
            if (fwd.MagnitudeSqrd() > 1.0e-6f)
            {
                fwd.Normalize();
                xf->rotation = Quaternion::FromLookRotation(fwd, Vector3f::Y_AXIS);
            }
        }
        if (!ag->graphDef)
            return;
        bool standoff = false;
        if (alive && xf && pxf)
        {
            const float dx = xf->position.x - pxf->position.x;
            const float dz = xf->position.z - pxf->position.z;
            standoff       = (dx * dx + dz * dz) <= (2.25f * 2.25f);
        }
        const BrainComponent* brain = world().get<BrainComponent>(e);
        const AI::Leaf        leaf  = (brain && brain->brain) ? brain->brain->leaf() : AI::Leaf::Wander;
        LocomotionSample      loco{};
        if (alive && xf)
            loco = locomotionSample(ai.planarVelocity, xf->rotation);
        ag->graph.setFloat("speed", loco.speed);
        ag->graph.setFloat("strafe", loco.strafe);

        if (alive && standoff && leaf == AI::Leaf::Chase)
        {
            const char* clip = ag->graph.player().clipName();
            if (!clip
                || (std::strcmp(clip, "SwingSword") != 0 && std::strcmp(clip, "Bite") != 0 && std::strcmp(clip, "Die") != 0
                    && std::strcmp(clip, "Jump") != 0))
                ag->graph.setTrigger("swing");
        }
    });
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
    const char* offhand = m_offhand.lightOn ? "Flashlight" : "Off hand stowed";
    if (m_blockCharge.isCharged())
        offhand = "Shield charged";
    else if (m_offhand.shield.blocking())
        offhand = "Shield up";
    ImGui::TextUnformatted(offhand);
    if (m_attackCharge.isCharged())
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
