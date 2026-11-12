#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Scene/SceneSpawn.h"
#include "Scene/SceneTypes.h"
#include "Scene/WalkObstacles.h"

using namespace Dark;

TEST(WalkObstacles, CollectsCubesAndSpheresAndSkipsAnEmptyWorld)
{
    World world;
    std::vector<Math::AABox3f> cubes;
    std::vector<Math::Sphere3f> spheres;
    collectWalkObstacles(world, cubes, spheres);
    EXPECT_TRUE(cubes.empty());
    EXPECT_TRUE(spheres.empty());

    const Entity cube = world.createEntity();
    ObstacleVolumeComponent box{};
    box.box = Math::AABox3f::FromCenterExtents(Math::Vector3f{ 1.0f, 2.0f, 3.0f }, Math::Vector3f{ 0.5f, 0.5f, 0.5f });
    world.emplace<ObstacleVolumeComponent>(cube, box);

    const Entity ball = world.createEntity();
    ObstacleSphereComponent round{};
    round.sphere = Math::Sphere3f(Math::Vector3f{ 4.0f, 0.0f, 5.0f }, 1.5f);
    world.emplace<ObstacleSphereComponent>(ball, round);

    collectWalkObstacles(world, cubes, spheres);
    ASSERT_EQ(cubes.size(), 1u);
    ASSERT_EQ(spheres.size(), 1u);
    EXPECT_NEAR(cubes[0].Center().x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(spheres[0].Center.x, 4.0f, 1.0e-4f);
    EXPECT_NEAR(spheres[0].Radius, 1.5f, 1.0e-4f);
}

TEST(WalkObstacles, ClassifySkipsVolumesAndKeepsNegativeScale)
{
    Math::AABox3f box;
    Math::Sphere3f sphere;
    const Math::Vector3f at{ 2.0f, 1.0f, -3.0f };
    const Math::Vector3f scale{ -2.0f, 1.0f, 4.0f };
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Cube, at, scale, false, false, false, false, false, box, sphere));
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Cube, at, scale, true, true, false, false, false, box, sphere));
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Cube, at, scale, true, false, true, false, false, box, sphere));
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Cube, at, scale, true, false, false, true, false, box, sphere));
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Cube, at, Math::Vector3f{ 1.0f, 0.2f, 1.0f }, true, false, false, false, false, box, sphere));
    EXPECT_TRUE(classifyWalkObstacle(SceneObjectType::Cube, at, scale, true, false, false, false, false, box, sphere));
    EXPECT_NEAR(box.Extents().x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(box.Extents().z, 2.0f, 1.0e-4f);
    EXPECT_TRUE(classifyWalkObstacle(SceneObjectType::Sphere, at, scale, true, false, false, false, false, box, sphere));
    EXPECT_NEAR(sphere.Radius, 2.0f, 1.0e-4f);
    EXPECT_FALSE(classifyWalkObstacle(SceneObjectType::Model, at, scale, true, false, false, false, false, box, sphere));
}
