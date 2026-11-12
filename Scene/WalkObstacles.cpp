#include "Scene/WalkObstacles.h"

#include "ECS/World.h"
#include "Scene/SceneSpawn.h"

namespace Dark
{

    void collectWalkObstacles(World& world, std::vector<Math::AABox3f>& cubes, std::vector<Math::Sphere3f>& spheres)
    {
        cubes.clear();
        spheres.clear();
        world.each<ObstacleVolumeComponent>([&](Entity, ObstacleVolumeComponent& block) { cubes.push_back(block.box); });
        world.each<ObstacleSphereComponent>([&](Entity, ObstacleSphereComponent& block) { spheres.push_back(block.sphere); });
    }

} // namespace Dark
