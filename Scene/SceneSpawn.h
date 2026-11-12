#pragma once

#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"

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

    // Pathfinding block created from a scene sphere. Radius is half the longest scale axis.
    struct ObstacleSphereComponent
    {
        static constexpr const char* kTypeName = "ObstacleSphere";

        Math::Sphere3f sphere{};
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

    // powerStamp and pickupStamp persist across batches. Null starts each call at 0.
    void instantiateSceneObjects(const SceneObjectData* objects, int count, const SceneSpawnHost& host,
        int* powerStamp = nullptr, int* pickupStamp = nullptr);

    // Gameplay fields from one scene object: light circuit, hunter dialogue, patrol, and squad.
    // heightAt supplies terrain Y for patrol points and hunter or wolf feet. Null leaves Y alone.
    // A searchlight spot gets a sweeping SecurityCamera on its own entity. Unticking it removes the camera.
    void applySearchlight(World& world, Entity light, const SceneObjectData& object);

    void applySceneActor(World& world, Entity actor, const SceneObjectData& object, float (*heightAt)(void* user, float x, float z), void* heightUser);

} // namespace Dark
