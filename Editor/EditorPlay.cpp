#include "EditorApp.h"

#include "AI/AiComponents.h"
#include "AI/Brain.h"
#include "AI/HsmGraph.h"
#include "AI/HsmGraphComponent.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/AnimGraphTick.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Audio/SoundComponents.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Character/PlayerMotorComponent.h"
#include "Collision/Collision.h"
#include "Collision/HitResult.h"
#include "Collision/SweptCollision.h"
#include "Combat/JumpAttack.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/WeaponHitAdapter.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Input/Input.h"
#include "Input/InputCodes.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Render/GpuUpload.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/WeaponLoadout.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <imgui.h>

#include <cmath>
#include <memory>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    constexpr float kPlayMouseSens = 0.0045f;
    constexpr float kPlayPadLook   = 2.1f;
    constexpr float kPlayRadius    = 0.45f;
    constexpr float kJumpBuf       = 0.12f;
    constexpr float kContactDps    = 12.0f;
    constexpr float kStandoff      = 2.25f;

    AssetID loadEditorClip(Audio::AudioSystem& audio, AssetManager& assets, const char* path, float freq, float dur, float amp)
    {
        const auto clip = audio.loadOrBlip(assets, path, freq, dur, amp);
        return (clip && clip->id != NULL_ASSET) ? clip->id : NULL_ASSET;
    }

    void attachEditorHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e)
    {
        SoundBankComponent bank;
        addSoundCue(bank, "pain", loadEditorClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addSoundCue(bank, "grunt", loadEditorClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true);
        addSoundCue(bank, "impact", loadEditorClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addSoundCue(bank, "land", loadEditorClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
        setSoundBank(world, pins, assets, e, std::move(bank));
    }

    void attachEditorPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e)
    {
        SoundBankComponent bank;
        addSoundCue(bank, "jump", loadEditorClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.7f, false);
        addSoundCue(bank, "land", loadEditorClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, false);
        addSoundCue(bank, "pain", loadEditorClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addSoundCue(bank, "fire", loadEditorClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.45f, true);
        addSoundCue(bank, "impact", loadEditorClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
        addSoundCue(bank, "death", loadEditorClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.55f, false);
        setSoundBank(world, pins, assets, e, std::move(bank));
        if (WeaponLoadoutComponent* wlc = world.get<WeaponLoadoutComponent>(e); wlc && wlc->loadout)
        {
            AssetID fireId   = NULL_ASSET;
            AssetID impactId = NULL_ASSET;
            if (const SoundBankComponent* cues = world.get<SoundBankComponent>(e))
            {
                if (const SoundCueDesc* c = findSoundCue(*cues, "fire"))
                    fireId = c->clipId;
                if (const SoundCueDesc* c = findSoundCue(*cues, "impact"))
                    impactId = c->clipId;
            }
            wlc->loadout->projectile().setAudio(&audio, &assets, fireId, impactId);
        }
    }
} // namespace

bool EditorApp::attachEditorModel(Entity e, const char* gltfPath)
{
    if (!e.valid() || !world().alive(e) || !gltfPath || gltfPath[0] == '\0')
        return false;
    if (world().has<AnimGraphComponent>(e) && world().has<ModelComponent>(e))
        return true;

    AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), gltfPath);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("Editor: animated character '{}' failed to load", gltfPath);
        return false;
    }

    AssetRef<AnimGraphDef> graph = assets().tryLoadAnimGraphForModel(gltfPath);
    if (!graph)
        DE_LOG_WARN("Editor: '{}' has no anim graph sidecar", gltfPath);

    ModelComponent mc{};
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    setModelComponent(world(), pins(), assets(), e, mc);

    if (MeshComponent* mesh = world().get<MeshComponent>(e))
    {
        MeshComponent next = *mesh;
        next.primitive     = PrimitiveMesh::None;
        setMeshComponent(world(), pins(), assets(), e, next);
    }

    if (TransformComponent* xf = world().get<TransformComponent>(e))
        xf->scale = Vector3f{ 1.0f, 1.0f, 1.0f };

    AnimGraphComponent ag;
    ag.model    = model;
    ag.animSet  = model->animationSet();
    ag.graphDef = graph;
    if (graph && model->skeleton())
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet && model->skeleton())
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    tickAnimGraphs(world(), assets(), 1.0f / 60.0f);
    DE_LOG_INFO("Editor: attached '{}' graph={}", gltfPath, graph ? "yes" : "no");
    return true;
}

bool EditorApp::attachEditorPlayer(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return false;

    if (!world().has<HealthComponent>(e))
    {
        HealthSettings playerHp;
        playerHp.maxHp       = 100.0f;
        playerHp.regenPerSec = 10.0f;
        playerHp.regenDelay  = 3.5f;
        HealthComponent hc{};
        hc.health = Health{ playerHp };
        world().emplace<HealthComponent>(e, std::move(hc));
    }
    if (!world().has<HitReactionComponent>(e))
    {
        HitReactionSettings hit{};
        hit.stunSeconds       = 0.28f;
        hit.knockbackDistance = 1.1f;
        hit.knockbackSeconds  = 0.14f;
        hit.horizontalOnly    = true;
        HitReactionComponent hr{};
        hr.hit.setSettings(hit);
        world().emplace<HitReactionComponent>(e, std::move(hr));
    }
    if (!world().has<HittableComponent>(e))
    {
        HittableComponent h{};
        h.halfExtents = Vector3f{ 0.35f, 0.7f, 0.35f };
        world().emplace<HittableComponent>(e, h);
    }
    if (!world().has<PlayerMotorComponent>(e))
        world().emplace<PlayerMotorComponent>(e);
    if (!world().has<HsmGraphComponent>(e))
    {
        HsmGraphComponent hsm{};
        hsm.def      = assets().tryLoadHsmGraph("ai/player.hsm.json");
        hsm.instance = std::make_unique<HsmGraphInstance>();
        const bool built = hsm.def ? hsm.instance->build(*hsm.def) : hsm.instance->build(makePlayerHsmGraph());
        if (built)
        {
            hsm.instance->start();
            world().emplace<HsmGraphComponent>(e, std::move(hsm));
        }
        else
            DE_LOG_WARN(LogCategory::AI, "Editor: player HSM build failed");
    }
    if (!world().has<WeaponLoadoutComponent>(e))
    {
        auto& wlc   = world().emplace<WeaponLoadoutComponent>(e);
        wlc.loadout = std::make_unique<WeaponLoadout>();
        wlc.loadout->setHitListener(&EditorApp::onPlayWeaponHitThunk, this);
        wlc.slot = wlc.loadout->slot();
    }
    else if (WeaponLoadoutComponent* wlc = world().get<WeaponLoadoutComponent>(e); wlc && wlc->loadout)
        wlc->loadout->setHitListener(&EditorApp::onPlayWeaponHitThunk, this);
    if (!world().has<JumpAttackComponent>(e))
    {
        auto& jac = world().emplace<JumpAttackComponent>(e);
        Combat::JumpAttackDef def = jac.jump.def();
        def.telegraphSeconds = 0.0f;
        def.cooldown         = 1.25f;
        def.connectFlags     = Combat::DamageFlags::CanBlock | Combat::DamageFlags::HardCc | Combat::DamageFlags::Knockdown;
        jac.jump.setDef(def);
    }
    if (!world().has<Combat::StatusEffectComponent>(e))
        world().emplace<Combat::StatusEffectComponent>(e);
    if (!world().has<Combat::PoiseComponent>(e))
        world().emplace<Combat::PoiseComponent>(e);

    attachEditorModel(e, "models/human.gltf");
    attachEditorPlayerSounds(world(), pins(), assets(), audio(), e);
    return true;
}

bool EditorApp::attachEditorHunter(Entity e)
{
    if (!m_ai.attachHunter(world(), e, pins(), assets()))
        return false;
    if (!attachEditorModel(e, "models/skeleton.gltf"))
    {
        MeshComponent mc{};
        mc.matAssetID  = m_propMaterial ? m_propMaterial->id : NULL_ASSET;
        mc.meshAssetID = NULL_ASSET;
        setMeshComponent(world(), pins(), assets(), e, mc);
        DE_LOG_WARN("Editor: hunter using cube proxy (skeleton model missing)");
    }
    if (HittableComponent* hit = world().get<HittableComponent>(e))
        hit->halfExtents = Vector3f{ 0.4f, 0.7f, 0.4f };
    attachEditorHunterSounds(world(), pins(), assets(), audio(), e);
    return true;
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
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::Cube)
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        const Vector3f half{ 0.5f * xf->scale.x, 0.5f * xf->scale.y, 0.5f * xf->scale.z };
        m_playCubes.push_back(AABox3f::FromCenterExtents(xf->position, half));
    });
    if (!m_haveTerrain || !m_terrain.heightMap().valid())
    {
        DE_LOG_WARN(LogCategory::AI, "Editor: play without terrain — hunters will not path");
        return;
    }
    AI::WalkabilityDesc d;
    d.heightMap   = &m_terrain.heightMap();
    d.waterLevel  = -1.0e9f;
    d.agentRadius = 0.8f;
    d.cubes       = m_playCubes.empty() ? nullptr : m_playCubes.data();
    d.cubeCount   = static_cast<int>(m_playCubes.size());
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
        resetPlayCombat();
        m_jumpAttackBuffer = 0.0f;
        m_gizmoDragAxis    = EditorDetail::TranslateGizmoAxis::None;
        m_dragging         = false;
        m_playMode         = true;
        DE_LOG_INFO("Editor: PLAY — WASD move, mouse look, Space jump, LMB/F attack, 1/2 weapons, F12 stop");
    }
    else
    {
        if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
        restoreAuthoredPoses();
        resetPlayCombat();
        m_playMode   = false;
        m_playPlayer = {};
        DE_LOG_INFO("Editor: play stopped — restored spawn poses");
    }
}

void EditorApp::togglePlayMode()
{
    setPlayMode(!m_playMode);
}

void EditorApp::updatePlayCamera()
{
    const TransformComponent* xf = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
    if (!xf)
        return;
    const Vector3f look{
        std::sinf(m_playLookYaw) * std::cosf(m_playLookPitch),
        std::sinf(m_playLookPitch),
        std::cosf(m_playLookYaw) * std::cosf(m_playLookPitch)
    };
    const Vector3f focus = xf->position + Vector3f{ 0.0f, 1.15f, 0.0f };
    const Vector3f eye   = focus - look * 5.5f + Vector3f{ 0.0f, 0.35f, 0.0f };
    m_camera.LookAt(eye, focus, Vector3f::Y_AXIS);
}

WeaponWorldQuery EditorApp::makePlayWeaponQuery()
{
    m_playWeaponTargets.clear();
    const Entity self = m_playPlayer;
    world().each<HittableComponent>([&](Entity e, HittableComponent& h) {
        if (self.valid() && e.id() == self.id())
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        const HealthComponent*    hp = world().get<HealthComponent>(e);
        if (!xf)
            return;
        PlayWeaponTarget t{};
        t.entity      = e;
        t.center      = xf->position;
        t.halfExtents = h.halfExtents;
        t.alive       = hp && hp->health.alive();
        m_playWeaponTargets.push_back(t);
    });

    WeaponWorldQuery q{};
    q.terrainUser = this;
    q.raycastTerrain = [](void* user, const Ray3f& ray, float) -> Collision::RayHit3D {
        return static_cast<EditorApp*>(user)->m_terrain.raycast(ray);
    };
    q.heightAt = [](void* user, float x, float z) {
        return static_cast<EditorApp*>(user)->m_terrain.heightAtWorld(x, z);
    };
    q.targetUser = this;
    q.targetCount = [](void* user) {
        return static_cast<int>(static_cast<EditorApp*>(user)->m_playWeaponTargets.size());
    };
    q.targetAlive = [](void* user, int i) {
        auto* app = static_cast<EditorApp*>(user);
        return i >= 0 && i < static_cast<int>(app->m_playWeaponTargets.size()) && app->m_playWeaponTargets[static_cast<size_t>(i)].alive;
    };
    q.targetCenter = [](void* user, int i) {
        auto* app = static_cast<EditorApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_playWeaponTargets.size()))
            return Vector3f{};
        return app->m_playWeaponTargets[static_cast<size_t>(i)].center;
    };
    q.targetHalfExtentsAt = [](void* user, int i) {
        auto* app = static_cast<EditorApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_playWeaponTargets.size()))
            return Vector3f{ 1.0f, 1.0f, 1.0f };
        return app->m_playWeaponTargets[static_cast<size_t>(i)].halfExtents;
    };
    q.targetEntityAt = [](void* user, int i) {
        auto* app = static_cast<EditorApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_playWeaponTargets.size()))
            return Entity{};
        return app->m_playWeaponTargets[static_cast<size_t>(i)].entity;
    };
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
    if (!hit.hitTarget || !hit.targetEntity.valid())
        return;
    Combat::DamageEvent ev = Combat::damageEventFromWeaponHit(hit, m_playPlayer);
    Combat::ResolveResult r = m_combat.resolve(world(), ev);
    if (!r.applied)
        return;
    playSoundCueAt(world(), audio(), assets(), hit.targetEntity, "pain", hit.point);
    if (world().has<AiAgentComponent>(hit.targetEntity))
    {
        const TransformComponent* px = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
        m_ai.onHunterAttacked(world(), hit.targetEntity, px ? px->position : Vector3f{});
        if (r.killed)
            m_ai.onHunterKilled(world(), hit.targetEntity);
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
            if (HealthComponent* hp = world().get<HealthComponent>(t); hp && !hp->health.alive())
                m_ai.onHunterKilled(world(), t);
        }
    }
}

bool EditorApp::firePlayLoadout()
{
    const Entity body = m_playPlayer;
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    WeaponLoadoutComponent* wlc = body.valid() ? world().get<WeaponLoadoutComponent>(body) : nullptr;
    if (!wlc || !wlc->loadout)
        return false;
    WeaponFireRequest req{};
    req.direction = m_camera.GetLook();
    if (req.direction.MagnitudeSqrd() > 1.0e-6f)
        req.direction.Normalize();
    else
        req.direction = Vector3f{ 0.0f, 0.0f, 1.0f };
    req.origin   = m_camera.GetPosition() + req.direction * 2.2f;
    req.ownerPos = xf ? xf->position : m_camera.GetPosition();
    if (!wlc->loadout->fire(req, makePlayWeaponQuery()))
        return false;
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
    TransformComponent* xf = world().get<TransformComponent>(body);
    if (!xf)
        return;

    const bool uiKeys  = m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    const bool uiMouse = m_imgui.isReady() && m_imgui.wantCaptureMouse();
    if (!uiMouse)
    {
        m_playLookYaw += static_cast<float>(input().mouseDeltaX()) * kPlayMouseSens;
        m_playLookPitch += static_cast<float>(input().mouseDeltaY()) * kPlayMouseSens;
    }
    m_playLookYaw += input().actionAxis("look_x") * kPlayPadLook * dt;
    m_playLookPitch += -input().actionAxis("look_y") * kPlayPadLook * dt;
    m_playLookYaw   = Math::WrapPi(m_playLookYaw);
    m_playLookPitch = Math::Clamp(m_playLookPitch, -0.96f, 0.96f);

    Vector3f look{
        std::sinf(m_playLookYaw) * std::cosf(m_playLookPitch),
        std::sinf(m_playLookPitch),
        std::cosf(m_playLookYaw) * std::cosf(m_playLookPitch)
    };
    Vector3f flat = look;
    flat.y        = 0.0f;
    if (flat.MagnitudeSqrd() > 1.0e-6f)
        flat.Normalize();
    Vector3f right = Vector3f{ 0.0f, 1.0f, 0.0f }.Cross(flat);
    if (right.MagnitudeSqrd() > 1.0e-6f)
        right.Normalize();

    const float mx = uiKeys ? 0.0f : input().actionAxis("move_x");
    const float mz = uiKeys ? 0.0f : input().actionAxis("move_z");
    Vector3f    wish = right * mx + flat * mz;
    const float mag  = wish.Magnitude();
    if (mag > 1.0f)
        wish *= (1.0f / mag);

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
    if (poise)
        poise->tick(dt);
    if (hp)
        hp->tick(dt);

    if (jump && (!hp || !hp->alive()))
        jump->cancel(Combat::JumpAttackCancel::NoPound);

    const bool ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
    const bool jumpBusy    = jump && jump->busy();
    const bool inAirCommit = jump && jump->inAirCommit();
    const bool canSteer    = hp && hp->alive() && !ccLocked && !(jump && jump->phase() == Combat::JumpAttackPhase::Pound);

    PlayerMotorInput motorIn{};
    motorIn.wish            = canSteer ? wish : Vector3f{};
    motorIn.sprint          = canSteer && !jumpBusy && !uiKeys && input().actionDown("sprint");
    motorIn.jumpPressed     = canSteer && !jumpBusy && !uiKeys && input().actionPressed("jump");
    motorIn.allowDoubleJump = !inAirCommit;
    motorIn.allowJumpBuffer = !jumpBusy;
    motorIn.airControlScale = inAirCommit && jump ? jump->def().airControlScale : 1.0f;
    motorIn.speedScale      = status ? status->moveSpeedScale() : 1.0f;

    PlayerGroundQuery ground{};
    ground.user    = this;
    ground.waterY  = -1.0e9f;
    ground.heightAt = [](void* user, float x, float z) {
        return static_cast<EditorApp*>(user)->m_terrain.heightAtWorld(x, z);
    };

    const Vector3f before = xf->position;
    PlayerMotorResult motorOut{};
    if (motor)
        motorOut = motor->tick(xf->position, motorIn, dt, ground);
    if (hitRx)
        xf->position += hitRx->tick(dt);

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

    Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
    if (delta.MagnitudeSqrd() > 1.0e-10f)
    {
        Sphere3f ball{ Vector3f{ before.x, xf->position.y, before.z }, kPlayRadius };
        for (const AABox3f& cube : m_playCubes)
        {
            const Dark::Collision::SweptHit3D hit = Dark::Collision::SweptIntersects(ball, delta, cube);
            if (hit.hit && hit.t < 1.0f)
            {
                delta *= Math::Max(0.0f, hit.t - 0.02f);
                break;
            }
        }
        xf->position.x = before.x + delta.x;
        xf->position.z = before.z + delta.z;
        if (dt > 1.0e-4f && motor)
            motor->setHorizontalVelocity(delta.x / dt, delta.z / dt);
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

    if (motorOut.jumped)
        playSoundCue(world(), audio(), assets(), body, "jump");
    if (motorOut.landed)
        playSoundCue(world(), audio(), assets(), body, "land");

    if (AnimGraphComponent* ag = world().get<AnimGraphComponent>(body))
    {
        float speed = 0.0f;
        if (motor)
        {
            const Vector3f v = motor->velocity();
            speed            = Vector3f{ v.x, 0.0f, v.z }.Magnitude();
        }
        if (hp && !hp->alive())
            speed = 0.0f;
        ag->graph.setFloat("speed", speed);
    }

    updatePlayCamera();

    world().each<Combat::PoiseComponent>([&](Entity e, Combat::PoiseComponent& p) {
        if (body.valid() && e.id() == body.id())
            return;
        p.tick(dt);
    });

    if (hp && hp->alive() && !inAirCommit)
    {
        world().each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
            const HealthComponent* hhp = world().get<HealthComponent>(e);
            const TransformComponent* hxf = world().get<TransformComponent>(e);
            if (!hhp || !hhp->health.alive() || !hxf)
                return;
            const float dx = hxf->position.x - xf->position.x;
            const float dz = hxf->position.z - xf->position.z;
            if (dx * dx + dz * dz > kStandoff * kStandoff)
                return;
            hp->applyDamage(kContactDps * dt);
        });
    }

    WeaponLoadoutComponent* wlc = world().get<WeaponLoadoutComponent>(body);
    if (wlc && wlc->loadout)
        wlc->loadout->tick(dt, makePlayWeaponQuery());

    if (input().actionPressed("weapon_1") && wlc && wlc->loadout)
        wlc->loadout->selectMelee();
    if (input().actionPressed("weapon_2") && wlc && wlc->loadout)
        wlc->loadout->selectProjectile();

    if (m_jumpAttackBuffer > 0.0f)
        m_jumpAttackBuffer = Math::Max(0.0f, m_jumpAttackBuffer - dt);

    const bool attack   = input().actionPressed("attack") || (!uiMouse && input().mousePressed(MouseButton::Left));
    const bool airborne = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
    if (ccLocked || jumpBusy)
    {
        if (ccLocked)
            m_jumpAttackBuffer = 0.0f;
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
        if (attack || m_jumpAttackBuffer > 0.0f)
        {
            if (tryBegin())
                m_jumpAttackBuffer = 0.0f;
            else if (attack)
                m_jumpAttackBuffer = kJumpBuf;
        }
        return;
    }
    if (attack || m_jumpAttackBuffer > 0.0f)
    {
        m_jumpAttackBuffer = 0.0f;
        firePlayLoadout();
    }
}

void EditorApp::tickEditorHunters(float dt)
{
    if (m_sceneMode != SceneMode::Scene3D || dt <= 0.0f)
        return;
    Entity player = (m_playPlayer.valid() && world().alive(m_playPlayer)) ? m_playPlayer : findPlayPlayer();
    if (!player.valid())
        return;
    if (!m_ai.walkability().valid())
        bakePlayWalkability();
    m_ai.setJumpAttackHits(&EditorApp::onPlayJumpHitsThunk, this);
    m_ai.setHunterCue(&EditorApp::onPlayHunterCueThunk, this);
    m_ai.tickHunters(world(), m_terrain, false, dt, player, m_playCubes.empty() ? nullptr : m_playCubes.data(),
                     static_cast<int>(m_playCubes.size()));
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
        float                 speed = 0.0f;
        float                 statusScale = 1.0f;
        if (const Combat::StatusEffectComponent* st = world().get<Combat::StatusEffectComponent>(e))
            statusScale = st->moveSpeedScale();
        if (alive && !standoff)
        {
            if (leaf == AI::Leaf::Assist || leaf == AI::Leaf::Flee)
                speed = 18.0f * statusScale;
            else if (leaf == AI::Leaf::Chase || leaf == AI::Leaf::Memory)
                speed = 12.0f * statusScale;
            else
                speed = 9.0f * statusScale;
        }
        ag->graph.setFloat("speed", speed);
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
    if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
        ImGui::Text("Jump CD  %.2fs", static_cast<double>(jac->jump.cooldownLeft()));
    int hunters = 0;
    int alive   = 0;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::Hunter)
            return;
        ++hunters;
        const HealthComponent* h = world().get<HealthComponent>(e);
        if (h && h->health.alive())
            ++alive;
    });
    ImGui::Text("Hunters  %d / %d", alive, hunters);
    ImGui::TextDisabled("F12 / Esc  stop");
    ImGui::End();
}
