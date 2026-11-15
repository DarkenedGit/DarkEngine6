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
#include "Content/SceneObjectSpawn.h"
#include "Scene/SceneTypes.h"
#include "Scene/WalkObstacles.h"

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

        bool circuitLight(const SceneObjectData& object)
        {
            if (sceneHasComponent(object, "ParticleEmitter"))
                return false;
            const std::string kind = sceneLightKind(object);
            return kind == "point" || kind == "spot";
        }

        void spawnOne(World& world, const SceneObjectData& o, const SceneSpawnHost& host, int& powerStamp, int& pickupStamp)
        {
            const AABox3f bounds = AABox3f::FromCenterExtents(o.position, o.scale * 0.5f);
            const std::string zoneId = sceneZone(o);
            const std::string pickupId = scenePickup(o);
            const std::string circuitId = sceneCircuit(o);
            const bool goal = sceneGoal(o);
            const bool conceal = sceneConceal(o);
            if (!zoneId.empty())
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                QuestZoneComponent zone{};
                zone.zone = zoneId;
                zone.box  = bounds;
                world.emplace<QuestZoneComponent>(e, zone);
                return;
            }
            if (!pickupId.empty())
            {
                if (!host.physics || !host.pins || !host.assets || !host.renderer)
                {
                    DE_LOG_WARN("Scene: pickup '{}' skipped, physics world is not live", pickupId);
                    return;
                }
                if (const ItemDef* def = findItemDef(pickupId))
                {
                    const Entity pickup = spawnItemPickup(world, *host.pins, *host.assets, *host.renderer, *host.physics, ItemStack{ def, Math::Max(1, scenePickupCount(o)) }, o.position, Vector3f{});
                    if (pickup.valid() && host.stampPrefix && host.stampPrefix[0] != '\0')
                        stampProceduralId(world, pickup, std::format("{}/pickup/{}", host.stampPrefix, pickupStamp++), "pickup");
                }
                else
                    DE_LOG_WARN("Scene: unknown pickup item '{}'", pickupId);
                return;
            }
            if (goal)
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                GoalVolumeComponent goalVolume{};
                goalVolume.box = bounds;
                world.emplace<GoalVolumeComponent>(e, goalVolume);
            }
            if (!circuitId.empty() && !circuitLight(o))
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                PowerBoxComponent box{};
                box.circuit = circuitId;
                box.pos     = o.position;
                box.on      = true;
                world.emplace<PowerBoxComponent>(e, box);
                setInteractable(world, e, InteractKind::Power, kInteractPowerRange);
                if (host.stampPrefix && host.stampPrefix[0] != '\0')
                    stampProceduralId(world, e, std::format("{}/power/{}", host.stampPrefix, powerStamp++), "power");
            }
            if (conceal)
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                ConcealVolumeComponent vol{};
                vol.box = bounds;
                world.emplace<ConcealVolumeComponent>(e, vol);
                return;
            }
            Math::AABox3f  walkBox;
            Math::Sphere3f walkSphere;
            std::string    walkPrimitive;
            if (sceneWalkObstacle(o, walkPrimitive)
                && classifyWalkObstacle(walkPrimitive, o.position, o.scale, sceneBlock(o), goal, conceal, !zoneId.empty(), !pickupId.empty(), walkBox, walkSphere))
            {
                const Entity e = world.createEntity();
                world.emplace<TransformComponent>(e, o.position, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
                if (walkPrimitive == "sphere")
                {
                    ObstacleSphereComponent block{};
                    block.sphere = walkSphere;
                    world.emplace<ObstacleSphereComponent>(e, block);
                }
                else
                {
                    ObstacleVolumeComponent block{};
                    block.box = walkBox;
                    world.emplace<ObstacleVolumeComponent>(e, block);
                }
            }
        }

    } // namespace

    void instantiateSceneObjects(const SceneObjectData* objects, int count, const SceneSpawnHost& host,
        int* powerStampIn, int* pickupStampIn)
    {
        if (!host.world || !objects || count <= 0)
            return;
        int localPower  = 0;
        int localPickup = 0;
        int& powerStamp  = powerStampIn ? *powerStampIn : localPower;
        int& pickupStamp = pickupStampIn ? *pickupStampIn : localPickup;
        for (int i = 0; i < count; ++i)
            spawnOne(*host.world, objects[i], host, powerStamp, pickupStamp);
    }

    void applySearchlight(World& world, Entity light, const SceneObjectData& object)
    {
        const bool wanted = sceneSearchlight(object) && sceneLightKind(object) == "spot";
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
        cam.def.sweep       = sceneSweepDeg(object);
        cam.def.range       = beam ? beam->range : sceneLightFloat(object, "range", 8.0f);
        cam.def.cone        = beam ? beam->outerConeDeg : sceneLightFloat(object, "outer", 25.0f);
        cam.def.intensity   = beam ? beam->intensity : sceneLightFloat(object, "intensity", 1885.0f);
        cam.def.circuit     = sceneCircuit(object);
        cam.def.searchlight = true;
        cam.aimYaw          = cam.def.yaw;
        cam.light           = light;
        cam.hasAuthoredPose = true;
        cam.authoredPos     = pos;
        cam.authoredRot     = xf ? xf->rotation : object.rotation;
        if (SecurityCameraComponent* existing = world.get<SecurityCameraComponent>(light))
            *existing = cam;
        else
            world.emplace<SecurityCameraComponent>(light, cam);
    }

    void applySceneActor(World& world, Entity actor, const SceneObjectData& object, float (*heightAt)(void* user, float x, float z), void* heightUser)
    {
        if (!actor.valid() || !world.alive(actor))
            return;
        if (sceneLightKind(object) == "spot")
            applySearchlight(world, actor, object);

        const std::string kind = sceneLightKind(object);
        const std::string circuit = sceneCircuit(object);
        const bool light = (kind == "point" || kind == "spot") && !sceneHasComponent(object, "ParticleEmitter");
        if (light && !circuit.empty())
        {
            CircuitMemberComponent* member = world.get<CircuitMemberComponent>(actor);
            if (!member)
                member = &world.emplace<CircuitMemberComponent>(actor);
            member->circuit = circuit;
        }

        const bool pawn = sceneHasBehavior(object, "ai.hunter") || sceneHasBehavior(object, "ai.wolf");
        if (pawn && heightAt)
        {
            if (TransformComponent* xf = world.get<TransformComponent>(actor))
                xf->position.y = heightAt(heightUser, xf->position.x, xf->position.z) + 0.5f;
        }
        if (!sceneHasBehavior(object, "ai.hunter"))
            return;

        std::string path = sceneDialogue(object);
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

        const std::vector<Math::Vector2f> patrolPoints = scenePatrol(object);
        if (patrolPoints.empty())
        {
            if (world.get<PatrolComponent>(actor))
                world.remove<PatrolComponent>(actor);
        }
        else
        {
            PatrolComponent patrol;
            patrol.points.reserve(patrolPoints.size());
            for (const Math::Vector2f& p : patrolPoints)
            {
                const float y = heightAt ? heightAt(heightUser, p.x, p.y) : 0.0f;
                patrol.points.push_back(Math::Vector3f{ p.x, y, p.y });
            }
            if (PatrolComponent* existing = world.get<PatrolComponent>(actor))
                *existing = std::move(patrol);
            else
                world.emplace<PatrolComponent>(actor, std::move(patrol));
        }

        const std::string squad = sceneSquad(object);
        if (AiAgentComponent* agent = world.get<AiAgentComponent>(actor))
            agent->squad = squad.empty() ? 0u : static_cast<uint32_t>(std::hash<std::string>{}(squad) | 1u);
    }

} // namespace Dark
