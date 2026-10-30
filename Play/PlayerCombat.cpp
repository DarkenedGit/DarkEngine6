#include "Play/PlayerCombat.h"

#include "Play/PlaySession.h"

#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "Animation/AnimGraphComponent.h"
#include "Audio/SoundComponents.h"
#include "Character/ArmorView.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/SkillComponent.h"
#include "Character/SkillXp.h"
#include "Combat/ArmorComponent.h"
#include "Combat/ArmorPieces.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Input/Input.h"
#include "Math/MathHelper.h"
#include "Play/HunterCue.h"
#include "Play/ImpactFx.h"
#include "Play/ThrowableHost.h"
#include "Play/WeaponFire.h"
#include "Render/Camera3D.h"
#include "Terrain/TerrainGrid.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <cmath>
#include <string>

namespace Dark
{
    namespace
    {
        Entity listenerOf(const PlayHost& host)
        {
            return host.cameraEntity.valid() ? host.cameraEntity : host.player;
        }

        ImpactFxHost impactOf(PlayHost& host, PlayRuntime& runtime)
        {
            ImpactFxHost fx;
            fx.world       = host.world;
            fx.scene       = host.scene;
            fx.renderer    = host.renderer;
            fx.terrain     = host.terrain;
            fx.ground      = host.ground;
            fx.contactGrid = (host.terrain && host.terrain->valid()) ? host.terrain : nullptr;
            fx.targets     = &runtime.targets;
            return fx;
        }

        bool playSoundCueFallback(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* primary, const char* fallback)
        {
            if (playSoundCue(world, audio, assets, e, primary))
                return true;
            return fallback && playSoundCue(world, audio, assets, e, fallback);
        }

        void faceTarget(PlayHost& host, PlayRuntime& runtime, Entity target)
        {
            World* world = host.world;
            if (!world)
                return;
            const TransformComponent* xf  = host.player.valid() ? world->get<TransformComponent>(host.player) : nullptr;
            const TransformComponent* txf = world->get<TransformComponent>(target);
            if (!xf || !txf)
                return;
            const float dx = txf->position.x - xf->position.x;
            const float dz = txf->position.z - xf->position.z;
            if (dx * dx + dz * dz < 1.0e-4f)
                return;
            runtime.faceYaw  = std::atan2f(dx, dz);
            runtime.faceLeft = 0.15f;
        }

        void onDefended(PlayHost& host, PlayRuntime& runtime, Entity attacker, bool parried)
        {
            World* world = host.world;
            if (!world || !host.audio || !host.assets)
                return;
            faceTarget(host, runtime, attacker);
            runtime.shieldBump = 0.2f;
            runtime.hitStop    = parried ? 0.1f : 0.05f;
            runtime.shake      = parried ? 0.09f : 0.06f;
            if (host.onDefenseText)
                host.onDefenseText(host.user, parried);
            playSoundCue(*world, *host.audio, *host.assets, host.player, "impact");
            if (!parried || !attacker.valid() || !host.ai || !world->has<AiAgentComponent>(attacker))
                return;
            const TransformComponent* axf = world->get<TransformComponent>(attacker);
            const TransformComponent* pxf = host.player.valid() ? world->get<TransformComponent>(host.player) : nullptr;
            host.ai->interruptAttack(*world, attacker, 1.2f);
            if (axf && pxf)
                host.ai->applyHunterHitReaction(*world, attacker, Math::Vector3f{ axf->position.x - pxf->position.x, 0.0f, axf->position.z - pxf->position.z });
            if (AnimGraphComponent* aag = world->get<AnimGraphComponent>(attacker))
                aag->graph.setTrigger("hit");
        }

        WeaponLoadout* loadoutOf(World& world, Entity body)
        {
            WeaponLoadoutComponent* wlc = body.valid() ? world.get<WeaponLoadoutComponent>(body) : nullptr;
            return (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
        }

    } // namespace

    bool playerOwnsWeapon(const PlayHost& host, WeaponKind kind)
    {
        World* world = host.world;
        if (!world || !host.player.valid())
            return kind != WeaponKind::Throwable;
        if (kind == WeaponKind::Throwable)
        {
            const ThrowableBeltComponent* belt = world->get<ThrowableBeltComponent>(host.player);
            return belt && belt->belt.any();
        }
        const InventoryComponent* inv = world->get<InventoryComponent>(host.player);
        const char*               id  = kind == WeaponKind::Melee ? "sword" : "rifle";
        return !inv || hasItem(*inv, id);
    }

    WeaponWorldQuery makePlayerWeaponQuery(PlayHost& host, PlayRuntime& runtime, const PlayerCombatHooks& hooks)
    {
        WeaponWorldQuery q{};
        if (!host.world)
            return q;
        runtime.targets.gather(*host.world, host.player);
        q.terrainUser    = hooks.terrainUser;
        q.raycastTerrain = hooks.raycastTerrain;
        q.heightAt       = hooks.terrainHeight;
        runtime.targets.bind(q);
        q.maxRange = 90.0f;
        return q;
    }

    void tickPlayerThrows(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks)
    {
        if (!host.world)
            return;
        ThrowableFlightHost flight;
        flight.world    = host.world;
        flight.pins     = host.pins;
        flight.assets   = host.assets;
        flight.renderer = host.renderer;
        flight.physics  = host.physics;
        flight.audio    = host.audio;
        flight.ai       = host.ai;
        flight.solids.hasTerrain     = host.terrain != nullptr;
        flight.solids.terrainUser    = hooks.terrainUser;
        flight.solids.raycastTerrain = hooks.raycastTerrain;
        flight.solids.heightAt       = hooks.terrainHeight;
        flight.solids.cubes          = host.cubeCount > 0 ? host.cubes : nullptr;
        flight.solids.cubeCount      = flight.solids.cubes ? host.cubeCount : 0;
        flight.solids.spheres        = host.sphereCount > 0 ? host.spheres : nullptr;
        flight.solids.sphereCount    = flight.solids.spheres ? host.sphereCount : 0;
        tickThrowableFlightHost(runtime.throws, dt, flight);
    }

    bool releasePlayerThrow(PlayHost& host, PlayRuntime& runtime, bool charged)
    {
        World* world = host.world;
        if (!world || !host.camera || !host.player.valid())
            return false;
        WeaponLoadoutComponent* wlc     = world->get<WeaponLoadoutComponent>(host.player);
        WeaponLoadout*          weapons = loadoutOf(*world, host.player);
        if (!weapons || weapons->activeKind() != WeaponKind::Throwable)
            return false;
        ThrowableBelt&            belt = throwableBelt(*world, host.player);
        const TransformComponent* xf   = world->get<TransformComponent>(host.player);
        const WeaponFireRequest   req  = makeCameraFireRequest(host.camera->GetPosition(), host.camera->GetLook(), xf ? &xf->position : nullptr, 1.0f);
        std::string               thrown;
        if (!throwFromLoadout(runtime.throws, *weapons, belt, req, host.player, charged, &thrown))
            return false;
        if (wlc)
            wlc->slot = weapons->slot();
        DE_LOG_INFO("Player: threw {}", thrown);
        return true;
    }

    void tickPlayerLoadout(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks)
    {
        World* world = host.world;
        if (!world)
            return;
        WeaponLoadout*         weapons = loadoutOf(*world, host.player);
        const WeaponWorldQuery query   = makePlayerWeaponQuery(host, runtime, hooks);
        if (weapons)
            weapons->tick(dt, query);
    }

    bool firePlayerLoadout(PlayHost& host, PlayRuntime& runtime, bool charged, const PlayerCombatHooks& hooks)
    {
        World* world = host.world;
        if (!world || !host.camera || !host.audio || !host.assets)
            return false;
        const Entity              body = host.player;
        const TransformComponent* xf   = body.valid() ? world->get<TransformComponent>(body) : nullptr;
        WeaponFireRequest         req  = makeCameraFireRequest(host.camera->GetPosition(), host.camera->GetLook(), xf ? &xf->position : nullptr,
                                                               charged ? runtime.chargeSettings.attackDamageScale : 1.0f);
        WeaponLoadout*            wFire = loadoutOf(*world, body);
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
        if (melee && runtime.blockRecoil > 0.0f)
            return false;

        const WeaponWorldQuery query = makePlayerWeaponQuery(host, runtime, hooks);
        if (!wFire || !playerOwnsWeapon(host, wFire->activeKind()) || !wFire->fire(req, query))
            return false;
        if (skill)
            noteShotXp(*skill, true);
        if (melee)
        {
            runtime.comboStep   = runtime.comboWindow > 0.0f ? 1 - runtime.comboStep : 0;
            runtime.comboCount  = runtime.comboWindow > 0.0f ? runtime.comboCount + 1 : 1;
            runtime.comboWindow = 0.9f;
            runtime.swingLock   = 0.45f;
        }
        if (AnimGraphComponent* ag = body.valid() ? world->get<AnimGraphComponent>(body) : nullptr)
        {
            if (melee)
                ag->graph.setTrigger(runtime.comboStep ? "swing2" : "swing");
            else if (wFire->activeKind() == WeaponKind::Projectile)
                ag->graph.setTrigger("shoot");
        }
        if (melee)
        {
            runtime.swingCharged = charged;
            playSoundCue(*world, *host.audio, *host.assets, body, "swing");
        }
        else if (wFire && wFire->activeKind() == WeaponKind::Projectile)
        {
            if (host.onMuzzle)
                host.onMuzzle(host.user);
            const RecoilKick kick = wFire->projectile().takeRecoil();
            runtime.lookYaw += kick.yaw;
            runtime.lookPitch += kick.pitch;
            runtime.lookYaw   = Math::WrapPi(runtime.lookYaw);
            runtime.lookPitch = Math::Clamp(runtime.lookPitch, -0.96f, 0.96f);
            if (hooks.refreshCamera)
                hooks.refreshCamera(hooks.cameraUser);
        }
        return true;
    }

    void switchPlayerWeapons(PlayHost& host)
    {
        World* world = host.world;
        Input* input = host.input;
        if (!world || !input || !host.audio || !host.assets || !host.player.valid())
            return;
        WeaponLoadoutComponent* wlc = world->get<WeaponLoadoutComponent>(host.player);
        WeaponLoadout*          w   = loadoutOf(*world, host.player);
        if (!w)
            return;
        const Entity listener = listenerOf(host);
        if (input->actionPressed("weapon_1") && playerOwnsWeapon(host, WeaponKind::Melee) && w->selectMelee())
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *host.audio, *host.assets, listener, "click");
            DE_LOG_INFO("Player: weapon melee");
        }
        if (input->actionPressed("weapon_2") && playerOwnsWeapon(host, WeaponKind::Projectile) && w->selectProjectile())
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *host.audio, *host.assets, listener, "click");
            DE_LOG_INFO("Player: weapon {}", w->projectile().name());
        }
        if (input->actionPressed("weapon_3") && w->selectSlot(2, throwableBelt(*world, host.player)))
        {
            wlc->slot = w->slot();
            playSoundCue(*world, *host.audio, *host.assets, listener, "click");
            const std::string held(throwableBelt(*world, host.player).hand());
            DE_LOG_INFO("Player: weapon {}", held);
        }
    }

    void onPlayerWeaponHit(PlayHost& host, PlayRuntime& runtime, const WeaponHit& hit)
    {
        World* world = host.world;
        if (!world || !host.audio || !host.assets)
            return;
        spawnWeaponImpact(impactOf(host, runtime), hit);
        if (!hit.hitTarget)
            return;
        Entity victim = hit.targetEntity;
        if (!victim.valid())
            victim = runtime.targets.entityAt(hit.targetIndex);
        if (!victim.valid() || !world->alive(victim))
            return;
        HealthComponent*          hp         = world->get<HealthComponent>(victim);
        const bool                wasAlive   = hp && hp->health.alive();
        const TransformComponent* attackerXf = host.player.valid() ? world->get<TransformComponent>(host.player) : nullptr;
        const Math::Vector3f      knock      = Combat::partKnockbackDirection(hit.part, attackerXf, hit.point, hit.direction);
        auto                      spendPieces = [&](float damage) {
            Combat::ArmorBreak broke{};
            if (Combat::ArmorPiecesComponent* armor = world->get<Combat::ArmorPiecesComponent>(victim))
            {
                if (const TransformComponent* vxf = world->get<TransformComponent>(victim))
                {
                    const bool hasFlat = Combat::usesFlatArmor(world->get<Combat::ArmorComponent>(victim));
                    damage = Combat::absorbArmorHit(*armor, vxf->position, vxf->rotation, hit.point, hit.direction, damage, broke, hit.part, hasFlat);
                }
            }
            if (broke.index >= 0 && host.physics)
                knockOffArmor(*world, *host.physics, victim, broke);
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
            if (!host.ai)
                return;
            const TransformComponent* playerXf  = host.player.valid() ? world->get<TransformComponent>(host.player) : nullptr;
            const Math::Vector3f      playerPos = playerXf ? playerXf->position : Math::Vector3f{};
            host.ai->forceAlert(*world, victim, playerPos);
            using MeleeResponse          = AiSystem::MeleeResponse;
            const bool          melee    = hit.weapon == WeaponKind::Melee;
            const MeleeResponse response = melee ? host.ai->respondToMelee(*world, victim, playerPos) : MeleeResponse::Stagger;
            if (response == MeleeResponse::Blocked)
            {
                faceTarget(host, runtime, victim);
                runtime.hitStop     = 0.06f;
                runtime.shake       = 0.07f;
                runtime.blockRecoil = 0.55f;
                playSoundCueAt(*world, *host.audio, *host.assets, victim, "impact", hit.point);
                if (AnimGraphComponent* vag = world->get<AnimGraphComponent>(victim))
                    vag->graph.setTrigger("block");
                if (AnimGraphComponent* pag = host.player.valid() ? world->get<AnimGraphComponent>(host.player) : nullptr)
                    pag->graph.setTrigger("hit");
                host.ai->onHunterAttacked(*world, victim, playerPos);
                return;
            }
            const float damage  = spendPieces(hit.damage);
            const bool  damaged = host.ai->applyHunterDamage(*world, victim, damage);
            noteIfAlive();
            if (!damaged)
                return;
            if (response == MeleeResponse::Stagger)
                host.ai->applyHunterHitReaction(*world, victim, knock);
            if (melee)
            {
                faceTarget(host, runtime, victim);
                const HealthComponent* after = world->get<HealthComponent>(victim);
                if (AnimGraphComponent* vag = world->get<AnimGraphComponent>(victim); vag && after && after->health.alive() && response == MeleeResponse::Stagger)
                    vag->graph.setTrigger("hit");
                const bool heavy = runtime.swingCharged || (hp && !hp->health.alive());
                runtime.hitStop  = heavy ? 0.12f : 0.07f;
                playSoundCueAt(*world, *host.audio, *host.assets, victim, "impact", hit.point);
            }
            playSoundCueAt(*world, *host.audio, *host.assets, victim, "pain", hit.point);
            playSoundCueAt(*world, *host.audio, *host.assets, victim, "grunt", hit.point);
            host.ai->onHunterAttacked(*world, victim, playerPos);
            if (host.onBloodBurst)
                host.onBloodBurst(host.user, hit.point);
            spawnLivingBloodDecal(impactOf(host, runtime), victim, hit.point, hit.normal);
            hp = world->get<HealthComponent>(victim);
            if (wasAlive && hp && !hp->health.alive())
            {
                spawnDeathBloodDecal(impactOf(host, runtime), hit.point, hit.normal);
                if (!impactDecalsReady(impactOf(host, runtime)) && host.onBloodSplat)
                    host.onBloodSplat(host.user, hit.point.x, hit.point.z);
                host.ai->onHunterKilled(*world, victim);
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
        playSoundCueAt(*world, *host.audio, *host.assets, victim, "pain", hit.point);
    }

    void resolvePlayerJumpHits(PlayHost& host, PlayRuntime& runtime, Combat::CombatSystem& combat, const Combat::DamageEvent* events, int count)
    {
        World* world = host.world;
        if (count <= 0 || !events || !world || !host.audio || !host.assets)
            return;

        struct Snap
        {
            Entity e{};
            float  hp       = 0.0f;
            bool   wasAlive = false;
            bool   hunter   = false;
        };
        Snap      snaps[8]{};
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
        Combat::resolveJumpAttackEvents(*world, combat, events, count, count <= 8 ? results : nullptr);

        const Entity player = host.player;
        for (int i = 0; i < n; ++i)
        {
            if (player.valid() && snaps[i].e.id() == player.id() && (results[i].blocked || results[i].parried))
                onDefended(host, runtime, events[i].source, results[i].parried);
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
                    runtime.shake = 0.08f;
                if (AnimGraphComponent* ag = world->get<AnimGraphComponent>(snaps[i].e))
                    ag->graph.setTrigger("hit");
            }
            if (!snaps[i].hunter)
            {
                if (snaps[i].e.id() == player.id())
                    playSoundCue(*world, *host.audio, *host.assets, snaps[i].e, "pain");
                continue;
            }
            playSoundCueAt(*world, *host.audio, *host.assets, snaps[i].e, "pain", events[i].hitPoint);
            playSoundCueAt(*world, *host.audio, *host.assets, snaps[i].e, "grunt", events[i].hitPoint);
            if (host.ai)
                host.ai->onHunterAttacked(*world, snaps[i].e, playerPos);
            if (host.onBloodBurst)
                host.onBloodBurst(host.user, events[i].hitPoint);
            spawnLivingBloodDecal(impactOf(host, runtime), snaps[i].e, events[i].hitPoint, -events[i].hitDir);
            if (killed)
            {
                spawnDeathBloodDecal(impactOf(host, runtime), events[i].hitPoint, Math::Vector3f(0.0f, 0.0f, 1.0f));
                if (!impactDecalsReady(impactOf(host, runtime)) && host.onBloodSplat)
                    host.onBloodSplat(host.user, events[i].hitPoint.x, events[i].hitPoint.z);
                if (host.ai)
                    host.ai->onHunterKilled(*world, snaps[i].e);
            }
        }
    }

    bool tickPlayerJumpAttack(PlayHost& host, PlayRuntime& runtime, Combat::CombatSystem& combat, Combat::JumpAttack& jump, Combat::PoiseComponent* poise, Entity body,
                              const Math::Vector3f& position, const Math::Vector3f& lookFlat, bool splashed, bool landed, bool alive, float dt, const PlayerCombatHooks& hooks)
    {
        World* world = host.world;
        if (!world)
            return false;
        jump.tick(dt);
        bool skipLandCue = false;
        if (alive)
        {
            if (splashed)
                jump.onSplashed();
            else if (landed)
            {
                jump.onLanded(position);
                if (jump.phase() == Combat::JumpAttackPhase::Pound)
                {
                    Combat::DamageEvent poundEvents[8]{};
                    const int           n = jump.tryPound(makePlayerWeaponQuery(host, runtime, hooks), position, poundEvents, 8);
                    DE_LOG_INFO("Player: ground pound");
                    if (host.audio && host.assets && playSoundCue(*world, *host.audio, *host.assets, body, "pound"))
                        skipLandCue = true;
                    resolvePlayerJumpHits(host, runtime, combat, poundEvents, n);
                }
            }
            else if (jump.phase() == Combat::JumpAttackPhase::Leap)
            {
                Combat::DamageEvent connectEv{};
                if (jump.tryConnect(makePlayerWeaponQuery(host, runtime, hooks), position, lookFlat, connectEv))
                {
                    DE_LOG_INFO("Player: pounce connect");
                    if (host.audio && host.assets)
                        playSoundCueFallback(*world, *host.audio, *host.assets, body, "pounce", "impact");
                    resolvePlayerJumpHits(host, runtime, combat, &connectEv, 1);
                }
            }
        }
        if (poise)
            poise->hyperArmor = jump.inAirCommit();
        return skipLandCue;
    }

    void tickPlayerAttack(PlayHost& host, PlayRuntime& runtime, float dt, const PlayerCombatHooks& hooks)
    {
        World* world = host.world;
        Input* input = host.input;
        if (!world || !input || !host.audio || !host.assets)
            return;

        const Entity                   body   = host.player;
        const TransformComponent*      xf     = body.valid() ? world->get<TransformComponent>(body) : nullptr;
        JumpAttackComponent*           jac    = body.valid() ? world->get<JumpAttackComponent>(body) : nullptr;
        Combat::JumpAttack*            jump   = jac ? &jac->jump : nullptr;
        Combat::StatusEffectComponent* status = body.valid() ? world->get<Combat::StatusEffectComponent>(body) : nullptr;
        HitReaction*                   hitRx  = nullptr;
        if (HitReactionComponent* hr = body.valid() ? world->get<HitReactionComponent>(body) : nullptr)
            hitRx = &hr->hit;
        PlayerMotor* motor = nullptr;
        if (PlayerMotorComponent* pmc = body.valid() ? world->get<PlayerMotorComponent>(body) : nullptr)
            motor = &pmc->motor;
        WeaponLoadout* weapons     = loadoutOf(*world, body);
        const bool     ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
        const bool     jumpBusy    = jump && jump->busy();

        if (runtime.jumpAttackBuffer > 0.0f)
            runtime.jumpAttackBuffer = Math::Max(0.0f, runtime.jumpAttackBuffer - dt);

        const bool attackPressed = input->actionPressed("attack") || (host.allowMouseButtons && input->actionMousePressed("attack"));
        const bool swimming      = motor && motor->state() == PlayerMoveState::Swimming;
        const bool airborne      = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
        const bool fireQuick     = runtime.fireQuick;
        const bool fireCharged   = runtime.fireCharged;
        runtime.fireQuick        = false;
        runtime.fireCharged      = false;
        WeaponAttackInput attackQuery{};
        attackQuery.airborne      = airborne;
        attackQuery.swimming      = swimming;
        attackQuery.attackPressed = attackPressed;
        attackQuery.jumpBuffered  = runtime.jumpAttackBuffer > 0.0f;
        attackQuery.fireQuick     = fireQuick;
        attackQuery.fireCharged   = fireCharged;
        attackQuery.ccLocked      = ccLocked;
        attackQuery.jumpBusy      = jumpBusy;
        if (weapons)
            classifyWeaponAttack(attackQuery, weapons->activeKind());
        const WeaponAttackDecision decision = decideWeaponAttack(attackQuery);

        auto noteSenses = [&]() {
            if (SkillComponent* sk = body.valid() ? world->get<SkillComponent>(body) : nullptr)
            {
                const bool foreign = host.audio->liveForeignSpatialVoices(body.id()) > 0;
                notePlayerSenseXp(*sk, sk->seeArmed, runtime.offhand.lightOn, foreign, dt);
            }
        };

        if (ccLocked || jumpBusy)
        {
            if (ccLocked)
                runtime.jumpAttackBuffer = 0.0f;
            noteSenses();
            return;
        }

        if (swimming)
        {
            runtime.jumpAttackBuffer = 0.0f;
            if (decision.throwOnRelease)
                releasePlayerThrow(host, runtime, fireCharged);
            else if (decision.fireLoadoutOnRelease)
                firePlayerLoadout(host, runtime, fireCharged, hooks);
            noteSenses();
            return;
        }

        auto tryBeginJumpAttack = [&]() -> bool {
            if (!jump || !xf || !motor)
                return false;
            const float groundY     = hooks.terrainHeight ? hooks.terrainHeight(hooks.terrainUser, xf->position.x, xf->position.z) : 0.0f;
            const float heightAbove = xf->position.y - groundY - motor->settings().groundOffset;
            const bool  airOk       = heightAbove >= jump->def().minHeight || motor->airTime() >= jump->def().minAirTime;
            if (!airOk)
                return false;
            Math::Vector3f lookFlat{ host.camera ? host.camera->GetLook().x : 0.0f, 0.0f, host.camera ? host.camera->GetLook().z : 1.0f };
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
                    runtime.jumpAttackBuffer = 0.0f;
                else if (attackPressed)
                    runtime.jumpAttackBuffer = kPlayJumpAttackBuffer;
            }
            if (decision.throwOnRelease)
            {
                runtime.jumpAttackBuffer = 0.0f;
                releasePlayerThrow(host, runtime, fireCharged);
            }
            noteSenses();
            return;
        }

        if (decision.throwOnRelease)
        {
            runtime.jumpAttackBuffer = 0.0f;
            releasePlayerThrow(host, runtime, fireCharged);
        }
        else if (decision.fireLoadoutOnRelease)
        {
            runtime.jumpAttackBuffer = 0.0f;
            firePlayerLoadout(host, runtime, fireCharged, hooks);
        }
        else if (runtime.jumpAttackBuffer > 0.0f)
        {
            runtime.jumpAttackBuffer = 0.0f;
            firePlayerLoadout(host, runtime, false, hooks);
        }
        noteSenses();
    }

} // namespace Dark
