#include "SandboxApp.h"

#include "AI/AiSystem.h"
#include "Animation/AnimGraphComponent.h"
#include "Audio/SoundComponents.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/SkillXp.h"
#include "Combat/JumpAttackComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Gameplay/HealthPack.h"
#include "Math/Vector3f.h"
#include "Network/Replication.h"
#include "Particles/StatusFxDriver.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Save/HostSync.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Scene/EntityMaster.h"
#include "Weapons/HittableComponent.h"

#include <cstring>
#include <format>
#include <string>

using namespace Dark;
using namespace Dark::Math;

void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);

namespace
{
    WorldClockComponent* sessionClock(World& world, Entity session)
    {
        return session.valid() ? world.get<WorldClockComponent>(session) : nullptr;
    }

    AiClockComponent* sessionAi(World& world, Entity session)
    {
        return session.valid() ? world.get<AiClockComponent>(session) : nullptr;
    }
}

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
    if (m_session.valid() && world().alive(m_session))
        return;
    m_session = world().createEntity();
    world().emplace<TagComponent>(m_session, "Session");
    stampProceduralId(world(), m_session, "session");
    world().emplace<WorldClockComponent>(m_session);
    world().emplace<AiClockComponent>(m_session);
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
        std::string wolfGltf;
        const char* gltf = "models/skeleton.gltf";
        if (wolf)
        {
            const EntityMaster wolfMaster = loadEntityMasterType("wolf");
            wolfGltf = wolfMaster.gltf.empty() ? std::string("models/wolf.gltf") : wolfMaster.gltf;
            gltf     = wolfGltf.c_str();
        }
        app->attachAnimatedCharacter(e, gltf);
        attachHunterSounds(world, app->pins(), app->assets(), app->audio(), e, app->m_ground);
        if (HittableComponent* hit = world.get<HittableComponent>(e))
            hit->halfExtents = wolf ? Vector3f{ 0.45f, 0.45f, 0.70f } : Vector3f{ 0.4f, 0.7f, 0.4f };
        if (wolf)
        {
            if (TagComponent* tag = world.get<TagComponent>(e))
                tag->name = "Wolf";
            applySkillProfile(world, e, "wolf");
        }
        app->bindEntityPhysics(e, wolf ? "wolf" : "human");
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
    if (LookComponent* look = body.valid() ? world.get<LookComponent>(body) : nullptr)
    {
        look->yaw     = app->m_lookYaw;
        look->pitch   = app->m_lookPitch;
        look->lightOn = app->m_offhand.lightOn;
    }
    if (RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        run->deadTimer = app->m_playerDeadTimer;
        run->spawnAge  = app->m_spawnAge;
        run->hasSpawn  = app->m_havePlayerSpawn;
        run->spawn     = app->m_playerSpawn;
    }
    if (AnimGraphComponent* ag = body.valid() ? world.get<AnimGraphComponent>(body) : nullptr)
        ag->graph.player().setLowerBodyYaw(app->m_lowerBodyYaw);
    if (WorldClockComponent* clock = sessionClock(world, app->m_session))
    {
        clock->cloudTime    = app->m_cloudTime;
        clock->envTimeOfDay = app->m_env.timeOfDay;
        clock->waterTime    = app->m_water.time();
        clock->simTimeSec   = app->m_cloudTime;
    }
    if (AiClockComponent* ai = sessionAi(world, app->m_session))
    {
        ai->time          = app->m_chase.ai().simTime();
        ai->packAttackGap = app->m_chase.ai().packAttackGap();
        ai->token         = app->m_chase.ai().jumpAttackToken();
    }
}

void SandboxApp::saveAfterApply(void* user, World& world)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    const Entity body = app->possessedBody();
    if (const LookComponent* look = body.valid() ? world.get<LookComponent>(body) : nullptr)
    {
        app->m_lookYaw        = look->yaw;
        app->m_lookPitch      = look->pitch;
        app->m_offhand.lightOn = look->lightOn;
    }
    if (const RunProgressComponent* run = body.valid() ? world.get<RunProgressComponent>(body) : nullptr)
    {
        app->m_playerDeadTimer = run->deadTimer;
        app->m_spawnAge        = run->spawnAge;
        app->m_havePlayerSpawn = run->hasSpawn;
        app->m_playerSpawn     = run->spawn;
    }
    app->m_crouchLatch = false;
    if (const PlayerMotor* motor = app->localMotor())
        app->m_crouchLatch = motor->state() == PlayerMoveState::Crouch;
    if (const AnimGraphComponent* ag = body.valid() ? world.get<AnimGraphComponent>(body) : nullptr)
        app->m_lowerBodyYaw = ag->graph.player().lowerBodyYaw();
    if (const WorldClockComponent* clock = sessionClock(world, app->m_session))
    {
        app->m_cloudTime     = clock->cloudTime;
        app->m_env.timeOfDay = clock->envTimeOfDay;
        app->m_water.setTime(clock->waterTime);
    }
    if (const AiClockComponent* ai = sessionAi(world, app->m_session))
    {
        app->m_chase.ai().setSimTime(ai->time);
        app->m_chase.ai().setPackAttackGap(ai->packAttackGap);
        app->m_chase.ai().setJumpAttackToken(ai->token);
    }
    app->pushPhysicsProgress();
}
