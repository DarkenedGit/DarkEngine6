#include "Particles/StatusFxDriver.h"

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Character/HealthComponent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/StatusId.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleMaterials.h"
#include "Particles/StatusFxTag.h"
#include "Particles/StatusParticlePresets.h"

namespace Dark
{
    using Combat::IStatusFx;
    using Combat::StatusDef;
    using Combat::StatusEffectComponent;
    using Combat::StatusFxEvent;
    using Combat::StatusFxOp;
    using Combat::StatusId;
    using Combat::drainStatusFx;
    using Combat::statusDef;

    namespace
    {
        constexpr int            kMaxStatusFxEntities = 64;
        const Math::Vector3f     kFollowOffset{ 0.0f, 1.1f, 0.0f };

        Entity findStatusFx(World& world, Entity pawn, uint8_t statusId)
        {
            Entity found{};
            world.each<StatusFxTag>([&](Entity e, StatusFxTag& tag) {
                if (!found.valid() && tag.follow == pawn && tag.statusId == statusId)
                    found = e;
            });
            return found;
        }

        int countStatusFx(World& world)
        {
            int n = 0;
            world.each<StatusFxTag>([&](Entity, StatusFxTag&) { ++n; });
            return n;
        }

        Entity oldestStatusFx(World& world)
        {
            Entity oldest{};
            world.each<StatusFxTag>([&](Entity e, StatusFxTag&) {
                if (!oldest.valid())
                    oldest = e;
            });
            return oldest;
        }

        bool containsEntity(const Entity* list, int n, Entity e)
        {
            for (int i = 0; i < n; ++i)
            {
                if (list[i] == e)
                    return true;
            }
            return false;
        }

        void queueEntity(Entity* list, int& n, int cap, Entity e)
        {
            if (!e.valid() || n >= cap || containsEntity(list, n, e))
                return;
            list[n++] = e;
        }

        void stopLoopVoice(World& world, Audio::AudioSystem* audio, Entity fx)
        {
            if (!audio)
                return;
            SoundEmitterComponent* se = world.get<SoundEmitterComponent>(fx);
            if (!se)
                return;
            if (se->voice)
                audio->stop(se->voice);
            se->voice   = 0;
            se->play    = false;
            se->playing = false;
        }

        void destroyCollected(World& world, Audio::AudioSystem* audio, Entity* list, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const Entity e = list[i];
                if (!e.valid() || !world.alive(e))
                    continue;
                if (ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(e))
                {
                    if (pe->runtime)
                        pe->runtime->stop(true);
                }
                stopLoopVoice(world, audio, e);
                world.destroyEntity(e);
            }
        }

        void setAnimBool(World& world, Entity pawn, const char* name, bool v)
        {
            if (!name || !name[0])
                return;
            if (AnimGraphComponent* ag = world.get<AnimGraphComponent>(pawn))
                ag->graph.setBool(name, v);
        }

        void fireAnimTrigger(World& world, Entity pawn, const char* name)
        {
            if (!name || !name[0])
                return;
            if (AnimGraphComponent* ag = world.get<AnimGraphComponent>(pawn))
                ag->graph.setTrigger(name);
        }

        void clearCatalogAnimBools(World& world, Entity pawn)
        {
            for (int i = 1; i < Combat::kStatusIdCount; ++i)
            {
                const StatusDef* def = statusDef(static_cast<StatusId>(i));
                if (def)
                    setAnimBool(world, pawn, def->animBool, false);
            }
        }

        void bindFollowTransform(World& world, Entity fx, const TransformComponent& followXf)
        {
            const Math::Vector3f pos = followXf.position + kFollowOffset;
            if (TransformComponent* xf = world.get<TransformComponent>(fx))
            {
                xf->position = pos;
                xf->rotation = followXf.rotation;
            }
            if (ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx))
            {
                ensureParticleRuntime(*pe);
                pe->runtime->setTransform(pos, followXf.rotation);
            }
        }

        void configureEmitter(World& world, AssetManager* assets, Entity fx, const StatusDef& def)
        {
            ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx);
            if (!pe)
            {
                ParticleEmitterComponent created{};
                world.emplace<ParticleEmitterComponent>(fx, std::move(created));
                pe = world.get<ParticleEmitterComponent>(fx);
            }
            if (!pe)
                return;

            pe->desc    = statusParticlePreset(def.particle);
            pe->playing = true;
            pe->matAssetID = NULL_ASSET;
            if (assets)
            {
                if (AssetRef<Material> mat = internParticleSpriteMaterial(*assets, false))
                    pe->matAssetID = mat->id;
            }
            if (pe->runtime)
                pe->runtime->setDesc(pe->desc);
            else
                ensureParticleRuntime(*pe);
            pe->runtime->play();
        }

        Entity spawnOrReuseFx(World& world, AssetManager* assets, Entity pawn, uint8_t statusId, const StatusDef& def)
        {
            Entity fx = findStatusFx(world, pawn, statusId);
            if (!fx.valid())
            {
                if (countStatusFx(world) >= kMaxStatusFxEntities)
                {
                    fx = oldestStatusFx(world);
                    static bool s_loggedCap = false;
                    if (!s_loggedCap)
                    {
                        s_loggedCap = true;
                        DE_LOG_WARN("StatusFx: cap {} reached, reusing oldest", kMaxStatusFxEntities);
                    }
                    if (fx.valid())
                    {
                        if (ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx))
                        {
                            if (pe->runtime)
                                pe->runtime->stop(true);
                        }
                    }
                }
                if (!fx.valid())
                {
                    fx = world.createEntity();
                    world.emplace<TransformComponent>(fx);
                    world.emplace<StatusFxTag>(fx);
                    ParticleEmitterComponent pe{};
                    world.emplace<ParticleEmitterComponent>(fx, std::move(pe));
                }
            }

            if (StatusFxTag* tag = world.get<StatusFxTag>(fx))
            {
                tag->follow   = pawn;
                tag->statusId = statusId;
            }
            configureEmitter(world, assets, fx, def);
            if (const TransformComponent* xf = world.get<TransformComponent>(pawn))
                bindFollowTransform(world, fx, *xf);
            return fx;
        }

        void playApplyCue(World& world, Audio::AudioSystem* audio, AssetManager* assets, Entity pawn, const StatusDef& def)
        {
            if (!audio || !assets || !def.applyCue || !def.applyCue[0])
                return;
            if (playSoundCue(world, *audio, *assets, pawn, def.applyCue))
                return;
            static bool s_loggedMissing = false;
            if (!s_loggedMissing)
            {
                s_loggedMissing = true;
                DE_LOG_WARN("StatusFx: missing applyCue '{}'", def.applyCue);
            }
        }

        void collectFollow(World& world, Entity pawn, Entity* list, int& n)
        {
            world.each<StatusFxTag>([&](Entity e, StatusFxTag& tag) {
                if (tag.follow == pawn)
                    queueEntity(list, n, kMaxStatusFxEntities, e);
            });
        }
    } // namespace

    void tickStatusFx(World& world, Audio::AudioSystem* audio, AssetManager* assets, IStatusFx extra)
    {
        Entity destroyList[kMaxStatusFxEntities]{};
        int    destroyN = 0;

        world.each<StatusEffectComponent>([&](Entity pawn, StatusEffectComponent& st) {
            StatusFxEvent evs[StatusEffectComponent::kMaxFxEvents]{};
            const int     n = drainStatusFx(st, evs, StatusEffectComponent::kMaxFxEvents);
            for (int i = 0; i < n; ++i)
            {
                const StatusFxEvent& ev = evs[i];
                if (extra.fn)
                    extra.fn(extra.user, pawn, ev);

                if (ev.op == StatusFxOp::Cleared)
                {
                    collectFollow(world, pawn, destroyList, destroyN);
                    clearCatalogAnimBools(world, pawn);
                    continue;
                }

                const StatusDef* def = statusDef(static_cast<StatusId>(ev.statusId));
                switch (ev.op)
                {
                case StatusFxOp::Applied:
                    if (!def)
                        break;
                    if (def->particle != Combat::StatusParticlePreset::None)
                        spawnOrReuseFx(world, assets, pawn, ev.statusId, *def);
                    playApplyCue(world, audio, assets, pawn, *def);
                    setAnimBool(world, pawn, def->animBool, true);
                    fireAnimTrigger(world, pawn, def->animTrigger);
                    break;
                case StatusFxOp::Refreshed:
                    break;
                case StatusFxOp::Ticked:
                {
                    if (!def)
                        break;
                    Entity fx = findStatusFx(world, pawn, ev.statusId);
                    if (!fx.valid())
                        break;
                    ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx);
                    if (!pe)
                        break;
                    ensureParticleRuntime(*pe);
                    if (static_cast<StatusId>(ev.statusId) == StatusId::Bleed)
                        pe->runtime->emitBurst(6);
                    else if (static_cast<StatusId>(ev.statusId) == StatusId::Ignite)
                        pe->runtime->emitBurst(4);
                    break;
                }
                case StatusFxOp::Expired:
                {
                    Entity fx = findStatusFx(world, pawn, ev.statusId);
                    if (fx.valid())
                    {
                        if (ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx))
                        {
                            if (pe->runtime)
                                pe->runtime->stop(true);
                        }
                        queueEntity(destroyList, destroyN, kMaxStatusFxEntities, fx);
                    }
                    if (def)
                        setAnimBool(world, pawn, def->animBool, false);
                    break;
                }
                default:
                    break;
                }
            }
        });

        world.each<StatusFxTag>([&](Entity e, StatusFxTag& tag) {
            if (containsEntity(destroyList, destroyN, e))
                return;
            if (!tag.follow.valid() || !world.alive(tag.follow))
            {
                queueEntity(destroyList, destroyN, kMaxStatusFxEntities, e);
                return;
            }
            const TransformComponent* xf = world.get<TransformComponent>(tag.follow);
            if (!xf)
            {
                queueEntity(destroyList, destroyN, kMaxStatusFxEntities, e);
                return;
            }
            bindFollowTransform(world, e, *xf);

            ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(e);
            if (!pe)
                return;
            ensureParticleRuntime(*pe);
            if (const HealthComponent* hp = world.get<HealthComponent>(tag.follow); hp && hp->health.dead())
                pe->runtime->stop(false);
            else if (pe->playing && !pe->runtime->isPlaying())
                pe->runtime->play();
        });

        destroyCollected(world, audio, destroyList, destroyN);
    }

    void clearStatusFx(World& world, Audio::AudioSystem* audio, Entity pawn)
    {
        Entity list[kMaxStatusFxEntities]{};
        int    n = 0;
        collectFollow(world, pawn, list, n);
        destroyCollected(world, audio, list, n);
        clearCatalogAnimBools(world, pawn);
    }

    void clearAllStatusFx(World& world, Audio::AudioSystem* audio)
    {
        Entity follows[kMaxStatusFxEntities]{};
        int    followN = 0;
        Entity list[kMaxStatusFxEntities]{};
        int    n = 0;
        world.each<StatusFxTag>([&](Entity e, StatusFxTag& tag) {
            queueEntity(list, n, kMaxStatusFxEntities, e);
            queueEntity(follows, followN, kMaxStatusFxEntities, tag.follow);
        });
        for (int i = 0; i < followN; ++i)
            clearCatalogAnimBools(world, follows[i]);
        world.each<StatusEffectComponent>([&](Entity pawn, StatusEffectComponent&) { clearCatalogAnimBools(world, pawn); });
        destroyCollected(world, audio, list, n);
    }

} // namespace Dark
