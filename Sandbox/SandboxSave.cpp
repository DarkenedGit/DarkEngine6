#include "SandboxApp.h"

#include "Play/CharacterAttach.h"
#include "Save/PlayClock.h"

#include "AI/AiSystem.h"
#include "Audio/SoundComponents.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Gameplay/HealthPack.h"
#include "Gameplay/PowerSystem.h"
#include "Play/Carry.h"
#include "Weapons/WeaponLoadoutComponent.h"
#include "Math/Vector3f.h"
#include "Network/Replication.h"
#include "Particles/StatusFxDriver.h"
#include "Physics/PhysicsBodyComponent.h"
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
    m_save.setWorldIdentity("", "", "sandbox");
}

void SandboxApp::ensureSessionEntity()
{
    m_session = Save::ensureSessionEntity(world(), m_session);
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
    Save::pullDynamicVelocities(world(), m_physics);
}

void SandboxApp::pushPhysicsProgress()
{
    Save::pushDynamicProgress(world(), m_physics);
}

bool SandboxApp::saveCanSave(void* user, Save::SaveResult& why)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (app->m_menu.visible() || app->m_playerDeadTimer > 0.0f)
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
    app->audio().stopAll();
    clearAllStatusFx(app->world(), &app->audio());
    app->m_attackCharge.reset();
    app->m_blockCharge.reset();
    app->m_chargeWindup = {};
    app->m_fireQuick = false;
    app->m_fireCharged = false;
    app->m_jumpAttackBuffer = 0.0f;
    app->m_offhand.reset();
    app->m_footstepAcc = 0.0f;
    app->m_hurtSoundTimer = 0.0f;
}

void SandboxApp::saveEndLoad(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    app->pushPhysicsProgress();
    applyAuthoredMovement(world);
    playMusicCue(world, app->audio(), app->assets(), app->m_camera, "music");
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

    if (archetype == "hunter" || archetype == "wolf")
    {
        Entity e = app->m_chase.ai().spawnHunter(world, app->pins(), app->assets(), xf);
        if (!e.valid())
            return {};
        const bool wolf = archetype == "wolf";
        const CharacterHost host{ world, app->renderer(), app->assets(), app->pins(), app->audio(), app->m_ground };
        attachNpcVisual(host, e, wolf ? NpcKind::Wolf : NpcKind::Hunter, ModelAttach::AnimatedOnly);
        app->bindEntityPhysics(e, wolf ? "wolf" : "human");
        app->m_chase.addHunter(e);
        return e;
    }

    if (archetype == "healthpack")
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "HealthPack");
        world.emplace<TransformComponent>(e, xf);
        if (app->m_packModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = app->m_packModelId;
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

    DE_LOG_WARN("Save: sandbox has no respawn for '{}'", archetype);
    return {};
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
    look.yaw          = app->m_lookYaw;
    look.pitch        = app->m_lookPitch;
    look.lightOn      = app->m_offhand.lightOn;
    look.lowerBodyYaw = app->m_lowerBodyYaw;
    Save::captureBodyLook(world, body, look);
    if (RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        run->deadTimer = app->m_playerDeadTimer;
        run->spawnAge  = app->m_spawnAge;
        run->hasSpawn  = app->m_havePlayerSpawn;
        run->spawn     = app->m_playerSpawn;
    }
    if (WorldClockComponent* clock = Save::sessionClock(world, app->m_session))
    {
        Save::SessionClockState clockState;
        clockState.cloudTime    = app->m_cloudTime;
        clockState.envTimeOfDay = app->m_env.timeOfDay;
        clockState.waterTime    = app->m_water.time();
        Save::writeSessionClock(*clock, clockState);
    }
    if (AiClockComponent* ai = Save::sessionAi(world, app->m_session))
    {
        ai->time          = app->m_chase.ai().simTime();
        ai->packAttackGap = app->m_chase.ai().packAttackGap();
        ai->token         = app->m_chase.ai().jumpAttackToken();
    }
    syncThrowableLoadout(world, body, app->m_throwFlights, ThrowableLoadoutSync::OntoRecord);
}

void SandboxApp::saveAfterApply(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    const Entity body = app->possessedBody();
    Save::BodyLookState look;
    look.yaw          = app->m_lookYaw;
    look.pitch        = app->m_lookPitch;
    look.lightOn      = app->m_offhand.lightOn;
    look.lowerBodyYaw = app->m_lowerBodyYaw;
    Save::restoreBodyLook(world, body, look);
    app->m_lookYaw         = look.yaw;
    app->m_lookPitch       = look.pitch;
    app->m_offhand.lightOn = look.lightOn;
    app->m_lowerBodyYaw    = look.lowerBodyYaw;
    app->m_crouchLatch     = look.crouch;
    if (const RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        app->m_playerDeadTimer = run->deadTimer;
        app->m_spawnAge        = run->spawnAge;
        app->m_havePlayerSpawn = run->hasSpawn;
        app->m_playerSpawn     = run->spawn;
    }
    if (const WorldClockComponent* clock = Save::sessionClock(world, app->m_session))
    {
        Save::SessionClockState clockState;
        Save::readSessionClock(*clock, clockState);
        app->m_cloudTime     = clockState.cloudTime;
        app->m_env.timeOfDay = clockState.envTimeOfDay;
        app->m_water.setTime(clockState.waterTime);
    }
    if (const AiClockComponent* ai = Save::sessionAi(world, app->m_session))
    {
        app->m_chase.ai().setSimTime(ai->time);
        app->m_chase.ai().setPackAttackGap(ai->packAttackGap);
        app->m_chase.ai().setJumpAttackToken(ai->token);
    }
    app->pushPhysicsProgress();
    syncThrowableLoadout(world, body, app->m_throwFlights, ThrowableLoadoutSync::FromRecord);
    applyDisabledCircuits(world);
    syncCarriedAgents(world);
}
