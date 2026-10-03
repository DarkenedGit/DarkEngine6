#include "EditorApp.h"

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

    void addGroundFootsteps(SoundBankComponent& bank, Audio::AudioSystem& audio, AssetManager& assets, const Terrain::TerrainGround& ground)
    {
        for (int i = 0; i < ground.layerCount(); ++i)
        {
            const Terrain::GroundContact& layer = ground.layer(i);
            if (!layer.cue || !layer.cue[0])
                continue;
            addSoundCue(bank, layer.cue, loadEditorClip(audio, assets, layer.footstep ? layer.footstep : "audio/foot_dirt.wav", layer.blipHz, 0.05f, 0.4f), 0.4f, true);
        }
    }

    void attachEditorHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground);
        addSoundCue(bank, "pain", loadEditorClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
        addSoundCue(bank, "grunt", loadEditorClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true);
        addSoundCue(bank, "growl", loadEditorClip(audio, assets, "audio/growl.wav", 90.0f, 0.55f, 0.5f), 0.9f, true);
        const AssetID impactClip = loadEditorClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f);
        addSoundCue(bank, "impact", impactClip, 0.5f, true);
        // Catalog Poison/Ignite applyCue is "fire"; hunters have no weapon fire row — alias impact so one-shots aren't silent.
        addSoundCue(bank, "fire", impactClip, 0.5f, true);
        addSoundCue(bank, "land", loadEditorClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
        setSoundBank(world, pins, assets, e, std::move(bank));
    }

    void attachEditorPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
    {
        SoundBankComponent bank;
        addGroundFootsteps(bank, audio, assets, ground);
        addSoundCue(bank, "step", loadEditorClip(audio, assets, "audio/foot_grass.wav", 160.0f, 0.05f, 0.4f), 0.35f, true);
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
    if (world().has<ModelComponent>(e) && world().get<ModelComponent>(e)->modelAssetID != NULL_ASSET)
        return true;

    AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), gltfPath);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("Editor: model '{}' failed to load", gltfPath);
        return false;
    }

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

    if (!model->skeleton())
    {
        tryLoadModelPhysics(e);
        DE_LOG_INFO("Editor: attached '{}' (static)", gltfPath);
        return true;
    }

    const std::string modelStem = std::filesystem::path(gltfPath).stem().string();
    const EntityMaster modelMaster = loadEntityMasterType(modelStem);
    AssetRef<AnimGraphDef> graph;
    if (!modelMaster.anim.empty())
        graph = assets().loadAnimGraph(modelMaster.anim);
    if (!graph)
        graph = assets().tryLoadAnimGraphForModel(gltfPath);
    if (!graph && model->skeleton())
        DE_LOG_WARN("Editor: '{}' has no anim graph sidecar", gltfPath);

    AnimGraphComponent ag;
    ag.model    = model;
    ag.animSet  = model->animationSet();
    ag.graphDef = graph;
    if (graph)
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet)
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    tickAnimGraphs(world(), assets(), 1.0f / 60.0f);
    tryLoadModelPhysics(e);
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
        const EntityMaster playerMaster = loadEntityMasterType("player");
        const std::string  hsmPath = playerMaster.hsm.empty() ? std::string("ai/player.hsm.json") : playerMaster.hsm;
        hsm.def      = assets().tryLoadHsmGraph(hsmPath);
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
    Combat::equipPlayerShield(world(), e);

    const EntityMaster playerMaster = loadEntityMasterType("player");
    const std::string  playerGltf = playerMaster.gltf.empty() ? std::string("models/human.gltf") : playerMaster.gltf;
    attachEditorModel(e, playerGltf.c_str());
    attachEditorPlayerSounds(world(), pins(), assets(), audio(), e, m_ground);
    applySkillProfile(world(), e, "player");
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
    attachEditorHunterSounds(world(), pins(), assets(), audio(), e, m_ground);
    return true;
}

bool EditorApp::attachEditorWolf(Entity e)
{
    if (!m_ai.attachHunter(world(), e, pins(), assets()))
        return false;
    const EntityMaster wolfMaster = loadEntityMasterType("wolf");
    const std::string  wolfGltf = wolfMaster.gltf.empty() ? std::string("models/wolf.gltf") : wolfMaster.gltf;
    if (!attachEditorModel(e, wolfGltf.c_str()))
    {
        MeshComponent mc{};
        mc.matAssetID  = m_propMaterial ? m_propMaterial->id : NULL_ASSET;
        mc.meshAssetID = NULL_ASSET;
        setMeshComponent(world(), pins(), assets(), e, mc);
        DE_LOG_WARN("Editor: wolf using cube proxy (wolf model missing)");
    }
    if (HittableComponent* hit = world().get<HittableComponent>(e))
        hit->halfExtents = Vector3f{ 0.45f, 0.45f, 0.70f };
    if (TagComponent* tag = world().get<TagComponent>(e))
        tag->name = "Wolf";
    applySkillProfile(world(), e, "wolf");
    attachEditorHunterSounds(world(), pins(), assets(), audio(), e, m_ground);
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
    d.cubes       = m_playCubes.empty() ? nullptr : m_playCubes.data();
    d.cubeCount   = static_cast<int>(m_playCubes.size());
    d.spheres     = m_playSpheres.empty() ? nullptr : m_playSpheres.data();
    d.sphereCount = static_cast<int>(m_playSpheres.size());
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
        ensurePlayGear();
        m_jumpAttackBuffer = 0.0f;
        m_gizmoDragAxis    = EditorDetail::TranslateGizmoAxis::None;
        m_dragging         = false;
        m_playMode = true;
        ensurePhysicsWorld();
        rebuildPhysicsGround(); // pick up terrain Generate / sculpt since the last Play
        if (m_physics.valid())
            m_physics.pushPoses(world(), true);
        window().setCursorCaptured(window().isFocused());
        ensurePlaySession();
        installSaveHost();
        if (!world().has<LookComponent>(m_playPlayer))
            world().emplace<LookComponent>(m_playPlayer);
        if (!world().has<RunProgressComponent>(m_playPlayer))
            world().emplace<RunProgressComponent>(m_playPlayer);
        m_save.captureBaseline(world());
        DE_LOG_INFO("Editor: PLAY — WASD move, mouse look, Space jump, hold LMB/F to charge an attack and release to swing, hold RMB/V to block, Ctrl/C crouch, L flashlight, Escape or F12 stop");
    }
    else
    {
        if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
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
    if (!m_playFlashlight.valid() || !world().alive(m_playFlashlight))
        m_playFlashlight = spawnPlayerFlashlight(world());
    if (!m_playShield.valid() || !world().alive(m_playShield))
        m_playShield = spawnPlayerShield(world(), pins(), assets(), renderer());
}

void EditorApp::destroyPlayGear()
{
    destroyPlayerShield(world(), pins(), m_playShield);
    destroyPlayerFlashlight(world(), m_playFlashlight);
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
    const PlayerMotor* motor = nullptr;
    if (const PlayerMotorComponent* pmc = world().get<PlayerMotorComponent>(m_playPlayer))
        motor = &pmc->motor;
    const float focusY = (motor && motor->state() == PlayerMoveState::Crouch) ? 0.78f : 1.15f;
    const Vector3f focus = xf->position + Vector3f{ 0.0f, focusY, 0.0f };
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
    // Terrain hits are !hitTarget. The mark still lands; damage stays behind that gate.
    spawnWeaponImpact(hit);
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

namespace
{

    bool decalPointFinite(const Vector3f& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    Vector3f decalSafeNormal(const Vector3f& n)
    {
        if (n.MagnitudeSqrd() <= 1.0e-8f)
            return Vector3f(0.0f, 1.0f, 0.0f);
        Vector3f out = n;
        out.Normalize();
        return out;
    }

    Vector3f decalFlatFacing(const Vector3f& facing)
    {
        Vector3f f(facing.x, 0.0f, facing.z);
        if (f.MagnitudeSqrd() <= 1.0e-8f)
            return Vector3f(0.0f, 0.0f, 1.0f);
        f.Normalize();
        return f;
    }

    Vector3f decalYawFacing(float x, float z)
    {
        const uint32_t hx  = static_cast<uint32_t>(std::fabs(x) * 1000.0f);
        const uint32_t hz  = static_cast<uint32_t>(std::fabs(z) * 1000.0f);
        const uint32_t h   = hx * 1664525u + hz + 1013904223u;
        const float    yaw = static_cast<float>(h & 0xFFFFu) * (6.2831853f / 65535.0f);
        return Vector3f(std::cos(yaw), 0.0f, std::sin(yaw));
    }

}

bool EditorApp::deferredDecals()
{
    return renderer().scenePath() == ScenePath::HybridDeferred
        && renderer().debugState().decalsEnabled
        && m_scene.decalsReady();
}

bool EditorApp::isStepCue(Entity e, const char* cue)
{
    const TransformComponent* xf = e.valid() ? world().get<TransformComponent>(e) : nullptr;
    const char* groundCue = nullptr;
    if (xf)
        groundCue = groundContactAt(xf->position.x, xf->position.z).cue;
    return decalCueIsGroundStep(cue, groundCue);
}

void EditorApp::spawnFootmark(const Vector3f& bodyPos, const Vector3f& facing)
{
    if (!deferredDecals())
        return;
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Footmark;
    desc.space    = DecalSpace::World;
    desc.position = Vector3f(bodyPos.x, m_terrain.heightAtWorld(bodyPos.x, bodyPos.z), bodyPos.z);
    desc.axisY    = Vector3f(0.0f, 1.0f, 0.0f);
    desc.axisX    = decalFlatFacing(facing);
    m_scene.spawnDecal(desc);
}

void EditorApp::spawnHunterFootmark(Entity hunter)
{
    if (!deferredDecals())
        return;
    const TransformComponent* xf = hunter.valid() ? world().get<TransformComponent>(hunter) : nullptr;
    if (!xf)
        return;
    Vector3f facing(0.0f, 0.0f, 1.0f);
    if (const AiAgentComponent* ai = world().get<AiAgentComponent>(hunter))
        facing = ai->forward;
    spawnFootmark(xf->position, facing);
}

void EditorApp::spawnWeaponImpact(const WeaponHit& hit)
{
    if (!deferredDecals() || !decalPointFinite(hit.point))
        return;
    const Vector3f normal = decalSafeNormal(hit.normal);
    DecalSpawnDesc desc;
    desc.kind   = DecalKind::Impact;
    desc.weapon = hit.weapon;
    desc.axisY  = normal;
    desc.axisX  = decalImpactAxisX(normal, hit.direction);
    Entity victim = hit.targetEntity;
    if (!victim.valid() && hit.targetIndex >= 0 && hit.targetIndex < static_cast<int>(m_playWeaponTargets.size()))
        victim = m_playWeaponTargets[static_cast<size_t>(hit.targetIndex)].entity;
    const bool attach = hit.hitTarget && victim.valid() && world().alive(victim) && world().get<TransformComponent>(victim);
    if (attach)
    {
        desc.entity   = victim;
        desc.position = decalShellBiasedPosition(hit.point, normal);
        if (world().has<AnimGraphComponent>(victim))
        {
            desc.space = DecalSpace::Bone;
            desc.bone  = -1;
        }
        else
            desc.space = DecalSpace::Entity;
    }
    else
    {
        desc.space    = DecalSpace::World;
        desc.position = hit.point;
    }
    m_scene.spawnDecal(desc);
}

void EditorApp::spawnLivingBloodDecal(Entity victim, const Vector3f& point, const Vector3f& hitNormal)
{
    if (!deferredDecals() || !decalPointFinite(point))
        return;
    if (!victim.valid() || !world().alive(victim) || !world().get<TransformComponent>(victim))
        return;
    const Vector3f normal = decalSafeNormal(hitNormal);
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Blood;
    desc.entity   = victim;
    desc.position = decalShellBiasedPosition(point, normal);
    desc.axisY    = normal;
    desc.axisX    = decalImpactAxisX(normal, Vector3f(1.0f, 0.0f, 0.0f));
    if (world().has<AnimGraphComponent>(victim))
    {
        desc.space = DecalSpace::Bone;
        desc.bone  = -1;
    }
    else
        desc.space = DecalSpace::Entity;
    m_scene.spawnDecal(desc);
}

void EditorApp::spawnDeathBloodDecal(const Vector3f& point, const Vector3f& hitNormal)
{
    if (!deferredDecals() || !decalPointFinite(point))
        return;
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Blood;
    desc.space    = DecalSpace::World;
    desc.position = Vector3f(point.x, m_terrain.heightAtWorld(point.x, point.z), point.z);
    desc.axisY    = decalDeathAxisY(hitNormal, m_terrain.normalAtWorld(point.x, point.z));
    desc.axisX    = decalYawFacing(point.x, point.z);
    m_scene.spawnDecal(desc);
}

bool EditorApp::firePlayLoadout(bool charged)
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
    req.origin      = m_camera.GetPosition() + req.direction * 2.2f;
    req.ownerPos    = xf ? xf->position : m_camera.GetPosition();
    req.damageScale = charged ? m_chargeSettings.attackDamageScale : 1.0f;
    SkillComponent* skill = nullptr;
    if (wlc->loadout->activeKind() == WeaponKind::Projectile)
        skill = world().get<SkillComponent>(body);
    if (skill)
    {
        const int shootLevel = skill->level(SkillId::Shoot);
        req.recoilScale      = skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, shootLevel);
        req.cooldownScale    = skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, shootLevel);
    }
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
    if (window().cursorCaptured() || !uiMouse)
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
        && ((input().mouseDown(MouseButton::Right) && pointerFree) || (!uiKeys && input().actionDown("shield"))
            || input().axis(GamepadAxis::LeftTrigger) > 0.45f);
    const bool toggleLight = canSteer && !uiKeys && input().actionPressed("flashlight");
    if (toggleLight)
    {
        if (SkillComponent* sk = world().get<SkillComponent>(body))
            sk->seeArmed = true;
    }
    const bool attackDown = canSteer && !uiKeys && (input().actionDown("attack") || (pointerFree && input().mouseDown(MouseButton::Left)));
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
    motorIn.wish            = canSteer ? wish : Vector3f{};
    motorIn.sprint          = canSteer && !jumpBusy && !uiKeys && input().actionDown("sprint");
    if (m_crouchLatch && input().actionPressed("crouch"))
        m_crouchLatch = false;
    motorIn.crouch          = canSteer && !jumpBusy && !uiKeys && (m_crouchLatch || input().actionDown("crouch"));
    motorIn.jumpPressed     = canSteer && !jumpBusy && !uiKeys && input().actionPressed("jump");
    motorIn.allowDoubleJump = !inAirCommit;
    motorIn.allowJumpBuffer = !jumpBusy;
    motorIn.airControlScale = inAirCommit && jump ? jump->def().airControlScale : 1.0f;
    const bool onGround = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch || motor->state() == PlayerMoveState::Dodge);
    const Terrain::GroundContact groundSurf = (onGround && xf) ? groundContactAt(xf->position.x, xf->position.z) : Terrain::GroundContact{};
    motorIn.speedScale      = (status ? status->moveSpeedScale() : 1.0f) * m_offhand.shield.speedScale() * (onGround ? groundSurf.moveSpeed : 1.0f);
    if (skills)
    {
        motorIn.runScale  = skillScalar(SkillId::Run, SkillScalar::RunScale, skills->level(SkillId::Run));
        motorIn.swimScale = skillScalar(SkillId::Swim, SkillScalar::SwimScale, skills->level(SkillId::Swim));
        motorIn.jumpScale = skillScalar(SkillId::Jump, SkillScalar::JumpScale, skills->level(SkillId::Jump));
    }
    motorIn.allowDodge      = canSteer && !jumpBusy && !uiKeys;
    if (motorIn.allowDodge)
    {
        motorIn.dodgeTap = moveCardinalFromEdges(
            input().keyPressed(Key::W) || input().keyPressed(Key::Up),
            input().keyPressed(Key::S) || input().keyPressed(Key::Down),
            input().keyPressed(Key::A) || input().keyPressed(Key::Left),
            input().keyPressed(Key::D) || input().keyPressed(Key::Right));
        motorIn.dodgeTapWish = moveCardinalWish(motorIn.dodgeTap, flat, right);
    }

    m_playGroundProbeY = xf->position.y;
    m_playGroundIgnore = m_physics.valid() ? m_physics.bodyOf(body) : Physics::kNullPhysicsBody;

    PlayerGroundQuery ground{};
    ground.user    = this;
    ground.waterY  = -1.0e9f;
    ground.heightAt = [](void* user, float x, float z) {
        return static_cast<const EditorApp*>(user)->playGroundHeight(x, z);
    };

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

    Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
    if (delta.MagnitudeSqrd() > 1.0e-10f)
    {
        auto livePhysics = [&](Entity solid) {
            const PhysicsBodyComponent* body = world().get<PhysicsBodyComponent>(solid);
            return body && body->valid && body->mode != PhysicsBodyMode::Static && m_physics.bodyValid(body->body);
        };
        Sphere3f ball{ Vector3f{ before.x, xf->position.y, before.z }, kPlayRadius };
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
        if (m_physics.valid())
        {
            // Origin sits groundOffset (0.5 m) above the feet. This capsule's bottom is just above the floor.
            Physics::PhysicsWorld::MoverCast cast;
            cast.origin      = Vector3f{ before.x, xf->position.y, before.z };
            cast.radius      = kPlayRadius;
            cast.bottom      = -kPlayRadius;
            cast.top         = 1.35f;
            cast.translation = delta;
            cast.ignore      = m_physics.bodyOf(body);
            cast.ignore2     = m_physicsGround;
            cast.staticOnly  = true;
            const float physicsT = m_physics.clipMover(cast);
            if (physicsT < bestT)
                bestT = physicsT;
        }
        if (bestT < 1.0f)
            delta *= Math::Max(0.0f, bestT - 0.02f);
        xf->position.x = before.x + delta.x;
        xf->position.z = before.z + delta.z;
        if (dt > 1.0e-4f && motor)
            motor->setHorizontalVelocity(delta.x / dt, delta.z / dt);
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
    if (TransformComponent* lightXf = m_playFlashlight.valid() ? world().get<TransformComponent>(m_playFlashlight) : nullptr)
    {
        placePlayerFlashlight(*lightXf, m_camera.GetPosition(), m_camera.GetLook(), m_camera.GetRight(), m_camera.GetUp());
        if (LocalLightComponent* light = world().get<LocalLightComponent>(m_playFlashlight))
        {
            light->enabled = m_offhand.lightOn;
            if (m_playFlashlightBaseId != m_playFlashlight.id())
            {
                m_playFlashlightBaseRange = light->range;
                m_playFlashlightBaseOuter = light->outerConeDeg;
                m_playFlashlightBaseId    = m_playFlashlight.id();
            }
            float seeRange = kSkillIdentity;
            float seeCone  = kSkillIdentity;
            if (const SkillComponent* sk = world().get<SkillComponent>(body))
            {
                const int seeLevel = sk->level(SkillId::See);
                seeRange = skillScalar(SkillId::See, SkillScalar::SeeRangeScale, seeLevel);
                seeCone  = skillScalar(SkillId::See, SkillScalar::SeeConeScale, seeLevel);
            }
            scaleFlashlight(m_playFlashlightBaseRange, m_playFlashlightBaseOuter, seeRange, seeCone, light->range, light->outerConeDeg);
        }
    }

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
            Vector3f hitDir{ xf->position.x - hxf->position.x, 0.0f, xf->position.z - hxf->position.z };
            if (hitDir.MagnitudeSqrd() > 1.0e-8f)
                hitDir.Normalize();
            else
                hitDir = Vector3f{ 0.0f, 0.0f, 1.0f };
            Combat::DamageEvent ev{};
            ev.source   = e;
            ev.target   = body;
            ev.amount   = kContactDps * dt;
            ev.type     = Combat::DamageType::Slash;
            ev.hitDir   = hitDir;
            ev.hitPoint = xf->position;
            ev.flags    = Combat::DamageFlags::CanBlock | Combat::DamageFlags::CanParry;
            m_combat.resolve(world(), ev);
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

    const bool attackPressed = input().actionPressed("attack") || (pointerFree && input().mousePressed(MouseButton::Left));
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
    const Terrain::HeightMap* height = nullptr;
    const Terrain::SplatMap*  splat  = nullptr;
    if (m_haveTerrain && m_splat.valid())
    {
        const Terrain::HeightMap* working = m_terrain.editableWorking();
        height = (working && working->valid()) ? working : &m_terrain.coarse();
        splat  = &m_splat;
    }
    return m_ground.at(height, splat, x, z);
}

void EditorApp::tickEditorHunters(float dt)
{
    if (m_sceneMode != SceneMode::Scene3D || dt <= 0.0f)
        return;
    Entity player = (m_playPlayer.valid() && world().alive(m_playPlayer)) ? m_playPlayer : findPlayPlayer();
    if (!player.valid())
        return;
    if (const PlayerMotorComponent* pmc = world().get<PlayerMotorComponent>(player))
    {
        const PlayerMotor& motor = pmc->motor;
        const float speed = Vector3f{ motor.velocity().x, 0.0f, motor.velocity().z }.Magnitude();
        const bool crouched = motor.state() == PlayerMoveState::Crouch;
        const bool moving = speed >= m_stealth.stillSpeed;
        const bool sprinting = moving && !crouched && input().actionDown("sprint");
        m_ai.setPreySense(preySenseFor(m_stealth, crouched, moving, sprinting));
    }
    if (!m_ai.walkability().valid())
        bakePlayWalkability();
    m_ai.setJumpAttackHits(&EditorApp::onPlayJumpHitsThunk, this);
    m_ai.setHunterCue(&EditorApp::onPlayHunterCueThunk, this);
    {
        const Terrain::HeightMap* height = nullptr;
        const Terrain::SplatMap*  splat  = nullptr;
        if (m_haveTerrain && m_splat.valid())
        {
            const Terrain::HeightMap* working = m_terrain.editableWorking();
            height = (working && working->valid()) ? working : &m_terrain.coarse();
            splat  = &m_splat;
        }
        m_ai.setGroundSurface(&m_ground, height, splat);
    }
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
