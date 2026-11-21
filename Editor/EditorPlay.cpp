#include "EditorApp.h"

#include "Play/AttachedLights.h"
#include "Play/GuardVoice.h"
#include "Play/Carry.h"
#include "Play/DialogueBinding.h"
#include "Play/CombatHud.h"
#include "Play/DialogueHud.h"
#include "Ui/SimTimeHud.h"
#include "Ui/ImGuiGameChrome.h"
#include "Ui/ImGuiTheme.h"
#include "Play/DialogueMemory.h"
#include "Play/CharacterAttach.h"
#include "Scene/EntityCombat.h"
#include "Play/ContactGrind.h"
#include "Gameplay/PowerComponents.h"
#include "Media/MovieCue.h"
#include "Content/SceneObjectSpawn.h"
#include "Scene/SceneSpawn.h"
#include "Scene/WalkObstacles.h"
#include "Gameplay/PowerSystem.h"
#include "Play/HunterCue.h"
#include "Play/ThrowableHost.h"
#include "Play/HunterTick.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayFeedback.h"
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
#include "Character/ItemView.h"
#include "Gameplay/Item.h"
#include "Gameplay/Upgrades.h"
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
#include "Math/Vector4f.h"
#include "Render/DecalBasis.h"
#include "Render/GpuUpload.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/WeaponLoadout.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <imgui.h>

#include <algorithm>
#include <format>

#include <cmath>
#include <cstdio>
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

    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_sceneWorld.ground };
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

    ensureHumanoidBody(world(), e, archetypeHealthSettings("core:player"), Vector3f{ 0.35f, 0.7f, 0.35f });
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_sceneWorld.ground };
    ensureLocalPlayerRig(host, e, &EditorApp::onPlayWeaponHitThunk, this);

    const std::string playerGltf = archetypeModelPath("core:player");
    if (playerGltf.empty())
        DE_LOG_WARN(LogCategory::Core, "Play: 'core:player' has no Model path");
    else
        attachEditorModel(e, playerGltf.c_str());
    return true;
}

bool EditorApp::attachEditorNpc(Entity e, NpcKind kind, const char* proxyWarn)
{
    const char* archetypeId = kind == NpcKind::Wolf ? "core:wolf" : "core:hunter";
    assignCombatProfile(world(), e, archetypeId);
    if (!m_ai.attachHunter(world(), e, pins(), assets()))
        return false;
    ensureHumanoidBody(world(), e, archetypeHealthSettings(archetypeId), Vector3f{ 0.35f, 0.7f, 0.35f });
    const CharacterHost host{ world(), renderer(), assets(), pins(), audio(), m_sceneWorld.ground };
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
        if (sceneHasBehavior(so->source, "player.controller"))
            return m_selected;
    }
    Entity found{};
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (sceneHasBehavior(so.source, "player.controller") && !found.valid())
            found = e;
    });
    return found;
}

void EditorApp::resetPlayCombat()
{
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!editorPawn(so.source))
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
        if (sceneHasBehavior(so.source, "player.controller"))
            profileId = "player";
        else if (sceneHasBehavior(so.source, "ai.wolf"))
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
        if (!editorPawn(so.source))
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

const Terrain::HeightMap* EditorApp::gatherPlayWalkInputs(float& waterLevel)
{
    m_playCubes.clear();
    m_playSpheres.clear();
    m_playCubeEntities.clear();
    m_playSphereEntities.clear();
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        AABox3f     box;
        Sphere3f    sphere;
        std::string primitive;
        if (!sceneWalkObstacle(so.source, primitive))
            return;
        if (!classifyWalkObstacle(primitive, xf->position, xf->scale, sceneBlock(so.source), sceneGoal(so.source), sceneConceal(so.source), !sceneZone(so.source).empty(), !scenePickup(so.source).empty(), box, sphere))
            return;
        if (primitive == "cube")
        {
            m_playCubes.push_back(box);
            m_playCubeEntities.push_back(e);
        }
        else if (primitive == "sphere")
        {
            m_playSpheres.push_back(sphere);
            m_playSphereEntities.push_back(e);
        }
    });
    waterLevel = m_sceneWorld.water.chunksX() > 0 ? m_sceneWorld.water.params().waterLevel : -1.0e9f;
    if (m_haveTerrain && m_sceneWorld.terrain.valid() && m_sceneWorld.terrain.coarse().valid())
        return &m_sceneWorld.terrain.coarse();
    // The default editor ground is the Y=0 grid from -20 to 20. Bake that as a flat walk map.
    if (!m_playGrid.valid())
    {
        if (!m_playGrid.create(41, 41, 1.0f, 1.0f))
            return nullptr;
        m_playGrid.setOrigin(Vector3f{ -20.0f, 0.0f, -20.0f });
    }
    return &m_playGrid;
}

uint64_t EditorApp::walkInputsKey(const Terrain::HeightMap* height, float waterLevel) const
{
    // FNV-1a over everything the bake reads except the height samples; m_walkTerrainEpoch covers those.
    uint64_t   key = 1469598103934665603ull;
    const auto mix = [&key](const void* data, size_t bytes)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i)
            key = (key ^ p[i]) * 1099511628211ull;
    };
    const uintptr_t heightPtr = reinterpret_cast<uintptr_t>(height);
    mix(&heightPtr, sizeof(heightPtr));
    if (height)
    {
        const uint32_t dims[2] = { height->width(), height->height() };
        const float    cell    = height->cellSize();
        mix(dims, sizeof(dims));
        mix(&cell, sizeof(cell));
    }
    mix(&waterLevel, sizeof(waterLevel));
    mix(&m_walkTerrainEpoch, sizeof(m_walkTerrainEpoch));
    for (const AABox3f& box : m_playCubes)
    {
        const float v[6] = { box.Min.x, box.Min.y, box.Min.z, box.Max.x, box.Max.y, box.Max.z };
        mix(v, sizeof(v));
    }
    for (const Sphere3f& sphere : m_playSpheres)
    {
        const float v[4] = { sphere.Center.x, sphere.Center.y, sphere.Center.z, sphere.Radius };
        mix(v, sizeof(v));
    }
    return key == 0 ? 1 : key;
}

void EditorApp::bakePlayWalkability()
{
    float                     water  = -1.0e9f;
    const Terrain::HeightMap* height = gatherPlayWalkInputs(water);
    m_walkBakedKey                   = walkInputsKey(height, water);
    m_walkPendingKey                 = 0;
    if (!height)
    {
        DE_LOG_WARN(LogCategory::AI, "Editor: play without terrain — hunters will not path");
        m_ai.walkability().clear();
        return;
    }
    if (height == &m_playGrid)
        DE_LOG_INFO(LogCategory::AI, "Editor: no terrain — hunters path the ground grid");
    if (!bakeHunterWalk(m_ai, height, water, 0.8f, m_playCubes, m_playSpheres))
        DE_LOG_WARN(LogCategory::AI, "Editor: walkability bake failed");
}

void EditorApp::refreshWalkOverlay(float dt)
{
    constexpr float kPollSeconds   = 0.1f;
    constexpr float kSettleSeconds = 0.3f; // a gizmo drag or a slider rebakes once it stops
    if (!renderer().debugState().showWalkability || m_playMode || m_sceneMode != SceneMode::Scene3D)
    {
        m_walkPendingKey = 0;
        m_walkPollLeft   = 0.0f;
        return;
    }
    m_walkPollLeft -= dt;
    if (m_walkPendingKey != 0)
        m_walkPendingAge += dt;
    if (m_walkPollLeft > 0.0f)
        return;
    m_walkPollLeft = kPollSeconds;

    float                     water  = -1.0e9f;
    const Terrain::HeightMap* height = gatherPlayWalkInputs(water);
    const uint64_t            key    = walkInputsKey(height, water);
    if (key == m_walkBakedKey)
    {
        m_walkPendingKey = 0;
        return;
    }
    if (key != m_walkPendingKey)
    {
        m_walkPendingKey = key;
        m_walkPendingAge = 0.0f;
        return;
    }
    if (m_walkPendingAge >= kSettleSeconds)
        bakePlayWalkability();
}

void EditorApp::fillMovieHost(void* user, MovieHost& host)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    host.renderer = &app->renderer();
    host.audio    = &app->audio();
    host.assets   = &app->assets();
}

void EditorApp::fillDialogueHost(void* user, DialogueHost& host)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    host.world      = &app->world();
    host.assets     = &app->assets();
    host.audio      = &app->audio();
    host.guardVoice = &app->m_play.guardVoice();
    host.quest      = &app->m_play.quest();
    host.player     = app->m_playPlayer;
    host.session    = app->m_play.session();
    host.look       = app->m_camera.GetLook();
    host.cameraPos  = app->m_camera.GetPosition();
    host.enabled    = app->m_playMode;
    host.uiBlocked  = app->m_showUpgrades || app->m_lootTarget.valid();
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
    host.physics  = app->m_sceneWorld.physics.valid() ? &app->m_sceneWorld.physics : nullptr;
    host.ai       = &app->m_ai;
    host.camera   = &app->m_camera;
    host.input    = &app->input();
    host.terrain  = &app->m_sceneWorld.terrain;
    host.ground   = &app->m_sceneWorld.ground;
    host.groundBody = app->m_sceneWorld.physicsGround;
    host.guardVoice = &app->m_play.guardVoice();
    host.crows    = &app->m_play.crows();
    host.foliageCollision = &app->m_sceneWorld.foliageCollision;
    host.foliage  = app->m_sceneWorld.foliage.empty() ? nullptr : &app->m_sceneWorld.foliage;

    host.player  = app->m_playPlayer;
    host.shield  = app->m_play.shield();
    host.session = app->m_play.session();

    host.waterLevel   = app->m_sceneWorld.water.chunksX() > 0 ? app->m_sceneWorld.water.params().waterLevel : -1.0e9f;
    host.sunElevation = app->m_sceneWorld.env.sunElevation();
    host.sunDir       = app->m_sceneWorld.env.sunDir();
    host.cloudTime    = app->m_sceneWorld.cloudTime;

    host.groundProbeY = &app->m_playGroundProbeY;
    host.groundIgnore = &app->m_sceneWorld.groundIgnore;
    host.camouflage   = false;

    const bool playUi      = app->m_lootTarget.valid() || app->m_showUpgrades;
    const bool pointerFree = !playUi && (app->window().cursorCaptured() || !(app->m_imgui.isReady() && app->m_imgui.wantCaptureMouse()));
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
    host.onQuestNote   = &EditorApp::onPlayQuestNote;
    host.onDefenseText = &EditorApp::onPlayDefenseText;
    host.onLoot        = &EditorApp::onPlayLoot;
    host.stepPhysics   = &EditorApp::onPlayStepPhysics;
    host.spawnUnit     = &EditorApp::onPlaySpawnUnit;
    host.heightAt      = &EditorApp::onPlayHeight;
    host.groundContact = &EditorApp::onPlayGroundContact;
    host.user          = app;
    app->bindPlayFeedback();
    installPlayFeedback(host, &app->m_play.feedback());
}

void EditorApp::bindPlayFeedback()
{
    m_play.feedback().world       = &world();
    m_play.feedback().camera      = &m_camera;
    m_play.feedback().muzzle = &m_play.muzzle();
    const Terrain::HeightMap* height = nullptr;
    if (m_haveTerrain && m_sceneWorld.terrain.valid() && m_sceneWorld.terrain.coarse().valid())
        height = &m_sceneWorld.terrain.coarse();
    else if (m_playGrid.valid())
        height = &m_playGrid;
    m_play.feedback().height      = height;
    m_play.feedback().afterRespawn = nullptr;
    m_play.feedback().afterUser   = nullptr;
}

void EditorApp::onPlayQuestNote(void* user, const QuestNoteEvent& note)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    if (note.kind != QuestNote::NewObjective || app->m_questBanners.size() < 4)
        app->m_questBanners.push_back(makeQuestBanner(note));
}

void EditorApp::onPlayDefenseText(void* user, bool parried)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    app->m_defenseText     = parried ? "PARRY!" : "BLOCKED";
    app->m_defenseTextLeft = 0.7f;
    app->m_defenseParry    = parried;
}

void EditorApp::onPlayLoot(void* user, Entity loot)
{
    if (auto* app = static_cast<EditorApp*>(user))
        app->m_lootTarget = loot;
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
        applyAuthoredStealth();
        captureAuthoredPoses();
        // Same actor load as the game. The pose was stored first, so Stop puts the file position back.
        world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
            SceneObjectData authored = so.source;
            sceneSetString(authored, "dialogue", so.dialogue);
            applySceneActor(world(), e, authored, &EditorApp::sceneGroundY, this);
        });
        bakePlayWalkability();
        {
            std::vector<AABox3f> conceal;
            gatherConcealVolumes(world(), conceal);
            m_ai.setConcealVolumes(std::move(conceal));
        }
        m_play.setSimPaused(false);
        m_play.bind(&EditorApp::fillPlayHost, this);
        m_dialogue.bind(&EditorApp::fillDialogueHost, this);
        m_movie.bind(&EditorApp::fillMovieHost, this);
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
        m_questBanners.clear();
        m_play.quest().clear();
        if (!m_play.sceneQuest().empty())
            m_play.startQuest(m_play.sceneQuest());
        {
            GuardBarks  barks;
            std::string barkError;
            if (loadGuardBarks(resolveContentFile("ai/barks.json"), barks, &barkError))
                m_play.guardVoice().start(std::move(barks));
            else
                DE_LOG_WARN(LogCategory::AI, "Guard barks: {}", barkError);
        }
        // clearPlayArmor deletes loose pickups. Throwables are copied so they can come back. Quest pickups spawn after physics is live.
        const std::vector<ThrowablePickupSpot> authoredThrowables = captureThrowablePickups(world());
        ensurePlayGear();
        m_gizmoDragAxis    = EditorDetail::TranslateGizmoAxis::None;
        m_dragging         = false;
        if (renderer().scenePath() == ScenePath::HybridDeferred)
            ensurePlayMuzzle(world(), m_play.muzzle(), m_play.feedback().muzzleTimer);
        bindPlayFeedback();
        m_playMode = true;
        ensurePhysicsWorld();
        rebuildPhysicsGround(); // pick up a terrain loaded since the last Play
        if (m_sceneWorld.physics.valid())
            m_sceneWorld.physics.pushPoses(world(), true);
        spawnPlayPickups();
        respawnThrowablePickups(world(), pins(), assets(), renderer(), m_sceneWorld.physics, authoredThrowables);
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
            stockMissingThrowablePickups(world(), pins(), assets(), renderer(), m_sceneWorld.physics, start->position, haveRock, haveBottle);
        }
        window().setCursorCaptured(window().isFocused());
        ensurePlaySession();
        bindPlayDialogue();
        installSaveHost();
        ensureUpgradeCatalog();
        m_play.refreshUpgrades();
        if (!world().has<LookComponent>(m_playPlayer))
            world().emplace<LookComponent>(m_playPlayer);
        if (!world().has<RunProgressComponent>(m_playPlayer))
            world().emplace<RunProgressComponent>(m_playPlayer);
        m_save.captureBaseline(world());
        DE_LOG_INFO("Editor: PLAY — WASD move, mouse look, Space jump, hold LMB/F to charge an attack and release to swing, hold RMB/V to block, Ctrl/C crouch, E pickup, L flashlight, Escape or F12 stop");
    }
    else
    {
        Physics::destroyFoliageCollision(m_sceneWorld.physics, m_sceneWorld.foliageCollision);
        if (JumpAttackComponent* jac = m_playPlayer.valid() ? world().get<JumpAttackComponent>(m_playPlayer) : nullptr)
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
        destroySpawnedUnits({});
        restoreAuthoredPoses();
        resetPlayCombat();
        clearAllStatusFx(world(), &audio());
        if (AnimGraphComponent* ag = m_playPlayer.valid() ? world().get<AnimGraphComponent>(m_playPlayer) : nullptr)
        {
            ag->graph.player().setLowerBodyYaw(0.0f);
            ag->graph.player().setCornerLean(0.0f);
        }
        // clearPlayArmor deletes loose pickups. Copy throwables, including throws still in the air, and put them back.
        std::vector<ThrowablePickupSpot> keptThrowables = captureThrowablePickups(world());
        appendUnresolvedThrowableSpots(keptThrowables, m_play.throws());
        m_play.throws().clear();
        m_play.quest().clear();
        m_questBanners.clear();
        m_lootTarget = {};
        m_defenseText.clear();
        m_defenseTextLeft = 0.0f;
        m_defenseParry    = false;
        destroyPlayMuzzle(world(), m_play.muzzle(), m_play.feedback().muzzleTimer);
        m_play.setSimPaused(false);
        audio().setWorldHeld(false);
        m_play.guardVoice().stop();
        // Interrupt stores a resume bookmark. This Play is over, so the next one starts at the greeting.
        m_dialogue.endPlay();
        m_movie.close();
        resetMovieCues(world());
        m_play.resetPawn();
        destroyPlayGear();
        releasePlayPickups();
        m_showUpgrades = false;
        m_play.clearFacility();
        resetBuildingFixtures();
        respawnThrowablePickups(world(), pins(), assets(), renderer(), m_sceneWorld.physics, keptThrowables);
        clearThrowablePlayVisuals(world(), pins());
        m_playMode   = false;
        m_playPlayer = {};
        world().bumpGeneration();
        if (m_sceneWorld.physics.valid())
            m_sceneWorld.physics.pushPoses(world(), true);
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
    if (!m_play.shield().valid() || !world().alive(m_play.shield()))
        m_play.shield() = spawnPlayerShield(world(), pins(), assets(), renderer());
    m_play.stockPlayer(m_playPlayer);
    equipPlayArmor();
}

void EditorApp::destroyPlayGear()
{
    destroyPlayerShield(world(), pins(), m_play.shield());
    clearPlayerFlashlight(world(), m_playPlayer);
    clearPlayArmor();
}

void EditorApp::equipPlayArmor()
{
    clearPlayArmor();
    std::vector<Entity> hunters;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (sceneHasBehavior(so.source, "ai.hunter"))
            hunters.push_back(e);
    });
    for (Entity hunter : hunters)
    {
        m_play.stockHunter(hunter);
        if (const TransformComponent* xf = world().get<TransformComponent>(hunter))
            spawnGuardTorch(world(), hunter, xf->position);
    }
}

void EditorApp::clearPlayArmor()
{
    destroyGuardTorches(world());
    std::vector<Entity> owners;
    world().each<Combat::ArmorPiecesComponent>([&](Entity e, Combat::ArmorPiecesComponent&) { owners.push_back(e); });
    for (Entity owner : owners)
        removeArmor(world(), pins(), m_sceneWorld.physics, owner);

    std::vector<Entity> keep;
    world().each<EditorObjectComponent>([&](Entity, EditorObjectComponent& so) {
        if (so.playPickup.valid() && world().alive(so.playPickup))
            keep.push_back(so.playPickup);
    });
    std::vector<Entity> pickups;
    world().each<ItemPickupComponent>([&](Entity e, ItemPickupComponent&) { pickups.push_back(e); });
    for (Entity pickup : pickups)
    {
        bool authored = false;
        for (Entity kept : keep)
        {
            if (kept.id() == pickup.id())
            {
                authored = true;
                break;
            }
        }
        if (!authored)
            destroyPickup(world(), pins(), m_sceneWorld.physics, pickup);
    }
}

void EditorApp::spawnPlayPickups()
{
    if (!m_sceneWorld.physics.valid())
        return;
    world().each<EditorObjectComponent>([&](Entity marker, EditorObjectComponent& so) {
        if (so.pickup.empty() || so.playPickupSpent)
            return;
        if (so.playPickup.valid() && world().alive(so.playPickup))
            return;
        if (so.playPickup.valid())
        {
            so.playPickup      = {};
            so.playPickupSpent = true;
            return;
        }
        const ItemDef* def = tryFindItemDef(so.pickup);
        if (!def)
        {
            DE_LOG_WARN("Editor: unknown pickup item '{}'", so.pickup);
            return;
        }
        const TransformComponent* xf = world().get<TransformComponent>(marker);
        if (!xf)
            return;
        const int count = so.pickupCount > 0 ? so.pickupCount : 1;
        const Entity pickup = spawnItemPickup(world(), pins(), assets(), renderer(), m_sceneWorld.physics, ItemStack{ def, count }, xf->position, Vector3f{});
        if (pickup.valid())
            so.playPickup = pickup;
    });
}

void EditorApp::destroyBuildingFixtures()
{
    std::vector<Entity> doomed;
    world().each<SecurityCameraComponent>([&](Entity e, SecurityCameraComponent& cam) {
        // A searchlight is the placed spot. Leave it so Stop can restore the mount instead of deleting the light.
        if (!world().has<EditorObjectComponent>(e))
            doomed.push_back(e);
        if (cam.light.valid() && cam.light != e && !world().has<EditorObjectComponent>(cam.light))
            doomed.push_back(cam.light);
    });
    world().each<DoorComponent>([&](Entity e, DoorComponent&) { doomed.push_back(e); });
    world().each<HideSpotComponent>([&](Entity e, HideSpotComponent&) { doomed.push_back(e); });
    world().each<LootComponent>([&](Entity e, LootComponent&) {
        if (!world().has<EditorObjectComponent>(e))
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
}

void EditorApp::resetBuildingFixtures()
{
    destroyBuildingFixtures();
    m_play.buildingLabels().clear();
    BuildingFixtureSpawn fixtures;
    fixtures.world    = &world();
    fixtures.renderer = &renderer();
    fixtures.assets   = &assets();
    fixtures.pins     = &pins();
    fixtures.ai       = &m_ai;
    fixtures.labels   = &m_play.buildingLabels();
    fixtures.stampPrefix = "editor/scene";
    spawnBuildingFixtures(fixtures, m_play.buildingContainers(), m_play.buildingHides(), m_play.buildingDoors(), m_play.buildingCameras());
}

void EditorApp::ensureUpgradeCatalog()
{
    if (!m_play.facility().defs.empty())
        return;
    std::string upgradeError;
    if (!loadUpgrades(resolveContentFile("upgrades.json"), m_play.facility().defs, &upgradeError))
        DE_LOG_WARN("Upgrades: {}", upgradeError);
}

void EditorApp::drawUpgrades()
{
    if (!m_playMode || !m_showUpgrades || !m_imgui.isReady())
        return;
    InventoryComponent* inv = m_playPlayer.valid() ? world().get<InventoryComponent>(m_playPlayer) : nullptr;
    ImGui::SetNextWindowSize(ImVec2(uiPx(480.0f), uiPx(420.0f)), ImGuiCond_FirstUseEver);
    if (!Dark::BeginGameCard("Upgrades", &m_showUpgrades, Dark::UiAccent::Editor))
    {
        Dark::EndGameCard();
        return;
    }
    if (!inv)
    {
        ImGui::TextDisabled("No body to modify.");
        Dark::EndGameCard();
        return;
    }
    ImGui::TextWrapped("Spend stolen research and materials to modify yourself. Upgrades last for this Play.");
    ImGui::Text("You have: %d Research Data, %d Bio Material", heldCount(*inv, "research"), heldCount(*inv, "bio_material"));
    ImGui::Separator();
    FacilityState& facility = m_play.facility();
    for (const UpgradeDef& up : facility.defs)
    {
        ImGui::PushID(up.id.c_str());
        const bool owned = std::find(facility.owned.begin(), facility.owned.end(), up.id) != facility.owned.end();
        ImGui::TextColored(owned ? Dark::toImVec4(Dark::UiPalette::kOk) : Dark::toImVec4(Dark::UiPalette::kWarn), "%s%s", up.name.c_str(), owned ? "  (owned)" : "");
        ImGui::TextWrapped("%s", up.description.c_str());
        std::string cost;
        for (const UpgradeCost& c : up.cost)
        {
            const ItemDef* def = tryFindItemDef(c.item);
            if (!cost.empty())
                cost += ", ";
            cost += std::format("{} {} ({} held)", c.count, def ? def->name : c.item.c_str(), heldCount(*inv, c.item));
        }
        ImGui::TextDisabled("Cost: %s", cost.c_str());
        if (!owned)
        {
            const bool afford = canAfford(*inv, up);
            ImGui::BeginDisabled(!afford);
            if (ImGui::Button("Modify") && payFor(*inv, up))
            {
                facility.owned.push_back(up.id);
                m_play.refreshUpgrades();
                playSoundCue(world(), audio(), assets(), m_playPlayer, "click");
                DE_LOG_INFO("Upgrades: bought {}", up.name);
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    Dark::EndGameCard();
}

void EditorApp::releasePlayPickups()
{
    std::vector<Entity> markers;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!so.pickup.empty() || so.playPickup.valid() || so.playPickupSpent)
            markers.push_back(e);
    });
    for (Entity marker : markers)
    {
        EditorObjectComponent* so = world().get<EditorObjectComponent>(marker);
        if (!so)
            continue;
        if (so->playPickup.valid() && world().alive(so->playPickup))
            destroyPickup(world(), pins(), m_sceneWorld.physics, so->playPickup);
        so->playPickup      = {};
        so->playPickupSpent = false;
    }
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
    host.terrain     = &m_sceneWorld.terrain;
    host.ground      = &m_sceneWorld.ground;
    host.contactGrid = (m_haveTerrain && m_sceneWorld.terrain.valid()) ? &m_sceneWorld.terrain : nullptr;
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
    const Terrain::TerrainGrid* grid = (m_haveTerrain && m_sceneWorld.terrain.valid()) ? &m_sceneWorld.terrain : nullptr;
    return Terrain::groundContactAt(m_sceneWorld.ground, grid, x, z);
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

bool EditorApp::bindListedPhysics(void* user, Entity e, bool)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app || !app->world().has<ModelComponent>(e))
        return false;
    app->tryLoadModelPhysics(e);
    return true;
}

void EditorApp::proxyListedNpc(void* user, Entity e)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app || !e.valid())
        return;
    MeshComponent mc{};
    mc.matAssetID  = app->m_propMaterial ? app->m_propMaterial->id : NULL_ASSET;
    mc.meshAssetID = NULL_ASSET;
    setMeshComponent(app->world(), app->pins(), app->assets(), e, mc);
    DE_LOG_WARN("Editor: npc using cube proxy");
}

Entity EditorApp::spawnSpawnerUnit(Entity spawner, const SpawnerRequest& req)
{
    // No EditorObjectComponent and no persistent id. A play-mode scene save must not keep the army.
    ListedSpawnHost host{};
    host.world            = &world();
    host.renderer         = &renderer();
    host.assets           = &assets();
    host.pins             = &pins();
    host.audio            = &audio();
    host.ground           = &m_sceneWorld.ground;
    host.ai               = &m_ai;
    host.crows            = &m_play.crows();
    host.terrain          = (m_haveTerrain && m_sceneWorld.terrain.valid()) ? &m_sceneWorld.terrain : nullptr;
    host.play             = &m_play;
    host.allowStaticModel = true;
    host.bindPhysics      = &EditorApp::bindListedPhysics;
    host.onProxy          = &EditorApp::proxyListedNpc;
    host.user             = this;
    return spawnListedUnit(host, spawner, req);
}

void EditorApp::ensurePlayerStealth(Entity e)
{
    if (!e.valid() || !world().alive(e) || world().has<PlayerStealthComponent>(e))
        return;
    PlayerStealthComponent& stealth = world().emplace<PlayerStealthComponent>(e);
    applyScenePrey(stealth.settings, m_sceneWorld.stealth);
}

void EditorApp::applyAuthoredStealth()
{
    applySceneAwareness(m_ai, m_sceneWorld.stealth);
    const Entity player = (m_playPlayer.valid() && world().alive(m_playPlayer)) ? m_playPlayer : findPlayPlayer();
    if (!player.valid() || !world().alive(player))
        return;
    if (!world().has<PlayerStealthComponent>(player))
        world().emplace<PlayerStealthComponent>(player);
    PlayerStealthComponent* stealth = world().get<PlayerStealthComponent>(player);
    if (!stealth)
        return;
    applyScenePrey(stealth->settings, m_sceneWorld.stealth);
}



void EditorApp::bindPlayDialogue()
{
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!sceneHasBehavior(so.source, "ai.hunter"))
            return;
        std::string path = so.dialogue;
        if (path.empty())
            path = loadEntityMasterType("hunter").dialogue;
        DialogueBindingComponent* binding = world().get<DialogueBindingComponent>(e);
        if (path.empty())
        {
            if (binding)
                world().remove<DialogueBindingComponent>(e);
            return;
        }
        if (binding)
            binding->graph = std::move(path);
        else
        {
            DialogueBindingComponent created;
            created.graph = std::move(path);
            world().emplace<DialogueBindingComponent>(e, std::move(created));
        }
        if (DialogueMemoryComponent* memory = world().get<DialogueMemoryComponent>(e))
            *memory = {};
        else
            world().emplace<DialogueMemoryComponent>(e);
    });
}

void EditorApp::drawPlayDialogue()
{
    if (!m_playMode || !m_imgui.isReady())
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp)
        return;
    DialogueHudView view;
    view.viewProj       = m_camera.GetViewProj();
    view.list           = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp));
    view.projectWidth   = vp->Size.x;
    view.projectHeight  = vp->Size.y;
    view.windowName     = "PlayDialogue";
    const std::string talkPrompt = input().actions().keyBracket("talk", "[T]") + " Talk";
    view.talkPrompt     = talkPrompt.c_str();
    view.heading        = m_imgui.fonts().heading;
    view.accent         = UiAccent::Editor;
    drawDialogueHud(m_dialogue, view);
}

void EditorApp::drawPlayHud()
{
    if (!m_playMode || !m_imgui.isReady())
        return;

    if (!m_movie.blocksWorld())
    {
        const HealthComponent* hp = m_playPlayer.valid() ? world().get<HealthComponent>(m_playPlayer) : nullptr;
        if (hp)
            drawHealthBar(hp->health.ratio());
        if (hp && hp->health.alive() && !m_dialogue.locksWorld())
        {
            bool throwableSlot = false;
            if (const ThrowableBeltComponent* carried = world().get<ThrowableBeltComponent>(m_playPlayer))
                throwableSlot = carried->belt.any();
            WeaponKind kind = WeaponKind::Melee;
            if (const WeaponLoadout* weapons = playWeapons())
                kind = weapons->activeKind();
            drawCrosshair(kind, throwableSlot);
        }
    }

    if (m_defenseTextLeft > 0.0f && !m_defenseText.empty())
    {
        const float          a     = Math::Clamp(m_defenseTextLeft / 0.7f, 0.0f, 1.0f);
        const bool           parry = m_defenseParry;
        const ImGuiViewport* vp    = ImGui::GetMainViewport();
        ImDrawList*          dl    = vp ? ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp)) : nullptr;
        if (dl)
        {
            ImFont*      font = m_imgui.fonts().display ? m_imgui.fonts().display : (m_imgui.fonts().heading ? m_imgui.fonts().heading : ImGui::GetFont());
            const float  size = font->FontSize;
            const ImVec2 ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, m_defenseText.c_str());
            const ImVec2 pos{ vp->Pos.x + (vp->Size.x - ts.x) * 0.5f, vp->Pos.y + vp->Size.y * 0.36f - (1.0f - a) * 18.0f };
            const int    alpha = static_cast<int>(a * 255.0f);
            dl->AddText(font, size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(0, 0, 0, alpha * 3 / 4), m_defenseText.c_str());
            dl->AddText(font, size, pos, parry ? Dark::toImU32(Dark::UiPalette::kWarn, alpha) : Dark::toImU32(Dark::UiPalette::kInfo, alpha), m_defenseText.c_str());
        }
    }

    const ImGuiViewport* hudVp = ImGui::GetMainViewport();
    const float          toolbarH = ImGui::GetFrameHeight() + 4.0f;
    if (hudVp)
        ImGui::SetNextWindowPos(ImVec2(hudVp->WorkPos.x + uiPx(16.0f), hudVp->WorkPos.y + toolbarH + uiPx(8.0f)), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!Dark::BeginGameCard("##playhud", nullptr, Dark::UiAccent::Editor, flags))
    {
        Dark::EndGameCard();
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
        if (sceneHasBehavior(so.source, "ai.hunter"))
        {
            ++hunters;
            if (living)
                ++huntersAlive;
        }
        else if (sceneHasBehavior(so.source, "ai.wolf"))
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
    const QuestRuntime& quest = m_play.quest();
    if (quest.active())
    {
        ImGui::Separator();
        std::vector<Dark::QuestHudLine> lines;
        Dark::collectQuestLines(quest, lines);
        for (const Dark::QuestHudLine& line : lines)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(line.color), "%s", line.text.c_str());
    }
    if (!m_questBanners.empty())
    {
        const QuestBanner& banner = m_questBanners.front();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(banner.color), "%s  %s", banner.label.c_str(), banner.text.c_str());
    }
    ImGui::Separator();
    drawSimTimeControls(m_play, "Pause sim", nullptr, nullptr);
    ImGui::TextDisabled("RMB / V / LT shield    L light    F12 / Esc stop");
    Dark::EndGameCard();

    if (rt.climbOffer)
    {
        const ImGuiViewport* cvp = ImGui::GetMainViewport();
        ImDrawList*          cdl = cvp ? ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(cvp)) : nullptr;
        if (cdl)
        {
            const Vector4f clip = m_camera.GetViewProj() * Vector4f(rt.climbAt, 1.0f);
            if (clip.w > 1.0e-4f)
            {
                const float invW = 1.0f / clip.w;
                const float ndcZ = clip.z * invW;
                if (ndcZ >= 0.0f && ndcZ <= 1.0f)
                {
                    const std::string climb = input().actions().keyBracket("pickup", "[E]") + (rt.climbDown ? " Climb down" : " Climb");
                    const char*       text  = climb.c_str();
                    const float  sx   = (clip.x * invW * 0.5f + 0.5f) * cvp->Size.x;
                    const float  sy   = (1.0f - (clip.y * invW * 0.5f + 0.5f)) * cvp->Size.y;
                    const ImVec2 ts   = ImGui::CalcTextSize(text);
                    const ImVec2 pos{ cvp->Pos.x + sx - ts.x * 0.5f, cvp->Pos.y + sy - ts.y };
                    cdl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
                    cdl->AddText(pos, Dark::toImU32(Dark::UiPalette::kWarn), text);
                }
            }
        }
    }

    drawPlayDialogue();

    Vector3f target;
    const TransformComponent* pxf = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
    if (!pxf || !quest.active() || quest.complete() || !quest.objectiveTarget(world(), m_playPlayer, target))
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList*          dl = vp ? ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(vp)) : nullptr;
    if (vp)
        Dark::drawObjectiveDiamond(dl, vp->Pos, vp->Size, m_camera.GetViewProj(), target, pxf->position);
}

void EditorApp::drawLoot()
{
    if (!m_playMode || !m_lootTarget.valid() || !m_imgui.isReady())
        return;
    InventoryComponent*       inv  = m_playPlayer.valid() ? world().get<InventoryComponent>(m_playPlayer) : nullptr;
    const TransformComponent* pxf  = m_playPlayer.valid() ? world().get<TransformComponent>(m_playPlayer) : nullptr;
    LootComponent*            loot = world().alive(m_lootTarget) ? world().get<LootComponent>(m_lootTarget) : nullptr;
    const TransformComponent* lxf  = loot ? world().get<TransformComponent>(m_lootTarget) : nullptr;
    if (!inv || !pxf || !lxf || (lxf->position - pxf->position).MagnitudeSqrd() > 3.0f * 3.0f)
    {
        m_lootTarget = {};
        return;
    }

    bool        open  = true;
    const char* title = loot->label ? loot->label : "Loot";
    ImGui::SetNextWindowSize(ImVec2(uiPx(320.0f), uiPx(300.0f)), ImGuiCond_FirstUseEver);
    if (Dark::BeginGameCard(title, &open, Dark::UiAccent::Editor))
    {
        const bool takeAll = ImGui::Button("Take all");
        ImGui::Separator();
        for (int i = 0; i < InventoryComponent::kBagSize; ++i)
        {
            ItemStack& item = loot->contents.bag[i];
            if (item.empty())
                continue;
            ImGui::PushID(i);
            ImGui::Text("%s x%d (%s)", item.def->name, item.count, itemTypeName(item.def->type));
            ImGui::SameLine();
            if (ImGui::SmallButton("Take") || takeAll)
            {
                item.count -= grantInventoryItem(world(), audio(), assets(), m_playPlayer, *inv, item);
                if (item.count <= 0)
                    item = {};
            }
            ImGui::PopID();
        }
        if (itemCount(loot->contents) == 0)
            ImGui::TextUnformatted("Empty");
    }
    Dark::EndGameCard();

    settleLootTarget(world(), pins(), m_sceneWorld.physics, m_lootTarget, open);
}
