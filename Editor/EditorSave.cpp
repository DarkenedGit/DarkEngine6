#include "EditorApp.h"

#include "Animation/AnimGraphComponent.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Editor/EditorInternals.h"
#include "Network/Replication.h"
#include "Particles/StatusFxDriver.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Save/HostSync.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Scene/SceneTypes.h"

#include <format>
#include <string>

using namespace Dark;
using namespace Dark::EditorDetail;

namespace
{
    bool skipStampType(SceneObjectType type)
    {
        return isLocalLightType(type) || isGlobalLightType(type) || type == SceneObjectType::CloudVolume;
    }

    WorldClockComponent* sessionClock(World& world, Entity session)
    {
        return session.valid() ? world.get<WorldClockComponent>(session) : nullptr;
    }

    AiClockComponent* sessionAi(World& world, Entity session)
    {
        return session.valid() ? world.get<AiClockComponent>(session) : nullptr;
    }

    // Respawn omits EditorObjectComponent until the saved origin is known.
    void restoreAuthoredSceneObjects(World& world)
    {
        world.each<PersistentIdComponent>([&](Entity e, PersistentIdComponent& id) {
            if (id.origin != PersistOrigin::Authored || world.has<EditorObjectComponent>(e))
                return;
            SceneObjectType type{};
            if (!tryParseSceneObjectType(id.archetype, type))
                return;
            EditorObjectComponent so{};
            so.type     = type;
            so.color[0] = 0.8f;
            so.color[1] = 0.8f;
            so.color[2] = 0.8f;
            so.color[3] = 1.0f;
            world.emplace<EditorObjectComponent>(e, so);
        });
    }
}

void EditorApp::installSaveHost()
{
    Save::SaveHost host{};
    host.id            = "editor-pie";
    host.canSaveNow    = &EditorApp::saveCanSave;
    host.beginLoad     = &EditorApp::saveBeginLoad;
    host.endLoad       = &EditorApp::saveEndLoad;
    host.respawn       = &EditorApp::saveRespawn;
    host.destroyEntity = &EditorApp::saveDestroy;
    host.skipEntity    = &EditorApp::saveSkip;
    host.netRole       = &EditorApp::saveNetRole;
    host.peerCount     = &EditorApp::savePeerCount;
    host.beforeCapture = &EditorApp::saveBeforeCapture;
    host.afterApply    = &EditorApp::saveAfterApply;
    host.user          = this;
    m_save.setHost(host);
    m_save.setWorldIdentity(m_scenePath.generic_string(), "", "");
}

void EditorApp::ensurePlaySession()
{
    if (m_session.valid() && world().alive(m_session))
        return;
    m_session = world().createEntity();
    world().emplace<TagComponent>(m_session, "Session");
    stampProceduralId(world(), m_session, "session");
    world().emplace<WorldClockComponent>(m_session);
    world().emplace<AiClockComponent>(m_session);
}

void EditorApp::pullPhysicsVelocities()
{
    Save::pullDynamicVelocities(world(), m_physics);
}

void EditorApp::pushPhysicsProgress()
{
    Save::pushDynamicProgress(world(), m_physics);
}

bool EditorApp::saveCanSave(void* user, Save::SaveResult& why)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app || !app->m_playMode || !app->m_playPlayer.valid())
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (const HealthComponent* hp = app->world().get<HealthComponent>(app->m_playPlayer); hp && !hp->health.alive())
    {
        why = Save::SaveResult::UnsafeMoment;
        return false;
    }
    if (const PlayerMotorComponent* motor = app->world().get<PlayerMotorComponent>(app->m_playPlayer))
    {
        if (motor->motor.state() == PlayerMoveState::Dodge)
        {
            why = Save::SaveResult::UnsafeMoment;
            return false;
        }
    }
    if (const JumpAttackComponent* jac = app->world().get<JumpAttackComponent>(app->m_playPlayer))
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

void EditorApp::saveBeginLoad(void* user, World&)
{
    auto* app = static_cast<EditorApp*>(user);
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
    app->m_playFootstepAcc = 0.0f;
}

void EditorApp::saveEndLoad(void* user, World&)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    app->pushPhysicsProgress();
}

Entity EditorApp::saveRespawn(void* user, World&, std::string_view archetype, const Save::SavePose& pose)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return {};
    SceneObjectType type{};
    if (!tryParseSceneObjectType(archetype, type) || skipStampType(type))
    {
        DE_LOG_WARN("Save: editor has no respawn for '{}'", archetype);
        return {};
    }
    float col[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    const Entity previous = app->m_selected;
    const bool suspended = app->m_suspendLiveStamp;
    app->m_suspendLiveStamp = true;
    const Entity e = app->spawnObject(type, pose.position, pose.scale, pose.rotation, col, nullptr, nullptr, false, false);
    app->m_suspendLiveStamp = suspended;
    app->m_selected = previous;
    return e;
}

void EditorApp::saveDestroy(void* user, World& world, Entity e)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    Save::destroyProgressEntity(world, e, app->network(), app->pins());
}

bool EditorApp::saveSkip(void* user, World& world, Entity e)
{
    (void)user;
    return Save::skipRemoteOwner(world, e);
}

int EditorApp::saveNetRole(void* user)
{
    auto* app = static_cast<EditorApp*>(user);
    return app ? Save::hostNetRole(app->network()) : 0;
}

uint32_t EditorApp::savePeerCount(void* user)
{
    auto* app = static_cast<EditorApp*>(user);
    return app ? Save::hostPeerCount(app->network()) : 0u;
}

void EditorApp::saveBeforeCapture(void* user, World& world)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    app->m_save.setWorldIdentity(app->m_scenePath.generic_string(), "", "");
    app->pullPhysicsVelocities();
    const Entity body = app->m_playPlayer;
    if (LookComponent* look = body.valid() ? world.get<LookComponent>(body) : nullptr)
    {
        look->yaw     = app->m_playLookYaw;
        look->pitch   = app->m_playLookPitch;
        look->lightOn = app->m_offhand.lightOn;
    }
    if (AnimGraphComponent* ag = body.valid() ? world.get<AnimGraphComponent>(body) : nullptr)
        ag->graph.player().setLowerBodyYaw(app->m_playLowerBodyYaw);
    if (WorldClockComponent* clock = sessionClock(world, app->m_session))
    {
        clock->cloudTime    = app->m_cloudTime;
        clock->envTimeOfDay = app->m_env.timeOfDay;
        clock->waterTime    = app->m_water.time();
        clock->simTimeSec   = app->m_cloudTime;
    }
    if (AiClockComponent* ai = sessionAi(world, app->m_session))
    {
        ai->time          = app->m_ai.simTime();
        ai->packAttackGap = app->m_ai.packAttackGap();
        ai->token         = app->m_ai.jumpAttackToken();
    }
}

void EditorApp::saveAfterApply(void* user, World& world)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return;
    restoreAuthoredSceneObjects(world);
    const Entity body = app->m_playPlayer;
    if (const LookComponent* look = body.valid() ? world.get<LookComponent>(body) : nullptr)
    {
        app->m_playLookYaw   = look->yaw;
        app->m_playLookPitch = look->pitch;
        app->m_offhand.lightOn = look->lightOn;
    }
    app->m_crouchLatch = false;
    if (const PlayerMotorComponent* motor = body.valid() ? world.get<PlayerMotorComponent>(body) : nullptr)
        app->m_crouchLatch = motor->motor.state() == PlayerMoveState::Crouch;
    if (const AnimGraphComponent* ag = body.valid() ? world.get<AnimGraphComponent>(body) : nullptr)
        app->m_playLowerBodyYaw = ag->graph.player().lowerBodyYaw();
    if (const WorldClockComponent* clock = sessionClock(world, app->m_session))
    {
        app->m_cloudTime     = clock->cloudTime;
        app->m_env.timeOfDay = clock->envTimeOfDay;
        app->m_water.setTime(clock->waterTime);
    }
    if (const AiClockComponent* ai = sessionAi(world, app->m_session))
    {
        app->m_ai.setSimTime(ai->time);
        app->m_ai.setPackAttackGap(ai->packAttackGap);
        app->m_ai.setJumpAttackToken(ai->token);
    }
    app->pushPhysicsProgress();
}
