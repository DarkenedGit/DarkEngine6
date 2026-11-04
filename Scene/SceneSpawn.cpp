#include "Scene/SceneSpawn.h"

#include "Assets/AssetManager.h"
#include "AI/AiComponents.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueMemory.h"
#include "Play/Interaction.h"
#include "Character/ItemView.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Item.h"
#include "Gameplay/PowerComponents.h"
#include "Gameplay/QuestZone.h"
#include "Math/MathHelper.h"
#include "Play/Carry.h"
#include "Render/Renderer.h"
#include "Save/PersistentId.h"
#include "Scene/Building.h"
#include "Scene/EntityMaster.h"
#include "Scene/SceneTypes.h"

#include <cmath>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace Dark
{

    namespace
    {
        using namespace Math;

        bool localLight(SceneObjectType type)
        {
            return type == SceneObjectType::PointLight || type == SceneObjectType::SpotLight;
        }

        void spawnOne(World& world, const SceneObjectData& o, const SceneSpawnHost& host, int& powerStamp, int& pickupStamp)
        {
            const AABox3f bounds = AABox3f::FromCenterExtents(o.position, o.scale * 0.5f);
            const bool    cube   = o.type == SceneObjectType::Cube;
            if (!o.zone.empty())
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                QuestZoneComponent zone{};
                zone.zone = o.zone;
                zone.box  = bounds;
                world.emplace<QuestZoneComponent>(e, zone);
                return;
            }
            if (!o.pickup.empty())
            {
                if (!host.physics || !host.pins || !host.assets || !host.renderer)
                {
                    DE_LOG_WARN("Scene: pickup '{}' skipped, physics world is not live", o.pickup);
                    return;
                }
                if (const ItemDef* def = findItemDef(o.pickup))
                {
                    const Entity pickup = spawnItemPickup(world, *host.pins, *host.assets, *host.renderer, *host.physics, ItemStack{ def, Math::Max(1, o.pickupCount) }, o.position, Vector3f{});
                    if (pickup.valid() && host.stampPrefix && host.stampPrefix[0] != '\0')
                        stampProceduralId(world, pickup, std::format("{}/pickup/{}", host.stampPrefix, pickupStamp++), "pickup");
                }
                else
                    DE_LOG_WARN("Scene: unknown pickup item '{}'", o.pickup);
                return;
            }
            if (o.goal)
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                GoalVolumeComponent goal{};
                goal.box = bounds;
                world.emplace<GoalVolumeComponent>(e, goal);
            }
            if (!o.circuit.empty() && !localLight(o.type))
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                PowerBoxComponent box{};
                box.circuit = o.circuit;
                box.pos     = o.position;
                box.on      = true;
                world.emplace<PowerBoxComponent>(e, box);
                setInteractable(world, e, InteractKind::Power, kInteractPowerRange);
                if (host.stampPrefix && host.stampPrefix[0] != '\0')
                    stampProceduralId(world, e, std::format("{}/power/{}", host.stampPrefix, powerStamp++), "power");
            }
            if (o.conceal)
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                ConcealVolumeComponent vol{};
                vol.box = bounds;
                world.emplace<ConcealVolumeComponent>(e, vol);
                return;
            }
            if (cube && o.scale.y >= 0.3f && !o.goal && o.block)
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                ObstacleVolumeComponent block{};
                block.box = bounds;
                world.emplace<ObstacleVolumeComponent>(e, block);
            }
        }

    } // namespace

    void instantiateSceneObjects(const SceneObjectData* objects, int count, const SceneSpawnHost& host)
    {
        if (!host.world || !objects || count <= 0)
            return;
        int powerStamp  = 0;
        int pickupStamp = 0;
        for (int i = 0; i < count; ++i)
            spawnOne(*host.world, objects[i], host, powerStamp, pickupStamp);
    }

    void applySearchlight(World& world, Entity light, const SceneObjectData& object)
    {
        const bool wanted = object.type == SceneObjectType::SpotLight && object.searchlight;
        if (!wanted)
        {
            if (world.get<SecurityCameraComponent>(light))
                world.remove<SecurityCameraComponent>(light);
            return;
        }
        const TransformComponent*  xf   = world.get<TransformComponent>(light);
        const LocalLightComponent* beam = world.get<LocalLightComponent>(light);
        const Math::Vector3f       pos  = xf ? xf->position : object.position;
        const Math::Vector3f       aim  = (xf ? xf->rotation : object.rotation).Rotate(Math::Vector3f::Z_AXIS);
        const float                flat = std::sqrt(aim.x * aim.x + aim.z * aim.z);

        SecurityCameraComponent cam;
        cam.def.pos         = pos;
        cam.def.yaw         = flat > 1.0e-3f ? std::atan2(aim.x, aim.z) * 57.29578f : 0.0f;
        cam.def.drop        = flat > 1.0e-3f ? Math::Clamp(-aim.y / flat, -1.0f, 8.0f) : 8.0f;
        cam.def.sweep       = Math::Clamp(object.sweepDeg, 0.0f, 340.0f);
        cam.def.range       = beam ? beam->range : object.lightRange;
        cam.def.cone        = beam ? beam->outerConeDeg : object.lightOuterDeg;
        cam.def.intensity   = beam ? beam->intensity : object.lightIntensity;
        cam.def.circuit     = object.circuit;
        cam.def.searchlight = true;
        cam.aimYaw          = cam.def.yaw;
        cam.light           = light;
        if (SecurityCameraComponent* existing = world.get<SecurityCameraComponent>(light))
            *existing = cam;
        else
            world.emplace<SecurityCameraComponent>(light, cam);
    }

    void applySceneActor(World& world, Entity actor, const SceneObjectData& object, float (*heightAt)(void* user, float x, float z), void* heightUser)
    {
        if (!actor.valid() || !world.alive(actor))
            return;
        if (object.type == SceneObjectType::SpotLight)
            applySearchlight(world, actor, object);

        const bool light = object.type == SceneObjectType::PointLight || object.type == SceneObjectType::SpotLight;
        if (light && !object.circuit.empty())
        {
            CircuitMemberComponent* member = world.get<CircuitMemberComponent>(actor);
            if (!member)
                member = &world.emplace<CircuitMemberComponent>(actor);
            member->circuit = object.circuit;
        }

        const bool pawn = object.type == SceneObjectType::Hunter || object.type == SceneObjectType::Wolf;
        if (pawn && heightAt)
        {
            if (TransformComponent* xf = world.get<TransformComponent>(actor))
                xf->position.y = heightAt(heightUser, xf->position.x, xf->position.z) + 0.5f;
        }
        if (object.type != SceneObjectType::Hunter)
            return;

        std::string path = object.dialogue;
        if (path.empty())
            path = loadEntityMasterType("hunter").dialogue;
        if (path.empty())
        {
            if (world.get<DialogueBindingComponent>(actor))
                world.remove<DialogueBindingComponent>(actor);
        }
        else if (DialogueBindingComponent* existing = world.get<DialogueBindingComponent>(actor))
            existing->graph = std::move(path);
        else
        {
            DialogueBindingComponent binding;
            binding.graph = std::move(path);
            world.emplace<DialogueBindingComponent>(actor, std::move(binding));
            if (!world.get<DialogueMemoryComponent>(actor))
                world.emplace<DialogueMemoryComponent>(actor);
        }
        if (world.get<DialogueBindingComponent>(actor) && !world.get<DialogueMemoryComponent>(actor))
            world.emplace<DialogueMemoryComponent>(actor);

        if (object.patrol.empty())
        {
            if (world.get<PatrolComponent>(actor))
                world.remove<PatrolComponent>(actor);
        }
        else
        {
            PatrolComponent patrol;
            patrol.points.reserve(object.patrol.size());
            for (const Math::Vector2f& p : object.patrol)
            {
                const float y = heightAt ? heightAt(heightUser, p.x, p.y) : 0.0f;
                patrol.points.push_back(Math::Vector3f{ p.x, y, p.y });
            }
            if (PatrolComponent* existing = world.get<PatrolComponent>(actor))
                *existing = std::move(patrol);
            else
                world.emplace<PatrolComponent>(actor, std::move(patrol));
        }

        if (AiAgentComponent* agent = world.get<AiAgentComponent>(actor))
            agent->squad = object.squad.empty() ? 0u : static_cast<uint32_t>(std::hash<std::string>{}(object.squad) | 1u);
    }

} // namespace Dark
