#include "Play/PlaySession.h"

#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "AI/HsmGraphComponent.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/Locomotion.h"
#include "Audio/SoundComponents.h"
#include "Character/ArmorView.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Character/ItemView.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/ShieldView.h"
#include "Character/SkillComponent.h"
#include "Character/SkillXp.h"
#include "Character/SwordView.h"
#include "Character/ThrowableView.h"
#include "Collision/Collision.h"
#include "Collision/StaticCollision.h"
#include "Combat/ArmorComponent.h"
#include "Combat/ArmorPieces.h"
#include "Combat/DefenseComponent.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusDot.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Gameplay/HealthPack.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/LootMark.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/PowerSystem.h"
#include "Gameplay/Quest.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Input/Input.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Particles/StatusFxDriver.h"
#include "Physics/FoliageCollision.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Play/AttachedLights.h"
#include "Play/Carry.h"
#include "Play/ContactGrind.h"
#include "Play/Crow.h"
#include "Play/GuardVoice.h"
#include "Play/HunterCue.h"
#include "Play/HunterTick.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayerFlashlight.h"
#include "Play/PlayLook.h"
#include "Play/PlayMotor.h"
#include "Play/ShoulderCamera.h"
#include "Play/Spawner.h"
#include "Play/ThrowableHost.h"
#include "Play/WeaponFire.h"
#include "Render/Camera3D.h"
#include "Render/Underwater.h"
#include "Save/ProgressComponents.h"
#include "Scene/EntityMaster.h"
#include "Terrain/TerrainGrid.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <cmath>
#include <cstring>

namespace Dark
{
    namespace
    {
        bool playSoundCueFallback(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* primary, const char* fallback)
        {
            if (playSoundCue(world, audio, assets, e, primary))
                return true;
            return fallback && playSoundCue(world, audio, assets, e, fallback);
        }

        Entity listenerOf(const PlayHost& host)
        {
            return host.cameraEntity.valid() ? host.cameraEntity : host.player;
        }
    } // namespace

    int grantInventoryItem(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity listener, InventoryComponent& inv, const ItemStack& item)
    {
        if (!item.def || item.count <= 0)
            return 0;
        const int added = addItem(inv, *item.def, item.count);
        if (added <= 0)
        {
            DE_LOG_INFO("Inventory: bag full");
            return 0;
        }
        DE_LOG_INFO("Inventory: picked up {} x{}", item.def->name, added);
        playSoundCue(world, audio, assets, listener, "click");
        const bool wearable = item.def->slot != EquipSlot::None && (item.def->type == ItemType::Armor || item.def->type == ItemType::Tool);
        if (wearable && inv.equipped[static_cast<int>(item.def->slot)].empty())
        {
            for (int i = 0; i < InventoryComponent::kBagSize; ++i)
            {
                if (inv.bag[i].def == item.def && equipItem(inv, i))
                {
                    DE_LOG_INFO("Inventory: wearing {}", item.def->name);
                    break;
                }
            }
        }
        return added;
    }

    void PlaySession::bind(void (*fill)(void* user, PlayHost& host), void* user)
    {
        m_fill = fill;
        m_user = user;
    }

    void PlaySession::pull()
    {
        m_host = {};
        if (m_fill)
            m_fill(m_user, m_host);
    }

    void PlaySession::resetPawn()
    {
        PlayRuntime& r = m_rt;
        r.crouchLatch       = false;
        r.footstepAcc       = 0.0f;
        r.jumpAttackBuffer  = 0.0f;
        r.swingLock         = 0.0f;
        r.comboWindow       = 0.0f;
        r.blockRecoil       = 0.0f;
        r.faceYaw           = 0.0f;
        r.faceLeft          = 0.0f;
        r.shieldBump        = 0.0f;
        r.comboStep         = 0;
        r.comboCount        = 0;
        r.swingCharged      = false;
        r.fireQuick         = false;
        r.fireCharged       = false;
        r.wet               = false;
        r.submerged         = false;
        r.swimDiveEdge      = false;
        r.swimStandEdge     = false;
        r.deadTimer         = 0.0f;
        r.spawnAge          = 0.0f;
        r.hurtTimer         = 0.0f;
        r.shake             = 0.0f;
        r.shakeTime         = 0.0f;
        r.hitStop           = 0.0f;
        r.underAmount       = 0.0f;
        r.groundProbeY      = 0.0f;
        r.haveSpawn         = false;
        r.spawn             = {};
        r.lowerBodyYaw      = 0.0f;
        r.offhand.reset();
        r.attackCharge.reset();
        r.blockCharge.reset();
        r.chargeWindup = {};
        for (const ItemDef*& worn : r.wornArmor)
            worn = nullptr;
    }

    void PlaySession::startQuest(std::string_view id)
    {
        if (id.empty())
            return;
        QuestDef    def;
        std::string error;
        if (!loadQuestFile(resolveContentFile(questVirtualPath(id)), def, &error))
        {
            DE_LOG_ERROR("Quest: could not load '{}': {}", id, error);
            return;
        }
        DE_LOG_INFO("Quest: started '{}' ({} objectives)", def.title, def.objectives.size());
        m_rt.quest.start(std::move(def));
    }

    float PlaySession::scaleDt(float dt)
    {
        if (m_rt.shake > 0.0f)
        {
            m_rt.shakeTime += dt;
            m_rt.shake = Math::Max(0.0f, m_rt.shake - dt * 0.5f);
        }
        if (m_rt.hitStop > 0.0f)
        {
            m_rt.hitStop -= dt;
            dt *= 0.05f;
        }
        return dt;
    }

    ImpactFxHost PlaySession::impact()
    {
        ImpactFxHost host;
        host.world       = m_host.world;
        host.scene       = m_host.scene;
        host.renderer    = m_host.renderer;
        host.terrain     = m_host.terrain;
        host.ground      = m_host.ground;
        host.contactGrid = (m_host.terrain && m_host.terrain->valid()) ? m_host.terrain : nullptr;
        host.targets     = &m_rt.targets;
        return host;
    }

    bool PlaySession::boneWorld(Entity e, const char* bone, Math::Vector3f& out) const
    {
        World* world = m_host.world;
        if (!world || !bone)
            return false;
        const TransformComponent* xf = world->get<TransformComponent>(e);
        const AnimGraphComponent* ag = world->get<AnimGraphComponent>(e);
        const Skeleton*           sk = ag ? ag->graph.skeleton() : nullptr;
        if (!xf || !sk)
            return false;
        const AnimPose& pose = ag->graph.player().pose();
        for (size_t j = 0; j < sk->joints.size() && j < pose.boneCount; ++j)
        {
            if (sk->joints[j].name == bone)
            {
                out = (pose.jointWorld[j] * worldMatrix(*xf)).GetTranslation();
                return true;
            }
        }
        return false;
    }

    float PlaySession::terrainHeight(float x, float z) const
    {
        if (m_host.terrain && m_host.terrain->valid())
            return m_host.terrain->heightAtWorld(x, z);
        if (m_host.heightAt)
            return m_host.heightAt(m_host.user, x, z);
        return 0.0f;
    }

    Collision::RayHit3D PlaySession::onTerrainRay(void* user, const Math::Ray3f& ray, float maxDistance)
    {
        auto* self = static_cast<PlaySession*>(user);
        if (!self || !self->m_host.terrain)
            return {};
        return self->m_host.terrain->raycast(ray, maxDistance);
    }

    float PlaySession::onTerrainHeight(void* user, float x, float z)
    {
        auto* self = static_cast<PlaySession*>(user);
        return self ? self->terrainHeight(x, z) : 0.0f;
    }

    float PlaySession::onCarryHeight(void* user, float x, float z)
    {
        return onTerrainHeight(user, x, z);
    }

    void PlaySession::onCarryHidden(void* user)
    {
        auto* self = static_cast<PlaySession*>(user);
        if (self)
            self->m_rt.quest.event("body_hidden");
    }

    void PlaySession::onPowerClick(void* user)
    {
        auto* self = static_cast<PlaySession*>(user);
        if (!self || !self->m_host.world || !self->m_host.audio || !self->m_host.assets)
            return;
        playSoundCue(*self->m_host.world, *self->m_host.audio, *self->m_host.assets, listenerOf(self->m_host), "click");
    }

    void PlaySession::onJumpHits(void* user, const Combat::DamageEvent* events, int count)
    {
        if (auto* self = static_cast<PlaySession*>(user))
            self->resolveJump(events, count);
    }

    void PlaySession::onHunterCue(void* user, Entity hunter, const char* cue)
    {
        auto* self = static_cast<PlaySession*>(user);
        if (!self || !self->m_host.world || !self->m_host.audio || !self->m_host.assets)
            return;
        playHunterCue(
            *self->m_host.world, *self->m_host.audio, *self->m_host.assets, hunter, cue,
            [](void* stepUser, Entity e, const char* stepCue) { return impactIsStepCue(static_cast<PlaySession*>(stepUser)->impact(), e, stepCue); },
            [](void* markUser, Entity e) { spawnHunterFootmark(static_cast<PlaySession*>(markUser)->impact(), e); }, self);
    }

    void PlaySession::faceTarget(Entity target)
    {
        World* world = m_host.world;
        if (!world)
            return;
        const TransformComponent* xf  = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        const TransformComponent* txf = world->get<TransformComponent>(target);
        if (!xf || !txf)
            return;
        const float dx = txf->position.x - xf->position.x;
        const float dz = txf->position.z - xf->position.z;
        if (dx * dx + dz * dz < 1.0e-4f)
            return;
        m_rt.faceYaw  = std::atan2f(dx, dz);
        m_rt.faceLeft = 0.15f;
    }

    void PlaySession::onDefended(Entity attacker, bool parried)
    {
        World* world = m_host.world;
        if (!world || !m_host.audio || !m_host.assets)
            return;
        faceTarget(attacker);
        m_rt.shieldBump = 0.2f;
        m_rt.hitStop    = parried ? 0.1f : 0.05f;
        m_rt.shake      = parried ? 0.09f : 0.06f;
        if (m_host.onDefenseText)
            m_host.onDefenseText(m_host.user, parried);
        playSoundCue(*world, *m_host.audio, *m_host.assets, m_host.player, "impact");
        if (!parried || !attacker.valid() || !m_host.ai || !world->has<AiAgentComponent>(attacker))
            return;
        const TransformComponent* axf = world->get<TransformComponent>(attacker);
        const TransformComponent* pxf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        m_host.ai->interruptAttack(*world, attacker, 1.2f);
        if (axf && pxf)
            m_host.ai->applyHunterHitReaction(*world, attacker, Math::Vector3f{ axf->position.x - pxf->position.x, 0.0f, axf->position.z - pxf->position.z });
        if (AnimGraphComponent* aag = world->get<AnimGraphComponent>(attacker))
            aag->graph.setTrigger("hit");
    }

    bool PlaySession::ownsWeapon(WeaponKind kind) const
    {
        World* world = m_host.world;
        if (!world || !m_host.player.valid())
            return kind != WeaponKind::Throwable;
        if (kind == WeaponKind::Throwable)
        {
            const ThrowableBeltComponent* belt = world->get<ThrowableBeltComponent>(m_host.player);
            return belt && belt->belt.any();
        }
        const InventoryComponent* inv = world->get<InventoryComponent>(m_host.player);
        const char*               id  = kind == WeaponKind::Melee ? "sword" : "rifle";
        return !inv || hasItem(*inv, id);
    }

    WeaponWorldQuery PlaySession::weaponQuery()
    {
        pull();
        WeaponWorldQuery q{};
        if (!m_host.world)
            return q;
        m_rt.targets.gather(*m_host.world, m_host.player);
        q.terrainUser    = this;
        q.raycastTerrain = &PlaySession::onTerrainRay;
        q.heightAt       = &PlaySession::onTerrainHeight;
        m_rt.targets.bind(q);
        q.maxRange = 90.0f;
        return q;
    }

    void PlaySession::tickThrows(float dt)
    {
        if (!m_host.world)
            return;
        ThrowableFlightHost host;
        host.world    = m_host.world;
        host.pins     = m_host.pins;
        host.assets   = m_host.assets;
        host.renderer = m_host.renderer;
        host.physics  = m_host.physics;
        host.audio    = m_host.audio;
        host.ai       = m_host.ai;
        host.solids.hasTerrain     = m_host.terrain != nullptr;
        host.solids.terrainUser    = this;
        host.solids.raycastTerrain = &PlaySession::onTerrainRay;
        host.solids.heightAt       = &PlaySession::onTerrainHeight;
        host.solids.cubes          = m_host.cubeCount > 0 ? m_host.cubes : nullptr;
        host.solids.cubeCount      = host.solids.cubes ? m_host.cubeCount : 0;
        host.solids.spheres        = m_host.sphereCount > 0 ? m_host.spheres : nullptr;
        host.solids.sphereCount    = host.solids.spheres ? m_host.sphereCount : 0;
        tickThrowableFlightHost(m_rt.throws, dt, host);
    }

    bool PlaySession::releaseThrow(bool charged)
    {
        World* world = m_host.world;
        if (!world || !m_host.camera || !m_host.player.valid())
            return false;
        WeaponLoadoutComponent* wlc = world->get<WeaponLoadoutComponent>(m_host.player);
        WeaponLoadout*          weapons = (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
        if (!weapons || weapons->activeKind() != WeaponKind::Throwable)
            return false;
        ThrowableBelt&            belt = throwableBelt(*world, m_host.player);
        const TransformComponent* xf   = world->get<TransformComponent>(m_host.player);
        const WeaponFireRequest   req  = makeCameraFireRequest(m_host.camera->GetPosition(), m_host.camera->GetLook(), xf ? &xf->position : nullptr, 1.0f);
        std::string               thrown;
        if (!throwFromLoadout(m_rt.throws, *weapons, belt, req, m_host.player, charged, &thrown))
            return false;
        wlc->slot = weapons->slot();
        DE_LOG_INFO("Player: threw {}", thrown);
        return true;
    }

    bool PlaySession::fireLoadout(bool charged)
    {
        World* world = m_host.world;
        if (!world || !m_host.camera || !m_host.audio || !m_host.assets)
            return false;
        const Entity              body = m_host.player;
        const TransformComponent* xf   = body.valid() ? world->get<TransformComponent>(body) : nullptr;
        WeaponFireRequest req = makeCameraFireRequest(m_host.camera->GetPosition(), m_host.camera->GetLook(), xf ? &xf->position : nullptr,
                                                      charged ? m_rt.chargeSettings.attackDamageScale : 1.0f);
        WeaponLoadoutComponent* wlc   = body.valid() ? world->get<WeaponLoadoutComponent>(body) : nullptr;
        WeaponLoadout*          wFire = (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
        if (wFire && wFire->activeKind() == WeaponKind::Throwable)
            return false;

        SkillComponent* skill = nullptr;
        if (wFire && wFire->activeKind() == WeaponKind::Projectile && body.valid())
            skill = world->get<SkillComponent>(body);
        applyShootSkill(req, skill);

        const bool melee = wFire && wFire->activeKind() == WeaponKind::Melee;
        const bool rifle = wFire && wFire->activeKind() == WeaponKind::Projectile;
        if ((melee || rifle) && Combat::hitVolumesBlockFire(body.valid() ? world->get<Combat::HitVolumesComponent>(body) : nullptr))
            return false;
        if (melee && m_rt.blockRecoil > 0.0f)
            return false;

        const WeaponWorldQuery query = weaponQuery();
        if (!wFire || !ownsWeapon(wFire->activeKind()) || !wFire->fire(req, query))
            return false;
        if (skill)
            noteShotXp(*skill, true);
        if (melee)
        {
            m_rt.comboStep   = m_rt.comboWindow > 0.0f ? 1 - m_rt.comboStep : 0;
            m_rt.comboCount  = m_rt.comboWindow > 0.0f ? m_rt.comboCount + 1 : 1;
            m_rt.comboWindow = 0.9f;
            m_rt.swingLock   = 0.45f;
        }
        if (AnimGraphComponent* ag = body.valid() ? world->get<AnimGraphComponent>(body) : nullptr)
        {
            if (melee)
                ag->graph.setTrigger(m_rt.comboStep ? "swing2" : "swing");
            else if (wFire->activeKind() == WeaponKind::Projectile)
                ag->graph.setTrigger("shoot");
        }
        if (melee)
        {
            m_rt.swingCharged = charged;
            playSoundCue(*world, *m_host.audio, *m_host.assets, body, "swing");
        }
        else if (wFire && wFire->activeKind() == WeaponKind::Projectile)
        {
            if (m_host.onMuzzle)
                m_host.onMuzzle(m_host.user);
            const RecoilKick kick = wFire->projectile().takeRecoil();
            m_rt.lookYaw += kick.yaw;
            m_rt.lookPitch += kick.pitch;
            m_rt.lookYaw   = Math::WrapPi(m_rt.lookYaw);
            m_rt.lookPitch = Math::Clamp(m_rt.lookPitch, -0.96f, 0.96f);
            tickCamera();
        }
        return true;
    }

    void PlaySession::respawn()
    {
        World*       world = m_host.world;
        const Entity body  = m_host.player;
        if (!world)
            return;
        if (TransformComponent* xf = body.valid() ? world->get<TransformComponent>(body) : nullptr)
            xf->position = m_rt.spawn;
        if (PlayerMotorComponent* pmc = body.valid() ? world->get<PlayerMotorComponent>(body) : nullptr)
            pmc->motor.reset();
        m_rt.lowerBodyYaw = 0.0f;
        if (AnimGraphComponent* ag = body.valid() ? world->get<AnimGraphComponent>(body) : nullptr)
            ag->graph.player().setLowerBodyYaw(0.0f);
        if (HealthComponent* hc = body.valid() ? world->get<HealthComponent>(body) : nullptr)
            hc->health.revive();
        if (HitReactionComponent* hr = body.valid() ? world->get<HitReactionComponent>(body) : nullptr)
        {
            hr->hit.reset();
            if (Combat::HitVolumesComponent* volumes = world->get<Combat::HitVolumesComponent>(body))
                Combat::clearHitPartState(*volumes);
        }
        if (JumpAttackComponent* jac = body.valid() ? world->get<JumpAttackComponent>(body) : nullptr)
            jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
        if (Combat::StatusEffectComponent* st = body.valid() ? world->get<Combat::StatusEffectComponent>(body) : nullptr)
            st->reset();
        if (body.valid() && m_host.audio)
            clearStatusFx(*world, m_host.audio, body);
        if (Combat::PoiseComponent* poise = body.valid() ? world->get<Combat::PoiseComponent>(body) : nullptr)
        {
            poise->reset();
            poise->hyperArmor = false;
        }
        m_rt.wet             = false;
        m_rt.submerged       = false;
        m_rt.underAmount     = 0.0f;
        m_rt.deadTimer       = 0.0f;
        m_rt.spawnAge        = 0.0f;
        m_rt.hurtTimer       = 0.0f;
        m_rt.jumpAttackBuffer = 0.0f;
        m_rt.offhand.reset();
        if (SkillComponent* sk = body.valid() ? world->get<SkillComponent>(body) : nullptr)
            sk->seeArmed = false;
        m_rt.attackCharge.reset();
        m_rt.blockCharge.reset();
        m_rt.chargeWindup = {};
        m_rt.fireQuick    = false;
        m_rt.fireCharged  = false;
        if (WeaponLoadoutComponent* wlc = body.valid() ? world->get<WeaponLoadoutComponent>(body) : nullptr)
        {
            if (wlc->loadout)
                wlc->loadout->clear();
        }
        if (m_host.onRespawn)
            m_host.onRespawn(m_host.user);
        DE_LOG_INFO("Player: respawned");
    }

    void PlaySession::resolveJump(const Combat::DamageEvent* events, int count)
    {
        World* world = m_host.world;
        if (count <= 0 || !events || !world || !m_host.audio || !m_host.assets)
            return;

        struct Snap
        {
            Entity e{};
            float  hp       = 0.0f;
            bool   wasAlive = false;
            bool   hunter   = false;
        };
        Snap snaps[8]{};
        const int n = count < 8 ? count : 8;
        for (int i = 0; i < n; ++i)
        {
            const Entity t = events[i].target;
            snaps[i].e     = t;
            if (!t.valid() || !world->alive(t))
                continue;
            snaps[i].hunter = world->has<AiAgentComponent>(t);
            if (const HealthComponent* hp = world->get<HealthComponent>(t))
            {
                snaps[i].hp       = hp->health.hp();
                snaps[i].wasAlive = hp->health.alive();
            }
        }

        Combat::ResolveResult results[8]{};
        Combat::resolveJumpAttackEvents(*world, m_combat, events, count, count <= 8 ? results : nullptr);

        const Entity player = m_host.player;
        for (int i = 0; i < n; ++i)
        {
            if (player.valid() && snaps[i].e.id() == player.id() && (results[i].blocked || results[i].parried))
                onDefended(events[i].source, results[i].parried);
        }
        const TransformComponent* playerXf = player.valid() ? world->get<TransformComponent>(player) : nullptr;
        const Math::Vector3f      playerPos = playerXf ? playerXf->position : Math::Vector3f{};
        for (int i = 0; i < n; ++i)
        {
            if (!snaps[i].e.valid())
                continue;
            HealthComponent* hp = world->get<HealthComponent>(snaps[i].e);
            if (!hp)
                continue;
            const bool damaged = hp->health.hp() < snaps[i].hp;
            const bool killed  = snaps[i].wasAlive && !hp->health.alive();
            if (!damaged && !killed)
                continue;
            if (!snaps[i].hunter && hp->health.alive() && !results[i].blocked)
            {
                if (player.valid() && snaps[i].e.id() == player.id())
                    m_rt.shake = 0.08f;
                if (AnimGraphComponent* ag = world->get<AnimGraphComponent>(snaps[i].e))
                    ag->graph.setTrigger("hit");
            }
            if (!snaps[i].hunter)
            {
                if (snaps[i].e.id() == player.id())
                    playSoundCue(*world, *m_host.audio, *m_host.assets, snaps[i].e, "pain");
                continue;
            }
            playSoundCueAt(*world, *m_host.audio, *m_host.assets, snaps[i].e, "pain", events[i].hitPoint);
            playSoundCueAt(*world, *m_host.audio, *m_host.assets, snaps[i].e, "grunt", events[i].hitPoint);
            if (m_host.ai)
                m_host.ai->onHunterAttacked(*world, snaps[i].e, playerPos);
            if (m_host.onBloodBurst)
                m_host.onBloodBurst(m_host.user, events[i].hitPoint);
            spawnLivingBloodDecal(impact(), snaps[i].e, events[i].hitPoint, -events[i].hitDir);
            if (killed)
            {
                spawnDeathBloodDecal(impact(), events[i].hitPoint, Math::Vector3f(0.0f, 0.0f, 1.0f));
                if (!impactDecalsReady(impact()) && m_host.onBloodSplat)
                    m_host.onBloodSplat(m_host.user, events[i].hitPoint.x, events[i].hitPoint.z);
                if (m_host.ai)
                    m_host.ai->onHunterKilled(*world, snaps[i].e);
            }
        }
    }

    void PlaySession::onWeaponHit(const WeaponHit& hit)
    {
        pull();
        World* world = m_host.world;
        if (!world || !m_host.audio || !m_host.assets)
            return;
        spawnWeaponImpact(impact(), hit);
        if (!hit.hitTarget)
            return;
        Entity victim = hit.targetEntity;
        if (!victim.valid())
            victim = m_rt.targets.entityAt(hit.targetIndex);
        if (!victim.valid() || !world->alive(victim))
            return;
        HealthComponent* hp = world->get<HealthComponent>(victim);
        const bool wasAlive = hp && hp->health.alive();
        const TransformComponent* attackerXf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        const Math::Vector3f knock = Combat::partKnockbackDirection(hit.part, attackerXf, hit.point, hit.direction);
        auto spendPieces = [&](float damage) {
            Combat::ArmorBreak broke{};
            if (Combat::ArmorPiecesComponent* armor = world->get<Combat::ArmorPiecesComponent>(victim))
            {
                if (const TransformComponent* vxf = world->get<TransformComponent>(victim))
                {
                    const bool hasFlat = world->get<Combat::ArmorComponent>(victim) != nullptr;
                    damage = Combat::absorbArmorHit(*armor, vxf->position, vxf->rotation, hit.point, hit.direction, damage, broke, hit.part, hasFlat);
                }
            }
            if (broke.index >= 0 && m_host.physics)
                knockOffArmor(*world, *m_host.physics, victim, broke);
            return damage;
        };
        auto noteIfAlive = [&]() {
            const HealthComponent* after = world->get<HealthComponent>(victim);
            if (!after || !after->health.alive())
                return;
            if (Combat::HitVolumesComponent* volumes = world->get<Combat::HitVolumesComponent>(victim))
                Combat::noteLandedPartHit(*volumes, hit.part);
        };
        if (world->has<AiAgentComponent>(victim))
        {
            if (!m_host.ai)
                return;
            const TransformComponent* playerXf  = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
            const Math::Vector3f      playerPos = playerXf ? playerXf->position : Math::Vector3f{};
            m_host.ai->forceAlert(*world, victim, playerPos);
            using MeleeResponse          = AiSystem::MeleeResponse;
            const bool          melee    = hit.weapon == WeaponKind::Melee;
            const MeleeResponse response = melee ? m_host.ai->respondToMelee(*world, victim, playerPos) : MeleeResponse::Stagger;
            if (response == MeleeResponse::Blocked)
            {
                faceTarget(victim);
                m_rt.hitStop     = 0.06f;
                m_rt.shake       = 0.07f;
                m_rt.blockRecoil = 0.55f;
                playSoundCueAt(*world, *m_host.audio, *m_host.assets, victim, "impact", hit.point);
                if (AnimGraphComponent* vag = world->get<AnimGraphComponent>(victim))
                    vag->graph.setTrigger("block");
                if (AnimGraphComponent* pag = m_host.player.valid() ? world->get<AnimGraphComponent>(m_host.player) : nullptr)
                    pag->graph.setTrigger("hit");
                m_host.ai->onHunterAttacked(*world, victim, playerPos);
                return;
            }
            const float damage  = spendPieces(hit.damage);
            const bool  damaged = m_host.ai->applyHunterDamage(*world, victim, damage);
            noteIfAlive();
            if (!damaged)
                return;
            if (response == MeleeResponse::Stagger)
                m_host.ai->applyHunterHitReaction(*world, victim, knock);
            if (melee)
            {
                faceTarget(victim);
                const HealthComponent* after = world->get<HealthComponent>(victim);
                if (AnimGraphComponent* vag = world->get<AnimGraphComponent>(victim); vag && after && after->health.alive() && response == MeleeResponse::Stagger)
                    vag->graph.setTrigger("hit");
                const bool heavy = m_rt.swingCharged || (hp && !hp->health.alive());
                m_rt.hitStop     = heavy ? 0.12f : 0.07f;
                playSoundCueAt(*world, *m_host.audio, *m_host.assets, victim, "impact", hit.point);
            }
            playSoundCueAt(*world, *m_host.audio, *m_host.assets, victim, "pain", hit.point);
            playSoundCueAt(*world, *m_host.audio, *m_host.assets, victim, "grunt", hit.point);
            m_host.ai->onHunterAttacked(*world, victim, playerPos);
            if (m_host.onBloodBurst)
                m_host.onBloodBurst(m_host.user, hit.point);
            spawnLivingBloodDecal(impact(), victim, hit.point, hit.normal);
            hp = world->get<HealthComponent>(victim);
            if (wasAlive && hp && !hp->health.alive())
            {
                spawnDeathBloodDecal(impact(), hit.point, hit.normal);
                if (!impactDecalsReady(impact()) && m_host.onBloodSplat)
                    m_host.onBloodSplat(m_host.user, hit.point.x, hit.point.z);
                m_host.ai->onHunterKilled(*world, victim);
            }
            return;
        }
        if (!hp)
            return;
        const float damage = spendPieces(hit.damage);
        const float before = hp->health.hp();
        hp->health.applyDamage(damage);
        noteIfAlive();
        if (hp->health.hp() >= before)
            return;
        if (HitReactionComponent* hr = world->get<HitReactionComponent>(victim))
            hr->hit.apply(knock);
        playSoundCueAt(*world, *m_host.audio, *m_host.assets, victim, "pain", hit.point);
    }

    void PlaySession::switchWeapons()
    {
        pull();
        World* world = m_host.world;
        Input* input = m_host.input;
        if (!world || !input || !m_host.audio || !m_host.assets || !m_host.player.valid())
            return;
        WeaponLoadoutComponent* wlc = world->get<WeaponLoadoutComponent>(m_host.player);
        WeaponLoadout*          w   = (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
        if (!w)
            return;
        const Entity listener = listenerOf(m_host);
        if (input->actionPressed("weapon_1") && ownsWeapon(WeaponKind::Melee) && w->selectMelee())
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *m_host.audio, *m_host.assets, listener, "click");
            DE_LOG_INFO("Player: weapon melee");
        }
        if (input->actionPressed("weapon_2") && ownsWeapon(WeaponKind::Projectile) && w->selectProjectile())
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *m_host.audio, *m_host.assets, listener, "click");
            DE_LOG_INFO("Player: weapon {}", w->projectile().name());
        }
        if (input->actionPressed("weapon_3") && w->selectSlot(2, throwableBelt(*world, m_host.player)))
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *m_host.audio, *m_host.assets, listener, "click");
            const std::string held(throwableBelt(*world, m_host.player).hand());
            DE_LOG_INFO("Player: weapon {}", held);
        }
    }

    void PlaySession::stockHunter(Entity hunter)
    {
        pull();
        World* world = m_host.world;
        if (!world || !hunter.valid() || !m_host.pins || !m_host.assets || !m_host.renderer || !m_host.physics)
            return;
        const ItemDef* med = findItemDef("medkit");
        if (!med)
        {
            DE_LOG_WARN("Play: medkit is not in the item catalog");
            return;
        }
        InventoryComponent inv;
        addItem(inv, *med, 1);
        if (InventoryComponent* existing = world->get<InventoryComponent>(hunter))
            *existing = inv;
        else
            world->emplace<InventoryComponent>(hunter, inv);
        removeArmor(*world, *m_host.pins, *m_host.physics, hunter);
        equipHunterArmor(*world, *m_host.pins, *m_host.assets, *m_host.renderer, hunter);
        equipSword(*world, *m_host.pins, *m_host.assets, *m_host.renderer, hunter);
    }

    PlayInteractPick PlaySession::pickInteract()
    {
        pull();
        PlayInteractPick best{};
        World*           world = m_host.world;
        if (!world || !m_host.camera || carriedBody(*world).valid())
            return best;
        const TransformComponent* pxf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        if (!pxf)
            return best;
        const Math::Vector3f eye = m_host.camera->GetPosition();
        Math::Vector3f       look = m_host.camera->GetLook();
        look.Normalize();
        float bestAim = 0.94f;
        auto  consider = [&](PlayInteract kind, Entity e, const Math::Vector3f& at, float range) {
            if (Math::Vector3f{ at.x - pxf->position.x, 0.0f, at.z - pxf->position.z }.MagnitudeSqrd() > range * range)
                return;
            Math::Vector3f to = at - eye;
            if (to.MagnitudeSqrd() < 1.0e-6f)
                return;
            to.Normalize();
            const float aim = look.Dot(to);
            if (aim <= bestAim)
                return;
            bestAim = aim;
            best    = PlayInteractPick{ kind, e };
        };
        world->each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const TagComponent*       tag = world->get<TagComponent>(e);
            const TransformComponent* gxf = world->get<TransformComponent>(e);
            const HealthComponent*    hp  = world->get<HealthComponent>(e);
            if (gxf && hp && !hp->health.alive() && !ai.carried && !(tag && tag->name == "Wolf"))
                consider(PlayInteract::Body, e, gxf->position, 2.2f);
        });
        world->each<PowerBoxComponent>([&](Entity e, PowerBoxComponent& box) {
            const TransformComponent* xf = world->get<TransformComponent>(e);
            consider(PlayInteract::Power, e, xf ? xf->position : box.pos, 2.5f);
        });
        world->each<ItemPickupComponent>([&](Entity e, ItemPickupComponent& pickup) {
            if (const TransformComponent* ixf = world->get<TransformComponent>(e); ixf && !pickup.item.empty())
                consider(PlayInteract::Item, e, ixf->position, 2.2f);
        });
        world->each<LootComponent>([&](Entity e, LootComponent& loot) {
            if (const TransformComponent* ixf = world->get<TransformComponent>(e); ixf && itemCount(loot.contents) > 0)
                consider(PlayInteract::Item, e, ixf->position, 2.2f);
        });
        return best;
    }

    Entity PlaySession::takedownTarget()
    {
        pull();
        World* world = m_host.world;
        if (!world || !m_host.chaseReady)
            return {};
        const TransformComponent* pxf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        if (!pxf || carriedBody(*world).valid())
            return {};
        Entity best{};
        float  bestD = 2.5f * 2.5f;
        world->each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const TagComponent*       tag = world->get<TagComponent>(e);
            const TransformComponent* gxf = world->get<TransformComponent>(e);
            const HealthComponent*    hp  = world->get<HealthComponent>(e);
            if (!gxf || !hp || !hp->health.alive() || (tag && tag->name == "Wolf") || ai.aware.alertness == AI::Alertness::Alert)
                return;
            const Math::Vector3f to{ pxf->position.x - gxf->position.x, 0.0f, pxf->position.z - gxf->position.z };
            const float          d = to.MagnitudeSqrd();
            Math::Vector3f       fwd{ ai.forward.x, 0.0f, ai.forward.z };
            if (d > bestD || d < 1.0e-6f || std::fabs(pxf->position.y - gxf->position.y) > 1.2f || fwd.MagnitudeSqrd() < 1.0e-6f)
                return;
            fwd.Normalize();
            if (fwd.Dot(to * (1.0f / std::sqrt(d))) > -0.2f)
                return;
            best  = e;
            bestD = d;
        });
        return best;
    }

    bool PlaySession::tryTakedown()
    {
        const Entity target = takedownTarget();
        World*       world  = m_host.world;
        if (!target.valid() || !world || !m_host.ai || !m_host.audio || !m_host.assets)
            return false;
        m_host.ai->applyHunterDamage(*world, target, 1.0e6f);
        if (const TransformComponent* gxf = world->get<TransformComponent>(target))
            playSoundCueAt(*world, *m_host.audio, *m_host.assets, target, "grunt", gxf->position);
        DE_LOG_INFO("Player: takedown");
        m_rt.quest.event("takedown");
        return true;
    }

    void PlaySession::tryPickup()
    {
        World* world = m_host.world;
        if (!world || !m_host.player.valid() || !m_host.pins || !m_host.physics || !m_host.audio || !m_host.assets)
            return;
        if (carriedBody(*world).valid())
        {
            CarryView view{};
            view.world      = world;
            view.carrier    = m_host.player;
            view.look       = m_host.camera ? m_host.camera->GetLook() : Math::Vector3f{ 0.0f, 0.0f, 1.0f };
            view.heightAt   = &PlaySession::onCarryHeight;
            view.heightUser = this;
            dropCarriedBody(view);
            return;
        }
        const PlayInteractPick pick = pickInteract();
        if (pick.kind == PlayInteract::Body)
        {
            takeCarriedBody(*world, m_host.player, pick.e, &PlaySession::onCarryHeight, this);
            DE_LOG_INFO("Player: picked up body");
            return;
        }
        if (pick.kind == PlayInteract::Power)
        {
            const PowerBoxComponent* box = pick.e.valid() ? world->get<PowerBoxComponent>(pick.e) : nullptr;
            if (box)
            {
                const AI::Walkability* walk = m_host.ai ? &m_host.ai->walkability() : nullptr;
                setPowerCircuit(*world, pick.e, !box->on, true, walk, m_host.cloudTime, &m_rt.quest.tracker(), &PlaySession::onPowerClick, this);
            }
            return;
        }
        if (pick.kind != PlayInteract::Item)
            return;
        InventoryComponent* inv = world->get<InventoryComponent>(m_host.player);
        if (!inv)
            return;
        const Entity e = pick.e;
        if (e.valid() && world->has<LootComponent>(e))
        {
            if (m_host.onLoot)
                m_host.onLoot(m_host.user, e);
            return;
        }
        ItemPickupComponent* pickup = e.valid() ? world->get<ItemPickupComponent>(e) : nullptr;
        if (!pickup || pickup->item.empty() || !pickup->item.def)
            return;
        if (throwableCatalog().find(pickup->item.def->id))
        {
            const std::string       id   = std::string(pickup->item.def->id);
            const CarryPickupResult took = takeCarryPickup(*world, *m_host.pins, *m_host.physics, e, throwableBelt(*world, m_host.player), nullptr);
            if (took.throwable)
            {
                DE_LOG_INFO("Throwable: picked up {}", id);
                playSoundCue(*world, *m_host.audio, *m_host.assets, listenerOf(m_host), "click");
            }
            return;
        }
        const int added = grantInventoryItem(*world, *m_host.audio, *m_host.assets, listenerOf(m_host), *inv, pickup->item);
        pickup->item.count -= added;
        if (pickup->item.count <= 0)
            destroyPickup(*world, *m_host.pins, *m_host.physics, e);
    }

    void PlaySession::tickCarry()
    {
        World* world = m_host.world;
        if (!world || !m_host.player.valid())
            return;
        CarryView view{};
        view.world      = world;
        view.carrier    = m_host.player;
        view.look       = m_host.camera ? m_host.camera->GetLook() : Math::Vector3f{ 0.0f, 0.0f, 1.0f };
        view.heightAt   = &PlaySession::onCarryHeight;
        view.heightUser = this;
        view.onHidden   = &PlaySession::onCarryHidden;
        view.user       = this;
        if (m_host.ai)
        {
            const std::vector<Math::AABox3f>& volumes = m_host.ai->concealVolumes();
            view.conceal      = volumes.empty() ? nullptr : volumes.data();
            view.concealCount = static_cast<int>(volumes.size());
        }
        tickCarriedBody(view);
    }

    void PlaySession::tickPawn(float dt)
    {
        pull();
        World* world = m_host.world;
        Input* input = m_host.input;
        if (!world || !input)
            return;

        m_rt.swimDiveEdge  = false;
        m_rt.swimStandEdge = false;
        const Entity    body   = m_host.player;
        SkillComponent* skills = body.valid() ? world->get<SkillComponent>(body) : nullptr;
        if (skills)
            tickSkill(*skills, dt);
        TransformComponent* xf = body.valid() ? world->get<TransformComponent>(body) : nullptr;
        if (!xf)
        {
            if (!m_host.uiKeys)
                switchWeapons();
            return;
        }

        if (m_rt.swingLock > 0.0f)
            m_rt.swingLock -= dt;
        if (m_rt.comboWindow > 0.0f)
            m_rt.comboWindow -= dt;
        if (m_rt.blockRecoil > 0.0f)
            m_rt.blockRecoil -= dt;
        if (m_rt.faceLeft > 0.0f)
        {
            m_rt.faceLeft -= dt;
            m_rt.lookYaw = approachAngle(m_rt.lookYaw, m_rt.faceYaw, 12.0f, dt);
        }
        accumulatePlayLook(m_rt.lookYaw, m_rt.lookPitch, static_cast<float>(input->mouseDeltaX()), static_cast<float>(input->mouseDeltaY()), m_host.applyMouseLook,
                           input->actionAxis("look_yaw"), input->actionAxis("look_pitch"), dt);

        const float     mx    = m_host.uiKeys ? 0.0f : input->actionAxis("move_x");
        const float     mz    = m_host.uiKeys ? 0.0f : input->actionAxis("move_z");
        const PlayBasis basis = playBasis(m_rt.lookYaw, m_rt.lookPitch, mx, mz);
        const Math::Vector3f& flat  = basis.flat;
        const Math::Vector3f& right = basis.right;
        const Math::Vector3f& wish  = basis.wish;

        if (!m_rt.haveSpawn)
        {
            m_rt.spawn     = xf->position;
            m_rt.haveSpawn = true;
        }

        HealthComponent*               hc     = world->get<HealthComponent>(body);
        Health*                        hp     = hc ? &hc->health : nullptr;
        HitReactionComponent*          hr     = world->get<HitReactionComponent>(body);
        HitReaction*                   hitRx  = hr ? &hr->hit : nullptr;
        PlayerMotorComponent*          pmc    = world->get<PlayerMotorComponent>(body);
        PlayerMotor*                   motor  = pmc ? &pmc->motor : nullptr;
        JumpAttackComponent*           jac    = world->get<JumpAttackComponent>(body);
        Combat::JumpAttack*            jump   = jac ? &jac->jump : nullptr;
        Combat::StatusEffectComponent* status = world->get<Combat::StatusEffectComponent>(body);
        Combat::PoiseComponent*        poise  = world->get<Combat::PoiseComponent>(body);
        if (jump && (!hp || !hp->alive()))
            jump->cancel(Combat::JumpAttackCancel::NoPound);
        const bool ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
        const bool jumpBusy    = jump && jump->busy();
        const bool inAirCommit = jump && jump->inAirCommit();
        const bool canSteer    = hp && hp->alive() && !ccLocked && !(jump && jump->phase() == Combat::JumpAttackPhase::Pound);
        const Combat::HitVolumesComponent* partVolumes = world->get<Combat::HitVolumesComponent>(body);
        const bool shieldBlocked = Combat::hitVolumesBlockShield(partVolumes);
        const bool holdShield = canSteer && !shieldBlocked
            && ((input->actionMouseDown("shield") && !m_host.uiMouse) || (!m_host.uiKeys && input->actionDown("shield")) || input->actionAxisHeld("shield"));
        const bool carriesLight = flashlightEquipped(world->get<InventoryComponent>(body));
        const bool toggleLight  = carriesLight && canSteer && !m_host.uiKeys && input->actionPressed("flashlight");
        if (!m_host.uiKeys && input->actionPressed("pickup"))
            tryPickup();
        if (!m_host.uiKeys && input->actionPressed("takedown"))
            tryTakedown();
        if (toggleLight)
        {
            if (SkillComponent* sk = world->get<SkillComponent>(body))
                sk->seeArmed = true;
        }
        const bool attackDown = canSteer && !m_host.uiKeys && (input->actionDown("attack") || (m_host.allowMouseButtons && input->actionMouseDown("attack")));
        const bool airborneNow = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
        WeaponAttackInput chargeQuery{};
        chargeQuery.airborne  = airborneNow;
        chargeQuery.swimming  = motor && motor->state() == PlayerMoveState::Swimming;
        chargeQuery.ccLocked  = ccLocked;
        chargeQuery.jumpBusy  = jumpBusy;
        WeaponLoadout* held = nullptr;
        if (WeaponLoadoutComponent* wlc = world->get<WeaponLoadoutComponent>(body))
            held = (wlc->loadout) ? wlc->loadout.get() : nullptr;
        if (held)
            classifyWeaponAttack(chargeQuery, held->activeKind());
        const WeaponAttackDecision chargeDecision = decideWeaponAttack(chargeQuery);
        Combat::DefenseComponent*  defense        = world->get<Combat::DefenseComponent>(body);
        if (defense)
            defense->tick(dt);
        Combat::PlayerChargeInput chargeIn{};
        chargeIn.dt              = dt;
        chargeIn.attackDown      = attackDown;
        chargeIn.canChargeAttack = canSteer && chargeDecision.allowCharge;
        chargeIn.holdShield      = holdShield;
        chargeIn.canHoldShield   = canSteer && !shieldBlocked;
        chargeIn.chargedParryUp  = defense && defense->chargedParry && defense->inParryWindow();
        Combat::PlayerChargeStep charge{};
        Combat::stepPlayerCharge(m_rt.attackCharge, m_rt.blockCharge, m_rt.chargeSettings, chargeIn, charge);
        m_rt.fireQuick   = charge.fireQuick;
        m_rt.fireCharged = charge.fireCharged;
        if (!m_host.uiKeys)
            switchWeapons();
        const bool wasBlocking = m_rt.offhand.shield.blocking();
        m_rt.offhand.tick(dt, charge.wantShield, toggleLight);
        if (defense)
        {
            if (charge.openChargedParry)
                Combat::openShieldParry(*defense, true, m_rt.chargeSettings);
            else if (!wasBlocking && m_rt.offhand.shield.blocking() && !m_rt.blockCharge.isCharged())
                Combat::openShieldParry(*defense, false, m_rt.chargeSettings);
            Combat::syncShieldDefense(*defense, m_rt.offhand.shield, m_rt.lookYaw);
        }

        PlayerMotorInput motorIn{};
        PlayMotorEdges   edges{};
        edges.sprintDown    = input->actionDown("sprint");
        edges.crouchPressed = input->actionPressed("crouch");
        edges.crouchDown    = input->actionDown("crouch");
        edges.jumpPressed   = input->actionPressed("jump");
        edges.jumpDown      = input->actionDown("jump");
        edges.forward       = input->actionPressed("dodge_forward");
        edges.back          = input->actionPressed("dodge_back");
        edges.left          = input->actionPressed("dodge_left");
        edges.right         = input->actionPressed("dodge_right");
        const bool sprintAllowed = canSteer && !jumpBusy && !m_host.uiKeys && m_rt.swingLock <= 0.0f;
        buildPlayMotorInput(motorIn, m_rt.crouchLatch, wish, flat, right, canSteer, jumpBusy, m_host.uiKeys, inAirCommit, jump, sprintAllowed, edges);
        const bool onGround = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch || motor->state() == PlayerMoveState::Dodge);
        const Terrain::GroundContact groundSurf = (onGround && m_host.groundContact) ? m_host.groundContact(m_host.user, xf->position.x, xf->position.z) : Terrain::GroundContact{};
        const float swingSlow = m_rt.blockRecoil > 0.0f ? 0.4f : (m_rt.swingLock <= 0.0f ? 1.0f : (m_rt.comboCount > 1 ? 0.45f : 0.8f));
        float speedScale = (status ? status->moveSpeedScale() : 1.0f) * m_rt.offhand.shield.speedScale() * (onGround ? groundSurf.moveSpeed : 1.0f) * swingSlow
                         * (carriedBody(*world).valid() ? 0.6f : 1.0f);
        if (partVolumes)
            speedScale = Combat::hitVolumesSpeedScale(partVolumes, speedScale);
        motorIn.speedScale = speedScale;
        if (skills)
        {
            motorIn.runScale  = skillScalar(SkillId::Run, SkillScalar::RunScale, skills->level(SkillId::Run));
            motorIn.swimScale = skillScalar(SkillId::Swim, SkillScalar::SwimScale, skills->level(SkillId::Swim));
            motorIn.jumpScale = skillScalar(SkillId::Jump, SkillScalar::JumpScale, skills->level(SkillId::Jump));
        }

        m_rt.groundProbeY = xf->position.y;
        if (m_host.groundProbeY && m_host.groundProbeY != &m_rt.groundProbeY)
            *m_host.groundProbeY = m_rt.groundProbeY;
        Physics::PhysicsBodyId ignore = Physics::kNullPhysicsBody;
        if (m_host.physics && m_host.physics->valid())
            ignore = m_host.physics->bodyOf(body);
        if (m_host.groundIgnore)
            *m_host.groundIgnore = ignore;

        PlayerGroundQuery ground{};
        ground.user     = m_host.user;
        ground.waterY   = m_host.waterLevel;
        ground.heightAt = m_host.heightAt;
        const PlayWallProbe walls{ m_host.physics, ignore, m_host.groundBody };
        bindWallProbe(ground, walls);

        const Math::Vector3f before = xf->position;
        PlayerMotorResult    motorOut{};
        if (motor)
            motorOut = motor->tick(xf->position, motorIn, dt, ground);
        m_rt.swimDiveEdge  = motorOut.splashed;
        m_rt.swimStandEdge = motorOut.stood;
        if (jump && jump->inAirCommit() && motor && !motorOut.landed && !motorOut.splashed)
        {
            Math::Vector3f vel = motor->velocity();
            Math::Vector3f targetPos{};
            const Math::Vector3f* targetPtr = nullptr;
            bool                  hasTarget = false;
            if (jump->phase() == Combat::JumpAttackPhase::Connected && jump->connectedTarget().valid())
            {
                if (const TransformComponent* txf = world->get<TransformComponent>(jump->connectedTarget()))
                {
                    targetPos = txf->position;
                    targetPtr = &targetPos;
                    hasTarget = true;
                }
            }
            jump->applyAirSteering(vel, xf->position, flat, targetPtr, hasTarget, dt);
            motor->setHorizontalVelocity(vel.x, vel.z);
            xf->position.x = before.x + vel.x * dt;
            xf->position.z = before.z + vel.z * dt;
        }
        if (jump && jump->phase() == Combat::JumpAttackPhase::Connected && !motorOut.landed && !motorOut.splashed)
        {
            if (const TransformComponent* txf = jump->connectedTarget().valid() ? world->get<TransformComponent>(jump->connectedTarget()) : nullptr)
                jump->applyConnectSnap(xf->position, txf->position, dt);
        }
        Math::Vector3f hitSlide{};
        if (hitRx)
        {
            hitSlide = hitRx->tick(dt);
            xf->position += hitSlide;
        }
        if (Combat::HitVolumesComponent* volumes = world->get<Combat::HitVolumesComponent>(body))
            Combat::tickHitVolumes(*volumes, dt);

        if (m_host.preparePlayer)
            m_host.preparePlayer(m_host.user);
        if (m_host.physics && m_host.foliageCollision && m_host.terrain)
            Physics::syncFoliageCollision(*m_host.physics, *m_host.foliageCollision, *m_host.terrain, m_host.foliage, xf->position.x, xf->position.z);

        Math::Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
        if (delta.MagnitudeSqrd() > 1.0e-10f && !motorOut.climbed)
        {
            auto liveDynamic = [&](Entity solid) {
                if (!m_host.physics || !solid.valid())
                    return false;
                const PhysicsBodyComponent* pb = world->get<PhysicsBodyComponent>(solid);
                return pb && pb->valid && pb->mode != PhysicsBodyMode::Static && m_host.physics->bodyValid(pb->body);
            };
            Math::Sphere3f ball{ Math::Vector3f{ before.x, xf->position.y, before.z }, kPlayBodyRadius };
            float          bestT = 1.0f;
            for (int i = 0; i < m_host.cubeCount; ++i)
            {
                if (m_host.cubeEntities && i < m_host.cubeEntityCount && liveDynamic(m_host.cubeEntities[i]))
                    continue;
                if (Collision::Intersects(ball, m_host.cubes[i]))
                    continue;
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, m_host.cubes[i]);
                if (hit.hit && hit.t < bestT)
                    bestT = hit.t;
            }
            for (int i = 0; i < m_host.sphereCount; ++i)
            {
                if (m_host.sphereEntities && i < m_host.sphereEntityCount && liveDynamic(m_host.sphereEntities[i]))
                    continue;
                if (Collision::Intersects(ball, m_host.spheres[i]))
                    continue;
                const Collision::SweptHit3D swept = Collision::SweptIntersects(ball, delta, m_host.spheres[i]);
                if (swept.hit && swept.t < bestT)
                    bestT = swept.t;
            }
            if (m_host.physics)
                bestT = tightenPlanarClip(*m_host.physics, ignore, m_host.groundBody, Math::Vector3f{ before.x, xf->position.y, before.z }, delta, bestT);
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
        if (motor && motor->state() == PlayerMoveState::Grounded && m_host.heightAt)
        {
            m_rt.groundProbeY = Math::Max(m_rt.groundProbeY, xf->position.y);
            if (m_host.groundProbeY && m_host.groundProbeY != &m_rt.groundProbeY)
                *m_host.groundProbeY = m_rt.groundProbeY;
            xf->position.y = m_host.heightAt(m_host.user, xf->position.x, xf->position.z) + motor->settings().groundOffset;
        }

        m_rt.wet       = motor && motor->state() == PlayerMoveState::Swimming;
        m_rt.submerged = motor && motor->submerged();
        if (m_rt.wet && hp && hp->alive() && motor->breath() <= 0.0f)
            hp->applyDamage(8.0f * dt);

        bool skipLandCue = false;
        if (jump)
        {
            jump->tick(dt);
            if (hp && hp->alive())
            {
                if (motorOut.splashed)
                    jump->onSplashed();
                else if (motorOut.landed)
                {
                    jump->onLanded(xf->position);
                    if (jump->phase() == Combat::JumpAttackPhase::Pound)
                    {
                        Combat::DamageEvent poundEvents[8]{};
                        const int           n = jump->tryPound(weaponQuery(), xf->position, poundEvents, 8);
                        DE_LOG_INFO("Player: ground pound");
                        if (playSoundCue(*world, *m_host.audio, *m_host.assets, body, "pound"))
                            skipLandCue = true;
                        resolveJump(poundEvents, n);
                    }
                }
                else if (jump->phase() == Combat::JumpAttackPhase::Leap)
                {
                    Combat::DamageEvent connectEv{};
                    if (jump->tryConnect(weaponQuery(), xf->position, flat, connectEv))
                    {
                        DE_LOG_INFO("Player: pounce connect");
                        playSoundCueFallback(*world, *m_host.audio, *m_host.assets, body, "pounce", "impact");
                        resolveJump(&connectEv, 1);
                    }
                }
            }
            if (poise)
                poise->hyperArmor = jump->inAirCommit();
        }

        if (HsmGraphComponent* hsm = world->get<HsmGraphComponent>(body))
        {
            if (hsm->instance)
            {
                const uint8_t motorState = motor ? static_cast<uint8_t>(motor->state()) : 0;
                syncPlayerHsm(*hsm->instance, motorState, hp && hp->alive());
            }
        }

        if (canSteer && flat.MagnitudeSqrd() > 1.0e-6f)
            xf->rotation = Math::Quaternion::FromLookRotation(flat, Math::Vector3f::Y_AXIS);
        if (canSteer && motor && motor->swimPose() > 1.0e-4f)
            xf->rotation = swimBodyRotation(xf->rotation, motor->swimPose());
        const bool blockStriking = defense && defense->chargedParry && defense->inParryWindow();
        stepChargeWindup(m_rt.chargeWindup, dt, m_rt.attackCharge.phase() != Combat::HoldChargePhase::Idle, m_rt.attackCharge.heldSeconds(), m_rt.chargeSettings.attackWindowSeconds,
                         m_rt.blockCharge.phase() != Combat::HoldChargePhase::Idle, m_rt.blockCharge.heldSeconds(), m_rt.chargeSettings.blockWindowSeconds, blockStriking);
        if (TransformComponent* shieldXf = m_host.shield.valid() ? world->get<TransformComponent>(m_host.shield) : nullptr)
        {
            Math::Vector3f grip{};
            const bool     inHand = boneWorld(body, "L_Hand", grip);
            if (m_rt.shieldBump > 0.0f)
            {
                m_rt.shieldBump -= dt;
                const float k = Math::Max(0.0f, m_rt.shieldBump) / 0.2f;
                grip += flat * (-0.1f * k) + right * (std::sinf(m_rt.shieldBump * 120.0f) * 0.03f * k);
            }
            placePlayerShield(*shieldXf, *xf, m_rt.offhand.shield.alpha(), m_rt.chargeWindup.block, m_rt.chargeWindup.blockStrike, inHand ? &grip : nullptr);
        }

        if (m_host.audio && m_host.assets)
        {
            if (motorOut.jumped)
                playSoundCue(*world, *m_host.audio, *m_host.assets, body, "jump");
            if (motorOut.landed && !skipLandCue)
                playSoundCue(*world, *m_host.audio, *m_host.assets, body, "land");
            if (motorOut.splashed || motorOut.leapt)
                playSoundCue(*world, *m_host.audio, *m_host.assets, body, "splash");
        }
        if (motorOut.landed || motorOut.splashed)
            m_rt.footstepAcc = 0.0f;

        const float stepSpeed = Math::Vector3f{ delta.x, 0.0f, delta.z }.Magnitude() / Math::Max(dt, 1.0e-4f);
        const bool  stepping  = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Swimming) && stepSpeed > 2.0f;
        if (stepping)
        {
            const float cadence = motor->state() == PlayerMoveState::Swimming ? 0.55f : 0.35f;
            m_rt.footstepAcc += dt * (stepSpeed * cadence);
            if (m_rt.footstepAcc >= 1.0f)
            {
                m_rt.footstepAcc = 0.0f;
                if (motor->state() == PlayerMoveState::Swimming)
                {
                    if (m_host.audio && m_host.assets)
                        playSoundCue(*world, *m_host.audio, *m_host.assets, body, "swim_step");
                }
                else
                {
                    const Terrain::GroundContact stepGround = m_host.groundContact ? m_host.groundContact(m_host.user, xf->position.x, xf->position.z) : Terrain::GroundContact{};
                    const char*                  cue       = (stepGround.cue && stepGround.cue[0]) ? stepGround.cue : "step";
                    Math::Vector3f               facing    = flat;
                    if (facing.MagnitudeSqrd() <= 1.0e-6f)
                        facing = xf->rotation.Rotate(Math::Vector3f::Z_AXIS);
                    spawnFootmark(impact(), xf->position, facing);
                    if (m_host.audio && m_host.assets && !playSoundCue(*world, *m_host.audio, *m_host.assets, body, cue))
                        playSoundCue(*world, *m_host.audio, *m_host.assets, body, "step");
                }
            }
        }
        else
            m_rt.footstepAcc = 0.0f;

        if (SoundEmitterComponent* se = world->get<SoundEmitterComponent>(body))
            se->play = m_rt.wet;
    }

    void PlaySession::tickCamera()
    {
        pull();
        World* world = m_host.world;
        if (!world || !m_host.camera)
            return;
        const TransformComponent* xf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr;
        if (!xf)
            return;
        const PlayerMotor* motor    = nullptr;
        if (const PlayerMotorComponent* pmc = world->get<PlayerMotorComponent>(m_host.player))
            motor = &pmc->motor;
        const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
        const float pose    = motor ? motor->swimPose() : 0.0f;
        const bool surface  = m_rt.wet && motor && motor->depth() < 0.25f && pose < 0.05f;
        const bool rayOk    = m_host.terrain != nullptr;
        ShoulderView view = solveShoulderCamera(xf->position, m_rt.lookYaw, m_rt.lookPitch, crouched, m_rt.shake, m_rt.shakeTime, surface, m_host.waterLevel,
                                                rayOk ? &shoulderTerrainRay : nullptr, rayOk ? m_host.terrain : nullptr);
        if (pose > 0.0f)
        {
            const ShoulderView swim = solveSwimCamera(xf->position, m_rt.lookYaw, rayOk ? &shoulderTerrainRay : nullptr, rayOk ? m_host.terrain : nullptr);
            view.eye = view.eye + (swim.eye - view.eye) * pose;
            view.aim = view.aim + (swim.aim - view.aim) * pose;
        }
        m_rt.underAmount = underwaterAmount(m_rt.wet, view.eye.y, m_host.waterLevel);
        Camera3D& camera = *m_host.camera;
        camera.SetLens(1.04719755f, camera.GetAspect(), camera.GetNearZ(), camera.GetFarZ());
        camera.LookAt(view.eye, view.aim, Math::Vector3f{ 0.0f, 1.0f, 0.0f });
        if (TransformComponent* cxf = m_host.cameraEntity.valid() ? world->get<TransformComponent>(m_host.cameraEntity) : nullptr)
            cxf->position = camera.GetPosition();
        syncPlayerFlashlight(*world, m_host.player, camera.GetPosition(), camera.GetLook(), camera.GetRight(), camera.GetUp(), m_rt.offhand.lightOn);
    }

    void PlaySession::tickCombat(float dt)
    {
        World* world = m_host.world;
        Input* input = m_host.input;
        if (!world || !input || !m_host.audio || !m_host.assets)
            return;
        tickThrows(dt);
        if (m_rt.hurtTimer > 0.0f)
            m_rt.hurtTimer -= dt;

        const Entity         body = m_host.player;
        HealthComponent*     hc   = body.valid() ? world->get<HealthComponent>(body) : nullptr;
        Health*              hpTick = hc ? &hc->health : nullptr;
        if (hpTick)
            hpTick->tick(dt);
        if (Combat::StatusEffectComponent* st = body.valid() ? world->get<Combat::StatusEffectComponent>(body) : nullptr)
            st->tick(dt);
        WeaponLoadoutComponent* wlc = body.valid() ? world->get<WeaponLoadoutComponent>(body) : nullptr;
        WeaponLoadout*          wTick = (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
        const WeaponWorldQuery  query = weaponQuery();
        if (wTick)
            wTick->tick(dt, query);

        if (!hpTick || !hpTick->alive())
        {
            m_rt.fireQuick  = false;
            m_rt.fireCharged = false;
            m_rt.deadTimer += dt;
            if (m_rt.deadTimer >= kPlayDeathSeconds)
                respawn();
            return;
        }

        const TransformComponent* xf = body.valid() ? world->get<TransformComponent>(body) : nullptr;
        JumpAttackComponent*      jac = body.valid() ? world->get<JumpAttackComponent>(body) : nullptr;
        Combat::JumpAttack*       jump = jac ? &jac->jump : nullptr;
        Combat::StatusEffectComponent* status = body.valid() ? world->get<Combat::StatusEffectComponent>(body) : nullptr;
        HitReaction* hitRx = nullptr;
        if (HitReactionComponent* hr = body.valid() ? world->get<HitReactionComponent>(body) : nullptr)
            hitRx = &hr->hit;
        PlayerMotor* motor = nullptr;
        if (PlayerMotorComponent* pmc = body.valid() ? world->get<PlayerMotorComponent>(body) : nullptr)
            motor = &pmc->motor;
        const bool inAirCommit = jump && jump->inAirCommit();
        const bool ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
        const bool jumpBusy    = jump && jump->busy();

        if (m_host.chaseReady && xf && !inAirCommit)
        {
            if (Combat::DefenseComponent* defense = world->get<Combat::DefenseComponent>(body))
                Combat::syncShieldDefense(*defense, m_rt.offhand.shield, m_rt.lookYaw);
            const ContactGrindResult grind = applyContactGrind(*world, m_combat, body, xf->position, dt);
            if (grind.killed)
            {
                DE_LOG_INFO("Player: down");
                playSoundCue(*world, *m_host.audio, *m_host.assets, body, "death");
            }
            if (grind.damaged && m_rt.hurtTimer <= 0.0f)
            {
                playSoundCue(*world, *m_host.audio, *m_host.assets, body, "pain");
                m_rt.hurtTimer = 0.40f;
                if (hitRx && !hitRx->stunned() && !hitRx->knockingBack())
                    hitRx->nudge(grind.away, 0.35f, 0.08f);
            }
        }

        if (m_rt.jumpAttackBuffer > 0.0f)
            m_rt.jumpAttackBuffer = Math::Max(0.0f, m_rt.jumpAttackBuffer - dt);

        const bool attackPressed = input->actionPressed("attack") || (m_host.allowMouseButtons && input->actionMousePressed("attack"));
        const bool swimming      = motor && motor->state() == PlayerMoveState::Swimming;
        const bool airborne      = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
        const bool fireQuick     = m_rt.fireQuick;
        const bool fireCharged   = m_rt.fireCharged;
        m_rt.fireQuick           = false;
        m_rt.fireCharged         = false;
        WeaponAttackInput attackQuery{};
        attackQuery.airborne      = airborne;
        attackQuery.swimming      = swimming;
        attackQuery.attackPressed = attackPressed;
        attackQuery.jumpBuffered  = m_rt.jumpAttackBuffer > 0.0f;
        attackQuery.fireQuick     = fireQuick;
        attackQuery.fireCharged   = fireCharged;
        attackQuery.ccLocked      = ccLocked;
        attackQuery.jumpBusy      = jumpBusy;
        if (wTick)
            classifyWeaponAttack(attackQuery, wTick->activeKind());
        const WeaponAttackDecision decision = decideWeaponAttack(attackQuery);

        auto noteSenses = [&]() {
            if (SkillComponent* sk = body.valid() ? world->get<SkillComponent>(body) : nullptr)
            {
                const bool foreign = m_host.audio->liveForeignSpatialVoices(body.id()) > 0;
                notePlayerSenseXp(*sk, sk->seeArmed, m_rt.offhand.lightOn, foreign, dt);
            }
        };

        if (ccLocked || jumpBusy)
        {
            if (ccLocked)
                m_rt.jumpAttackBuffer = 0.0f;
            noteSenses();
            return;
        }

        if (swimming)
        {
            m_rt.jumpAttackBuffer = 0.0f;
            if (decision.throwOnRelease)
                releaseThrow(fireCharged);
            else if (decision.fireLoadoutOnRelease)
                fireLoadout(fireCharged);
            noteSenses();
            return;
        }

        auto tryBeginJumpAttack = [&]() -> bool {
            if (!jump || !xf || !motor)
                return false;
            const float groundY     = terrainHeight(xf->position.x, xf->position.z);
            const float heightAbove = xf->position.y - groundY - motor->settings().groundOffset;
            const bool  airOk       = heightAbove >= jump->def().minHeight || motor->airTime() >= jump->def().minAirTime;
            if (!airOk)
                return false;
            Math::Vector3f lookFlat{ m_host.camera ? m_host.camera->GetLook().x : 0.0f, 0.0f, m_host.camera ? m_host.camera->GetLook().z : 1.0f };
            if (lookFlat.MagnitudeSqrd() > 1.0e-6f)
                lookFlat.Normalize();
            else
                lookFlat = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
            Combat::JumpAttackBegin req{};
            req.attacker          = body;
            req.position          = xf->position;
            req.lookFlat          = lookFlat;
            req.velocity          = motor->velocity();
            req.heightAboveGround = heightAbove;
            req.airTime           = motor->airTime();
            if (!jump->begin(req))
                return false;
            motor->clearJumpBuffer();
            triggerJumpAttackAnim(*world, body);
            DE_LOG_INFO("Player: jump attack");
            return true;
        };

        if (airborne)
        {
            if (decision.beginJumpAttack)
            {
                if (tryBeginJumpAttack())
                    m_rt.jumpAttackBuffer = 0.0f;
                else if (attackPressed)
                    m_rt.jumpAttackBuffer = kPlayJumpAttackBuffer;
            }
            if (decision.throwOnRelease)
            {
                m_rt.jumpAttackBuffer = 0.0f;
                releaseThrow(fireCharged);
            }
            noteSenses();
            return;
        }

        if (decision.throwOnRelease)
        {
            m_rt.jumpAttackBuffer = 0.0f;
            releaseThrow(fireCharged);
        }
        else if (decision.fireLoadoutOnRelease)
        {
            m_rt.jumpAttackBuffer = 0.0f;
            fireLoadout(fireCharged);
        }
        else if (m_rt.jumpAttackBuffer > 0.0f)
        {
            m_rt.jumpAttackBuffer = 0.0f;
            fireLoadout(false);
        }
        noteSenses();
    }

    void PlaySession::lightAndExposure(float dt)
    {
        World* world = m_host.world;
        if (!world || !m_host.ai || !m_host.chaseReady)
            return;
        const PlayerMotor* motor = nullptr;
        if (const PlayerMotorComponent* pmc = m_host.player.valid() ? world->get<PlayerMotorComponent>(m_host.player) : nullptr)
            motor = &pmc->motor;
        const bool sprintDown = m_host.input && m_host.input->actionDown("sprint");
        const PlayerStealthSettings fallback{};
        const PlayerStealthSettings& stealth = m_host.stealth ? *m_host.stealth : fallback;
        applyPreyFromMotor(*m_host.ai, stealth, motor, sprintDown);
        m_host.ai->ensureAwarenessSettings();
        const AI::AwarenessSettings& aware = m_host.ai->awarenessSettings();
        const float speed    = motor ? Math::Vector3f{ motor->velocity().x, 0.0f, motor->velocity().z }.Magnitude() : 0.0f;
        const bool  crouched = motor && motor->state() == PlayerMoveState::Crouch;
        const bool  moving   = speed >= stealth.stillSpeed;
        const bool  sprinting = moving && !crouched && sprintDown;

        float level = 1.0f;
        if (const TransformComponent* xf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr)
        {
            const Math::Vector3f chest{ xf->position.x, xf->position.y + 0.45f, xf->position.z };
            float                sun = Math::Clamp((m_host.sunElevation + 0.05f) * 3.0f, 0.0f, 1.0f);
            Math::Vector3f       toSun = m_host.sunDir;
            if (sun > 0.0f && toSun.MagnitudeSqrd() > 1.0e-6f && m_host.cubes && m_host.cubeCount > 0)
            {
                toSun.Normalize();
                const Math::Ray3f ray{ chest, toSun };
                for (int i = 0; i < m_host.cubeCount; ++i)
                {
                    const Collision::RayHit3D hit = Collision::Intersect(ray, m_host.cubes[i]);
                    if (hit.hit && hit.t > 0.0f && hit.t < 80.0f)
                    {
                        sun *= 0.3f;
                        break;
                    }
                }
            }
            float lamps = 0.0f;
            world->each<LocalLightComponent>([&](Entity e, LocalLightComponent& light) {
                if (!light.enabled || (m_host.muzzle.valid() && e.id() == m_host.muzzle.id()))
                    return;
                const TransformComponent* lxf = world->get<TransformComponent>(e);
                if (!lxf || light.range <= 0.0f)
                    return;
                Math::Vector3f to   = chest - lxf->position;
                const float    dist = to.Magnitude();
                if (dist >= light.range)
                    return;
                if (light.type == LocalLightType::Spot && dist > 1.0e-3f)
                {
                    const Math::Vector3f fwd = lxf->rotation.Rotate(Math::Vector3f::Z_AXIS);
                    if (fwd.Dot(to * (1.0f / dist)) < std::cosf(light.outerConeDeg * 0.0174533f))
                        return;
                }
                const float f = 1.0f - dist / light.range;
                lamps += f * f;
            });
            level = Math::Clamp(0.12f + 0.88f * sun + lamps, 0.0f, 1.0f);
        }
        float stance = crouched ? (moving ? aware.stanceCrouchWalk : aware.stanceCrouchStill)
                                : (sprinting ? aware.stanceSprint : (moving ? aware.stanceWalk : aware.stanceStand));
        if (m_host.camouflage)
            stance *= aware.stanceCamouflage;
        const float exposure = Math::Clamp((0.45f + 0.55f * level) * stance, 0.0f, 1.0f);
        if (m_host.lightLevel)
            *m_host.lightLevel = level;
        if (m_host.exposure)
            *m_host.exposure = exposure;
        m_host.ai->setPlayerExposure(exposure);
        m_host.ai->setPlayerMotion(Math::Clamp((speed - 0.15f) / 1.35f, 0.0f, 1.0f));
        const bool grounded = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch);
        if (m_host.strideLeft && grounded && moving)
        {
            *m_host.strideLeft -= speed * dt;
            if (*m_host.strideLeft <= 0.0f)
            {
                *m_host.strideLeft = crouched ? aware.strideCrouch : (sprinting ? aware.strideSprint : aware.strideWalk);
                if (const TransformComponent* pxf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr)
                    m_host.ai->addNoise(pxf->position, crouched ? stealth.hearCrouchWalk : (sprinting ? stealth.hearSprint : stealth.hearWalk));
            }
        }
    }

    void PlaySession::applyArmor()
    {
        World* world = m_host.world;
        if (!world || !m_host.player.valid() || !m_host.pins || !m_host.physics || !m_host.assets || !m_host.renderer)
            return;
        const InventoryComponent* inv = world->get<InventoryComponent>(m_host.player);
        if (!inv)
            return;
        Combat::ArmorComponent* armor = world->get<Combat::ArmorComponent>(m_host.player);
        if (!armor)
            armor = &world->emplace<Combat::ArmorComponent>(m_host.player);
        armor->stats.armor = equippedArmor(*inv);

        bool changed = false;
        for (int s = 0; s < static_cast<int>(EquipSlot::Count); ++s)
        {
            if (static_cast<EquipSlot>(s) == EquipSlot::Light)
                continue;
            if (m_rt.wornArmor[s] != inv->equipped[s].def)
            {
                m_rt.wornArmor[s] = inv->equipped[s].def;
                changed           = true;
            }
        }
        if (!changed)
            return;
        removeArmor(*world, *m_host.pins, *m_host.physics, m_host.player);
        const Combat::ArmorPiecesComponent all = Combat::makeHunterArmor();
        Combat::ArmorPiecesComponent       worn;
        for (int i = 0; i < all.count; ++i)
        {
            for (const ItemStack& eq : inv->equipped)
            {
                if (!eq.empty() && std::strcmp(eq.def->id, all.pieces[i].name) == 0)
                    worn.pieces[worn.count++] = all.pieces[i];
            }
        }
        if (worn.count > 0)
            equipArmorPieces(*world, *m_host.pins, *m_host.assets, *m_host.renderer, m_host.player, worn);
    }

    void PlaySession::lootHunters()
    {
        World* world = m_host.world;
        if (!world || !m_host.pins || !m_host.physics || !m_host.assets || !m_host.renderer)
            return;
        std::vector<Entity> hunters;
        world->each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
            if (world->has<InventoryComponent>(e))
                hunters.push_back(e);
        });
        for (Entity e : hunters)
        {
            const HealthComponent*    hp = world->get<HealthComponent>(e);
            const TransformComponent* xf = world->get<TransformComponent>(e);
            if (!hp || !xf)
                continue;
            const bool dead    = !hp->health.alive();
            const bool dropped = world->has<LootDroppedComponent>(e);
            if (!dead && dropped)
            {
                world->remove<LootDroppedComponent>(e);
                stockHunter(e);
                continue;
            }
            auto holdingLoot = [](const InventoryComponent& inv) {
                if (itemCount(inv) > 0)
                    return true;
                for (const ItemStack& stack : inv.equipped)
                    if (!stack.empty())
                        return true;
                return false;
            };
            const InventoryComponent* held = world->get<InventoryComponent>(e);
            if (dropped && (!held || !holdingLoot(*held)))
                continue;
            if (!dead)
                continue;
            if (!dropped)
                world->emplace<LootDroppedComponent>(e);
            InventoryComponent contents = *world->get<InventoryComponent>(e);
            *world->get<InventoryComponent>(e) = {};
            if (const Combat::ArmorPiecesComponent* armor = world->get<Combat::ArmorPiecesComponent>(e))
            {
                for (int p = 0; p < armor->count; ++p)
                {
                    const ItemDef* def = findItemDef(armor->pieces[p].name);
                    if (armor->pieces[p].attached && def)
                        addItem(contents, *def, 1);
                }
            }
            removeArmor(*world, *m_host.pins, *m_host.physics, e);
            const Math::Vector3f bagPos{ xf->position.x, terrainHeight(xf->position.x, xf->position.z) + 0.18f, xf->position.z };
            spawnLootBag(*world, *m_host.pins, *m_host.assets, *m_host.renderer, bagPos, "Hunter loot", contents);
        }
    }

    void PlaySession::presentQuestNotes()
    {
        World* world = m_host.world;
        if (!world)
            return;
        for (QuestNoteEvent& note : m_rt.quest.takeNotes())
        {
            const char* label = "QUEST";
            switch (note.kind)
            {
            case QuestNote::Started:
                label = "NEW QUEST";
                break;
            case QuestNote::NewObjective:
                label = "NEW OBJECTIVE";
                break;
            case QuestNote::ObjectiveDone:
                label = "OBJECTIVE COMPLETE";
                break;
            case QuestNote::QuestDone:
                label = "QUEST COMPLETE";
                break;
            }
            DE_LOG_INFO("Quest: {} - {}", label, note.text);
            if (note.kind == QuestNote::QuestDone && m_host.audio && m_host.assets)
            {
                InventoryComponent* inv = m_host.player.valid() ? world->get<InventoryComponent>(m_host.player) : nullptr;
                for (const QuestReward& reward : m_rt.quest.def().rewards)
                {
                    const ItemDef* def = findItemDef(reward.item);
                    if (!def)
                    {
                        DE_LOG_WARN("Quest: unknown reward item '{}'", reward.item);
                        continue;
                    }
                    if (inv)
                        grantInventoryItem(*world, *m_host.audio, *m_host.assets, listenerOf(m_host), *inv, ItemStack{ def, reward.count });
                }
            }
            if (m_host.onQuestNote)
                m_host.onQuestNote(m_host.user, note);
        }
    }

    void PlaySession::characterAnims(float dt)
    {
        World* world = m_host.world;
        if (!world)
            return;
        const Entity body = m_host.player;
        if (AnimGraphComponent* ag = body.valid() ? world->get<AnimGraphComponent>(body) : nullptr)
        {
            if (ag->graphDef)
            {
                AimLocomotion aim{};
                const HealthComponent*     hc    = world->get<HealthComponent>(body);
                const TransformComponent*  xf    = world->get<TransformComponent>(body);
                const PlayerMotorComponent* pmc  = world->get<PlayerMotorComponent>(body);
                const bool                 alive = !(hc && !hc->health.alive());
                const PlayerMotor*         motor = pmc ? &pmc->motor : nullptr;
                if (xf && alive && motor)
                    aim = aimLocomotion(motor->velocity(), xf->rotation);
                const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
                if (crouched)
                {
                    aim.strafe   = 0.0f;
                    aim.backward = false;
                }
                ag->graph.setBool("crouch", crouched);
                ag->graph.setBool("climb", motor && motor->state() == PlayerMoveState::WallRun);
                ag->graph.setBool("hang", motor && motor->state() == PlayerMoveState::Hang);
                const bool swimming   = motor && motor->state() == PlayerMoveState::Swimming;
                const bool standingUp = motor && motor->posture() == SwimPosture::Standing;
                if (swimming || standingUp)
                {
                    aim.strafe   = 0.0f;
                    aim.backward = false;
                    aim.lowerYaw = 0.0f;
                }
                ag->graph.setFloat("speed", aim.speed);
                ag->graph.setFloat("strafe", aim.strafe);
                ag->graph.setBool("backward", aim.backward);
                ag->graph.setBool("swimming", swimming);
                ag->graph.setBool("swimMove", motor && motor->swimPushing());
                if (m_rt.swimDiveEdge)
                    ag->graph.setTrigger("dive");
                if (m_rt.swimStandEdge)
                    ag->graph.setTrigger("standup");
                m_rt.swimDiveEdge  = false;
                m_rt.swimStandEdge = false;
                m_rt.lowerBodyYaw = approachAngle(m_rt.lowerBodyYaw, aim.lowerYaw, 10.0f, dt);
                ag->graph.player().setLowerBodyYaw(m_rt.lowerBodyYaw);
                ag->graph.player().setChargeWindup(m_rt.chargeWindup.attack, m_rt.chargeWindup.block, m_rt.chargeWindup.blockStrike);
                ag->graph.player().setShieldGuard(m_rt.offhand.shield.alpha());
                const char* clip      = ag->graph.player().clipName();
                const float playScale = (motor && motor->state() == PlayerMoveState::Dodge) ? motor->settings().dodgeAnimSpeed
                                                                                            : locomotionPlaybackScale(clip ? clip : "", aim.speed);
                ag->graph.setPlaybackScale(playScale);
            }
        }

        world->each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            AnimGraphComponent* ag = world->get<AnimGraphComponent>(e);
            if (!ag || !ag->graphDef)
                return;
            const HealthComponent* hp = world->get<HealthComponent>(e);
            const bool alive = hp && hp->health.alive();
            ag->graph.setBool("dead", !alive);
            TransformComponent* xf = world->get<TransformComponent>(e);
            if (alive && xf)
            {
                Math::Vector3f fwd = ai.forward;
                fwd.y              = 0.0f;
                if (fwd.MagnitudeSqrd() > 1.0e-6f)
                {
                    fwd.Normalize();
                    xf->rotation = Math::Quaternion::FromLookRotation(fwd, Math::Vector3f::Y_AXIS);
                }
            }
            LocomotionSample loco{};
            if (alive && xf)
                loco = locomotionSample(ai.planarVelocity, xf->rotation);
            ag->graph.setFloat("speed", loco.speed);
            ag->graph.setFloat("strafe", loco.strafe);
            const TagComponent* tag  = world->get<TagComponent>(e);
            const bool          wolf = tag && tag->name == "Wolf";
            const char*         clip = ag->graph.player().clipName();
            if (!wolf)
                ag->graph.setPlaybackScale(locomotionPlaybackScale(clip ? clip : "", loco.speed));
            if (alive && ai.meleeWindup > 0.0f)
            {
                if (wolf || ai.meleeWindup < 0.12f)
                {
                    if (!clip
                        || (std::strcmp(clip, "SwingSword") != 0 && std::strcmp(clip, "Bite") != 0 && std::strcmp(clip, "Die") != 0
                            && std::strcmp(clip, "Jump") != 0))
                        ag->graph.setTrigger("swing");
                }
                else if (!clip || (std::strcmp(clip, "SwingWindup") != 0 && std::strcmp(clip, "SwingSword") != 0))
                    ag->graph.setTrigger("windup");
            }
        });
    }

    void PlaySession::tickWorld(float dt)
    {
        pull();
        World* world = m_host.world;
        if (!world)
            return;
        if (WorldClockComponent* clock = m_host.session.valid() ? world->get<WorldClockComponent>(m_host.session) : nullptr)
            clock->playTimeSec += static_cast<double>(dt);

        lightAndExposure(dt);
        if (m_host.chaseReady && m_host.spawnUnit)
            tickSpawnerJobs(*world, dt, m_host.spawnUnit, m_host.user);
        if (m_host.chaseReady && m_host.crows && m_host.assets && m_host.pins && m_host.audio)
        {
            const Terrain::TerrainGrid* grid = (m_host.terrain && m_host.terrain->valid()) ? m_host.terrain : nullptr;
            simulateCrows(*world, grid, m_host.crows, m_host.assets, m_host.pins, m_host.audio, dt);
        }
        if (m_host.chaseReady && m_host.ai && m_host.terrain)
        {
            bindHunterGround(*m_host.ai, m_host.ground, m_host.terrain);
            m_host.ai->setJumpAttackHits(&PlaySession::onJumpHits, this);
            m_host.ai->setHunterCue(&PlaySession::onHunterCue, this);
            m_host.ai->tickHunters(*world, *m_host.terrain, m_rt.submerged, dt, m_host.player, m_host.cubeCount > 0 ? m_host.cubes : nullptr, m_host.cubeCount,
                                   m_host.sphereCount > 0 ? m_host.spheres : nullptr, m_host.sphereCount);
        }
        if (m_host.chaseReady && m_host.guardVoice && m_host.audio && m_host.assets)
        {
            if (const TransformComponent* pxf = m_host.player.valid() ? world->get<TransformComponent>(m_host.player) : nullptr)
                m_host.guardVoice->tick(*world, *m_host.audio, *m_host.assets, pxf->position, dt);
        }
        tickCombat(dt);
        syncArmorVisuals(*world);
        if (m_host.stepPhysics)
            m_host.stepPhysics(m_host.user, dt);
        Combat::harvestAndResolveDots(*world, m_combat);
        if (m_host.audio)
            tickStatusFx(*world, m_host.audio, m_host.assets);
        if (m_host.player.valid())
        {
            const int taken = tickHealthPacks(*world, m_host.player, dt);
            for (int i = 0; i < taken && m_host.audio && m_host.assets; ++i)
                playSoundCue(*world, *m_host.audio, *m_host.assets, m_host.player, "heal");
        }
        applyArmor();
        lootHunters();
        if (m_host.ai)
        {
            const AI::Walkability* walk = &m_host.ai->walkability();
            tickPower(*world, dt, walk, m_host.cloudTime, &m_rt.quest.tracker(), &PlaySession::onPowerClick, this);
        }
        else
            tickPower(*world, dt, nullptr, m_host.cloudTime, &m_rt.quest.tracker(), &PlaySession::onPowerClick, this);
        m_rt.quest.tick(*world, m_host.player);
        presentQuestNotes();
        tickCarry();
        tickAttachedLights(*world, m_host.sunElevation);
        characterAnims(dt);
        if (HealthComponent* hc = m_host.player.valid() ? world->get<HealthComponent>(m_host.player) : nullptr)
        {
            if (hc->health.alive())
                m_rt.spawnAge += dt;
        }
    }

    void PlaySession::tickHeldItems()
    {
        pull();
        World* world = m_host.world;
        if (!world)
            return;
        if (SwordComponent* sword = m_host.player.valid() ? world->get<SwordComponent>(m_host.player) : nullptr)
        {
            WeaponLoadout* weapons = nullptr;
            if (WeaponLoadoutComponent* wlc = world->get<WeaponLoadoutComponent>(m_host.player))
                weapons = (wlc->loadout) ? wlc->loadout.get() : nullptr;
            sword->shown = weapons && weapons->activeKind() == WeaponKind::Melee && ownsWeapon(WeaponKind::Melee);
        }
        syncSwordVisuals(*world);
        if (m_host.pins && m_host.assets && m_host.renderer)
            syncThrowableHands(*world, *m_host.pins, *m_host.assets, *m_host.renderer);
    }

} // namespace Dark
