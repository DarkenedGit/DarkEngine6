#include "SandboxApp.h"

#include "Animation/AnimGraphComponent.h"
#include "Play/CharacterAttach.h"
#include "Play/SpawnHost.h"
#include "Save/PlayClock.h"

#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "Assets/Model.h"
#include "Audio/SoundComponents.h"
#include "Content/ArchetypeSpawn.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Gameplay/HealthPack.h"
#include "Gameplay/PowerSystem.h"
#include "Gameplay/QuestRuntime.h"
#include "Play/Carry.h"
#include "Play/DialogueFacts.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "Math/Vector3f.h"
#include "Network/Replication.h"
#include "Particles/StatusFxDriver.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsComponent.h"
#include "Save/HostSync.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include <cstring>
#include <format>
#include <string>

using namespace Dark;
using namespace Dark::Math;

void SandboxApp::installSaveHost()
{
    Save::SaveHost host{};
    host.id            = "sandbox";
    host.canSaveNow    = &SandboxApp::saveCanSave;
    host.beginLoad     = &SandboxApp::saveBeginLoad;
    host.endLoad       = &SandboxApp::saveEndLoad;
    host.respawn       = &SandboxApp::saveRespawn;
    host.destroyEntity = &SandboxApp::saveDestroy;
    host.skipEntity    = &SandboxApp::saveSkip;
    host.netRole       = &SandboxApp::saveNetRole;
    host.peerCount     = &SandboxApp::savePeerCount;
    host.beforeCapture = &SandboxApp::saveBeforeCapture;
    host.afterApply    = &SandboxApp::saveAfterApply;
    host.user          = this;
    m_save.setHost(host);
    // SaveSystem normalizes this to a portable, package-aware content id.
    const std::string scenePath = m_loadedScenePath.empty() ? std::string{} : m_loadedScenePath.generic_string();
    m_save.setWorldIdentity(scenePath, "", "sandbox");
}

void SandboxApp::ensureSessionEntity()
{
    m_play.session() = Save::ensureSessionEntity(world(), m_play.session());
    if (m_play.session().valid() && !world().get<DialogueFactsComponent>(m_play.session()))
        world().emplace<DialogueFactsComponent>(m_play.session());
    m_dialogue.runtime().setFacts(m_play.session().valid() ? world().get<DialogueFactsComponent>(m_play.session()) : nullptr);
    m_dialogue.speech().setServices(&assets(), &audio(), &world());
    m_dialogue.speech().startSynth();
    m_dialogue.runtime().setAudio(m_dialogue.speech().hook());
}

void SandboxApp::ensurePlayerProgress(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return;
    if (!world().has<LookComponent>(e))
        world().emplace<LookComponent>(e);
    if (!world().has<RunProgressComponent>(e))
        world().emplace<RunProgressComponent>(e);
    if (world().get<PersistentIdComponent>(e))
        return;
    const Dark::UUID want = makeProceduralId("sandbox/player");
    bool taken = false;
    world().each<PersistentIdComponent>([&](Entity, PersistentIdComponent& pid) {
        if (static_cast<uint64_t>(pid.id) == static_cast<uint64_t>(want))
            taken = true;
    });
    if (!taken)
        stampProceduralId(world(), e, "sandbox/player");
}

void SandboxApp::pullPhysicsVelocities()
{
    Save::pullDynamicVelocities(world(), m_sceneWorld.physics);
}

void SandboxApp::pushPhysicsProgress()
{
    Save::pushDynamicProgress(world(), m_sceneWorld.physics);
}

bool SandboxApp::saveCanSave(void* user, Save::SaveResult& why)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (app->m_hud.m_menu.visible() || app->m_play.runtime().deadTimer > 0.0f)
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (const Health* hp = app->localHealth(); hp && !hp->alive())
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (const PlayerMotor* motor = app->localMotor(); motor && motor->state() == PlayerMoveState::Dodge)
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    const Entity body = app->possessedBody();
    if (const JumpAttackComponent* jac = body.valid() ? app->world().get<JumpAttackComponent>(body) : nullptr)
    {
        if (jac->jump.phase() != Combat::JumpAttackPhase::Idle)
        {
            why = Save::SaveResult::UnsafeMoment;
            return false;
        }
    }
    why = Save::SaveResult::Ok;
    return true;
}

void SandboxApp::saveBeginLoad(void* user, World&)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    // The stream id must already be 0 before stopAll destroys voices.
    app->m_movie.close();
    app->audio().stopAll();
    app->m_sceneWorld.leafRustle.stopAll(app->audio());
    clearAllStatusFx(app->world(), &app->audio());
    if (app->m_play.session().valid())
        app->world().remove<QuestProgressComponent>(app->m_play.session());
    app->m_play.forgetFacilitySave(app->world(), app->m_play.session());
    app->m_play.runtime().attackCharge.reset();
    app->m_play.runtime().blockCharge.reset();
    app->m_play.runtime().chargeWindup = {};
    app->m_play.runtime().fireQuick = false;
    app->m_play.runtime().fireCharged = false;
    app->m_play.runtime().jumpAttackBuffer = 0.0f;
    app->m_play.runtime().offhand.reset();
    app->m_play.runtime().footstepAcc = 0.0f;
    app->m_play.runtime().hurtTimer = 0.0f;
    app->m_frame.prevWorldByEntity().clear();
    app->world().each<AnimGraphComponent>([](Entity, AnimGraphComponent& ag) { ag.prevWorldValid = false; });
    app->m_frame.scene().discardTemporalHistory();
}

void SandboxApp::saveEndLoad(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->pushPhysicsProgress();
    applyAuthoredMovement(world);
    playMusicCue(world, app->audio(), app->assets(), app->m_play.cameraEntity(), "music");
}

Entity SandboxApp::saveRespawn(void* user, World& world, std::string_view archetype, const Save::SavePose& pose)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return {};
    TransformComponent xf{};
    xf.position = pose.position;
    xf.rotation = pose.rotation;
    xf.scale    = pose.scale.x == 0.0f && pose.scale.y == 0.0f && pose.scale.z == 0.0f ? Vector3f{ 1.0f, 1.0f, 1.0f } : pose.scale;

    if (archetype == "healthpack")
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "HealthPack");
        world.emplace<TransformComponent>(e, xf);
        if (app->m_demo.packModelId() != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = app->m_demo.packModelId();
            mc.castShadow   = true;
            setModelComponent(world, app->pins(), app->assets(), e, mc);
        }
        HealthPackComponent pack{};
        pack.restPos = xf.position;
        pack.active  = true;
        world.emplace<HealthPackComponent>(e, pack);
        return e;
    }

    if (archetype == "tree")
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Tree");
        world.emplace<TransformComponent>(e, xf);
        AssetID modelId = NULL_ASSET;
        world.each<TagComponent>([&](Entity other, TagComponent& tag) {
            if (modelId != NULL_ASSET || other == e || tag.name != "Tree")
                return;
            if (const ModelComponent* mc = world.get<ModelComponent>(other))
                modelId = mc->modelAssetID;
        });
        if (modelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = modelId;
            mc.castShadow   = true;
            setModelComponent(world, app->pins(), app->assets(), e, mc);
        }
        return e;
    }

    SpawnServices services;
    services.world           = &world;
    services.assets          = &app->assets();
    services.pins            = &app->pins();
    services.renderer        = &app->renderer();
    services.audio           = &app->audio();
    services.ground          = &app->m_sceneWorld.ground;
    services.ai              = app->m_chaseOk ? const_cast<AiSystem*>(&app->m_chase.ai()) : nullptr;
    services.ensurePrimitive = &SandboxApp::ensureScenePrimitive;
    services.ensureMarker    = &SandboxApp::ensureSceneMarker;
    services.onWeaponHit     = &SandboxApp::onWeaponHitThunk;
    services.weaponHitUser   = app;
    services.user            = app;
    services.crows           = &app->m_play.crows();
    services.crowTerrain     = &app->m_sceneWorld.terrain;
    const ArchetypeSpawnHost host = makeSpawnHost(services);
    ArchetypeSpawnPose placed{};
    placed.position = xf.position;
    placed.rotation = xf.rotation;
    placed.scale    = xf.scale;
    const Entity e = respawnSavedArchetype(host, archetype, placed);
    if (!e.valid())
        return {};
    if (world.has<AiAgentComponent>(e))
    {
        if (world.has<PhysicsComponent>(e))
            app->bindEntityPhysics(e, "respawn");
        else
            DE_LOG_WARN(LogCategory::Collision, "Sandbox: respawn '{}' has no physics card", archetype);
        noteListedHunter(app, e);
    }
    else if (world.has<PhysicsComponent>(e) && app->m_sceneWorld.physics.valid())
    {
        const Model* model = nullptr;
        if (const ModelComponent* mc = world.get<ModelComponent>(e))
            model = app->assets().find<Model>(mc->modelAssetID);
        if (!Physics::bindPhysicsEntity(app->m_sceneWorld.physics, world, e, model))
            DE_LOG_WARN(LogCategory::Collision, "Sandbox: respawn physics bind failed for '{}' #{}", archetype, e.id());
    }
    return e;
}

void SandboxApp::saveDestroy(void* user, World& world, Entity e)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    Save::destroyProgressEntity(world, e, app->network(), app->pins());
}

bool SandboxApp::saveSkip(void* user, World& world, Entity e)
{
    (void)user;
    return Save::skipRemoteOwner(world, e);
}

int SandboxApp::saveNetRole(void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    return app ? Save::hostNetRole(app->network()) : 0;
}

uint32_t SandboxApp::savePeerCount(void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    return app ? Save::hostPeerCount(app->network()) : 0u;
}

void SandboxApp::saveBeforeCapture(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->pullPhysicsVelocities();
    const Entity body = app->possessedBody();
    Save::BodyLookState look;
    look.yaw          = app->m_play.runtime().lookYaw;
    look.pitch        = app->m_play.runtime().lookPitch;
    look.lightOn      = app->m_play.runtime().offhand.lightOn;
    look.lowerBodyYaw = app->m_play.runtime().lowerBodyYaw;
    Save::captureBodyLook(world, body, look);
    if (RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        run->deadTimer = app->m_play.runtime().deadTimer;
        run->spawnAge  = app->m_play.runtime().spawnAge;
        run->hasSpawn  = app->m_play.runtime().haveSpawn;
        run->spawn     = app->m_play.runtime().spawn;
    }
    if (WorldClockComponent* clock = Save::sessionClock(world, app->m_play.session()))
    {
        Save::SessionClockState clockState;
        clockState.cloudTime    = app->m_sceneWorld.cloudTime;
        clockState.envTimeOfDay = app->m_sceneWorld.env.timeOfDay;
        clockState.waterTime    = app->m_sceneWorld.water.time();
        Save::writeSessionClock(*clock, clockState);
    }
    if (AiClockComponent* ai = Save::sessionAi(world, app->m_play.session()))
    {
        ai->time          = app->m_chase.ai().simTime();
        ai->packAttackGap = app->m_chase.ai().packAttackGap();
        ai->token         = app->m_chase.ai().jumpAttackToken();
    }
    syncThrowableLoadout(world, body, app->m_play.runtime().throws, ThrowableLoadoutSync::OntoRecord);
    if (app->m_play.session().valid())
    {
        QuestProgressComponent* quest = world.get<QuestProgressComponent>(app->m_play.session());
        if (!quest)
            quest = &world.emplace<QuestProgressComponent>(app->m_play.session());
        writeQuestProgress(app->m_play.quest(), *quest);
    }
    app->m_play.captureFacilitySave(world, app->m_play.session());
}

void SandboxApp::saveAfterApply(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    if (app->m_play.session().valid() && !world.get<DialogueFactsComponent>(app->m_play.session()))
        world.emplace<DialogueFactsComponent>(app->m_play.session());
    app->m_dialogue.runtime().setFacts(app->m_play.session().valid() ? world.get<DialogueFactsComponent>(app->m_play.session()) : nullptr);
    const Entity body = app->possessedBody();
    Save::BodyLookState look;
    look.yaw          = app->m_play.runtime().lookYaw;
    look.pitch        = app->m_play.runtime().lookPitch;
    look.lightOn      = app->m_play.runtime().offhand.lightOn;
    look.lowerBodyYaw = app->m_play.runtime().lowerBodyYaw;
    Save::restoreBodyLook(world, body, look);
    app->m_play.runtime().lookYaw         = look.yaw;
    app->m_play.runtime().lookPitch       = look.pitch;
    app->m_play.runtime().offhand.lightOn = look.lightOn;
    app->m_play.runtime().lowerBodyYaw    = look.lowerBodyYaw;
    app->m_play.runtime().crouchLatch     = look.crouch;
    if (const RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        app->m_play.runtime().deadTimer = run->deadTimer;
        app->m_play.runtime().spawnAge        = run->spawnAge;
        app->m_play.runtime().haveSpawn = run->hasSpawn;
        app->m_play.runtime().spawn     = run->spawn;
    }
    if (const WorldClockComponent* clock = Save::sessionClock(world, app->m_play.session()))
    {
        Save::SessionClockState clockState;
        Save::readSessionClock(*clock, clockState);
        app->m_sceneWorld.cloudTime     = clockState.cloudTime;
        app->m_sceneWorld.env.timeOfDay = clockState.envTimeOfDay;
        app->m_sceneWorld.water.setTime(clockState.waterTime);
    }
    if (const AiClockComponent* ai = Save::sessionAi(world, app->m_play.session()))
    {
        app->m_chase.ai().setSimTime(ai->time);
        app->m_chase.ai().setPackAttackGap(ai->packAttackGap);
        app->m_chase.ai().setJumpAttackToken(ai->token);
    }
    app->pushPhysicsProgress();
    app->ensurePlayerStealth(body);
    if (const QuestProgressComponent* quest = app->m_play.session().valid() ? world.get<QuestProgressComponent>(app->m_play.session()) : nullptr)
    {
        if (!readQuestProgress(*quest, app->m_play.quest()))
            DE_LOG_WARN("Quest: saved progress for '{}' was not restored", quest->id);
    }
    syncThrowableLoadout(world, body, app->m_play.runtime().throws, ThrowableLoadoutSync::FromRecord);
    app->m_play.applyFacilitySave(world, app->m_play.session(), body);
    applyDisabledCircuits(world);
    syncCarriedAgents(world);
}
