#include "Scene/SceneSpawn.h"

#include "Assets/AssetManager.h"
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
#include "Scene/SceneTypes.h"

#include <format>
#include <string>

namespace Dark
{

    namespace
    {
        using namespace Math;

        bool localLight(SceneObjectType type)
        {
            return type == SceneObjectType::PointLight || type == SceneObjectType::SpotLight;
        }

        void spawnOne(World& world, const SceneObjectData& o, const SceneSpawnHost& host, int& powerStamp)
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
                    spawnItemPickup(world, *host.pins, *host.assets, *host.renderer, *host.physics, ItemStack{ def, Math::Max(1, o.pickupCount) }, o.position, Vector3f{});
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
        int powerStamp = 0;
        for (int i = 0; i < count; ++i)
            spawnOne(*host.world, objects[i], host, powerStamp);
    }

} // namespace Dark
