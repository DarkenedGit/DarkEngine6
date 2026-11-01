#pragma once

#include "ECS/Entity.h"
#include "Math/AABox3f.h"

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;
    struct SceneObjectData;

    namespace Physics
    {
        class PhysicsWorld;
    }

    // Pathfinding block created from a scene cube. The chase copies the box before hunters spawn.
    struct ObstacleVolumeComponent
    {
        static constexpr const char* kTypeName = "ObstacleVolume";

        Math::AABox3f box{};
    };

    // Pickup spawn uses the live physics world. A null physics pointer skips pickups.
    struct SceneSpawnHost
    {
        World*                  world       = nullptr;
        Physics::PhysicsWorld*  physics     = nullptr;
        AssetPinTable*          pins        = nullptr;
        AssetManager*           assets      = nullptr;
        Renderer*               renderer    = nullptr;
        const char*             stampPrefix = nullptr;
    };

    void instantiateSceneObjects(const SceneObjectData* objects, int count, const SceneSpawnHost& host);

    // Gameplay fields from one scene object: light circuit, hunter dialogue, patrol, and squad.
    // heightAt supplies terrain Y for patrol points and hunter or wolf feet. Null leaves Y alone.
    void applySceneActor(World& world, Entity actor, const SceneObjectData& object, float (*heightAt)(void* user, float x, float z), void* heightUser);

} // namespace Dark
