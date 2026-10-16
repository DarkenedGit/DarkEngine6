#include "Play/CharacterAttach.h"

#include "Play/CharacterAudio.h"

#include "AI/HsmGraph.h"
#include "AI/HsmGraphComponent.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/SkillXp.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/PoiseComponent.h"
#include "Combat/Shield.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Render/GpuUpload.h"
#include "Scene/EntityMaster.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/MeleeWeapon.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Dark
{

    namespace
    {

        bool aliveEntity(const World& world, Entity e)
        {
            return e.valid() && world.alive(e);
        }

    } // namespace

    ContentModelResult attachContentModel(const CharacterHost& host, Entity e, const char* gltfPath, ModelAttach mode)
    {
        ContentModelResult result;
        if (!aliveEntity(host.world, e) || !gltfPath || gltfPath[0] == '\0')
            return result;

        AssetRef<Model> model = loadAndUploadModel(host.renderer, host.assets, gltfPath);
        if (!model || !model->valid())
        {
            DE_LOG_WARN("Play: model '{}' failed to load", gltfPath);
            return result;
        }

        if (!model->skeleton() && mode == ModelAttach::AnimatedOnly)
        {
            DE_LOG_WARN("Play: '{}' has no skeleton", gltfPath);
            return result;
        }

        ModelComponent mc{};
        mc.modelAssetID = model->id;
        mc.castShadow   = model->hasOpaque();
        setModelComponent(host.world, host.pins, host.assets, e, mc);

        if (MeshComponent* mesh = host.world.get<MeshComponent>(e))
        {
            MeshComponent next = *mesh;
            next.primitive     = PrimitiveMesh::None;
            setMeshComponent(host.world, host.pins, host.assets, e, next);
        }

        if (!model->skeleton())
        {
            result.attached = true;
            DE_LOG_INFO("Play: attached '{}' (static)", gltfPath);
            return result;
        }

        const std::string modelStem   = std::filesystem::path(gltfPath).stem().string();
        const EntityMaster modelMaster = loadEntityMasterType(modelStem);
        AssetRef<AnimGraphDef> graph;
        if (!modelMaster.anim.empty())
            graph = host.assets.loadAnimGraph(modelMaster.anim);
        if (!graph)
            graph = host.assets.tryLoadAnimGraphForModel(gltfPath);
        if (!graph)
            DE_LOG_WARN("Play: '{}' has no anim graph sidecar", gltfPath);

        if (mode == ModelAttach::AnimatedOnly)
        {
            if (TransformComponent* xf = host.world.get<TransformComponent>(e))
                xf->scale = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
        }

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
        host.world.emplace<AnimGraphComponent>(e, std::move(ag));

        result.attached = true;
        result.skinned  = true;
        DE_LOG_INFO("Play: attached '{}' graph={}", gltfPath, graph ? "yes" : "no");
        return result;
    }

    void ensureHumanoidBody(World& world, Entity e, float maxHp, const Math::Vector3f& halfExtents)
    {
        if (!aliveEntity(world, e))
            return;

        if (!world.has<HealthComponent>(e))
        {
            HealthSettings playerHp;
            playerHp.maxHp       = maxHp;
            playerHp.regenPerSec = 10.0f;
            playerHp.regenDelay  = 3.5f;
            HealthComponent hc{};
            hc.health = Health{ playerHp };
            world.emplace<HealthComponent>(e, std::move(hc));
        }
        if (!world.has<HitReactionComponent>(e))
        {
            HitReactionSettings hit{};
            hit.stunSeconds       = 0.28f;
            hit.knockbackDistance = 1.1f;
            hit.knockbackSeconds  = 0.14f;
            hit.horizontalOnly    = true;
            HitReactionComponent hr{};
            hr.hit.setSettings(hit);
            world.emplace<HitReactionComponent>(e, std::move(hr));
        }
        if (!world.has<HittableComponent>(e))
        {
            HittableComponent h{};
            h.halfExtents = halfExtents;
            world.emplace<HittableComponent>(e, h);
        }
    }

    void ensureLocalPlayerRig(const CharacterHost& host, Entity e, WeaponHitFn hitFn, void* hitUser)
    {
        if (!aliveEntity(host.world, e))
            return;

        if (!host.world.has<PlayerMotorComponent>(e))
            host.world.emplace<PlayerMotorComponent>(e);
        if (!host.world.has<HsmGraphComponent>(e))
        {
            HsmGraphComponent hsm{};
            const EntityMaster playerMaster = loadEntityMasterType("player");
            const std::string  hsmPath      = playerMaster.hsm.empty() ? std::string("ai/player.hsm.json") : playerMaster.hsm;
            hsm.def                         = host.assets.tryLoadHsmGraph(hsmPath);
            hsm.instance                    = std::make_unique<HsmGraphInstance>();
            const bool built                = hsm.def ? hsm.instance->build(*hsm.def) : hsm.instance->build(makePlayerHsmGraph());
            if (!built)
                DE_LOG_WARN(LogCategory::AI, "Play: player HSM build failed");
            else
            {
                hsm.instance->start();
                host.world.emplace<HsmGraphComponent>(e, std::move(hsm));
            }
        }
        if (!host.world.has<WeaponLoadoutComponent>(e))
        {
            auto& wlc   = host.world.emplace<WeaponLoadoutComponent>(e);
            wlc.loadout = std::make_unique<WeaponLoadout>();
            wlc.loadout->setHitListener(hitFn, hitUser);
            MeleeWeaponDesc sword = wlc.loadout->melee().desc();
            sword.hitDelay        = 0.12f;
            sword.singleTarget    = true;
            wlc.loadout->melee().setDesc(sword);
            wlc.slot = wlc.loadout->slot();
        }
        else if (WeaponLoadoutComponent* wlc = host.world.get<WeaponLoadoutComponent>(e); wlc && wlc->loadout)
            wlc->loadout->setHitListener(hitFn, hitUser);
        if (!host.world.has<JumpAttackComponent>(e))
        {
            auto& jac                = host.world.emplace<JumpAttackComponent>(e);
            Combat::JumpAttackDef def = jac.jump.def();
            def.telegraphSeconds     = 0.0f;
            def.cooldown             = 1.25f;
            def.connectFlags         = Combat::DamageFlags::CanBlock | Combat::DamageFlags::HardCc | Combat::DamageFlags::Knockdown;
            jac.jump.setDef(def);
        }
        if (!host.world.has<Combat::StatusEffectComponent>(e))
            host.world.emplace<Combat::StatusEffectComponent>(e);
        if (!host.world.has<Combat::PoiseComponent>(e))
            host.world.emplace<Combat::PoiseComponent>(e);
        Combat::equipPlayerShield(host.world, e);
        attachPlayerSounds(host.world, host.pins, host.assets, host.audio, e, host.ground);
        applySkillProfile(host.world, e, "player");
    }

    void configureNpc(const CharacterHost& host, Entity e, NpcKind kind)
    {
        if (!aliveEntity(host.world, e))
            return;

        const bool wolf = kind == NpcKind::Wolf;
        if (HittableComponent* hit = host.world.get<HittableComponent>(e))
            hit->halfExtents = wolf ? Math::Vector3f{ 0.45f, 0.45f, 0.70f } : Math::Vector3f{ 0.4f, 0.7f, 0.4f };
        if (wolf)
        {
            if (TagComponent* tag = host.world.get<TagComponent>(e))
                tag->name = "Wolf";
            applySkillProfile(host.world, e, "wolf");
        }
        attachHunterSounds(host.world, host.pins, host.assets, host.audio, e, host.ground);
    }

    ContentModelResult attachNpcVisual(const CharacterHost& host, Entity e, NpcKind kind, ModelAttach mode)
    {
        const char* path = "models/skeleton.gltf";
        std::string wolfGltf;
        if (kind == NpcKind::Wolf)
        {
            const EntityMaster wolfMaster = loadEntityMasterType("wolf");
            wolfGltf = wolfMaster.gltf.empty() ? std::string("models/wolf.gltf") : wolfMaster.gltf;
            path     = wolfGltf.c_str();
        }
        const ContentModelResult result = attachContentModel(host, e, path, mode);
        configureNpc(host, e, kind);
        return result;
    }

} // namespace Dark
