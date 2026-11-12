#pragma once

#include "Math/AABox3f.h"
#include "Math/MathHelper.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Scene/SceneTypes.h"

#include <cmath>
#include <vector>

namespace Dark
{

    class World;

    // True when this solid should stay out of the hunter walk bake.
    inline bool walkObstacleSkipped(bool block, bool goal, bool conceal, bool zone, bool pickup)
    {
        return !block || goal || conceal || zone || pickup;
    }

    // Scene cubes and spheres only. Volume markers and flat cubes are not obstacles.
    // A negative scale still blocks. Sphere radius is half the longest axis of a unit sphere (radius 0.5).
    inline bool classifyWalkObstacle(SceneObjectType type, const Math::Vector3f& position, const Math::Vector3f& scale, bool block, bool goal, bool conceal, bool zone, bool pickup,
                                     Math::AABox3f& box, Math::Sphere3f& sphere)
    {
        if (walkObstacleSkipped(block, goal, conceal, zone, pickup))
            return false;
        if (type == SceneObjectType::Cube)
        {
            if (std::fabs(scale.y) < 0.3f)
                return false;
            const Math::Vector3f half{ 0.5f * std::fabs(scale.x), 0.5f * std::fabs(scale.y), 0.5f * std::fabs(scale.z) };
            box = Math::AABox3f::FromCenterExtents(position, half);
            return true;
        }
        if (type == SceneObjectType::Sphere)
        {
            const float radius = 0.5f * Math::Max(std::fabs(scale.x), Math::Max(std::fabs(scale.y), std::fabs(scale.z)));
            if (radius <= 1.0e-4f)
                return false;
            sphere = Math::Sphere3f(position, radius);
            return true;
        }
        return false;
    }

    // Copies ObstacleVolumeComponent and ObstacleSphereComponent. Does not add an origin cube.
    void collectWalkObstacles(World& world, std::vector<Math::AABox3f>& cubes, std::vector<Math::Sphere3f>& spheres);

} // namespace Dark
