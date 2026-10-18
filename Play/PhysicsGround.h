#pragma once

#include "Physics/PhysicsIds.h"

namespace Dark
{
    struct PlayerGroundQuery;

    namespace Physics
    {
        class PhysicsWorld;
    }

    namespace Terrain
    {
        class HeightMap;
    }

    // Height-field body for a coarse terrain map. Friction is 0.8. A null id means the map or the
    // world could not produce one; the caller decides whether to log that and whether to stand in a flat box.
    Physics::PhysicsBodyId bindHeightFieldGround(Physics::PhysicsWorld& physics, const Terrain::HeightMap& coarse, const char* logTag);

    // Terrain height, raised when a static body sits above it. The physics ground body is ignored so the
    // height field does not report itself as a raised surface. An invalid world returns terrainY.
    float raisedGroundY(const Physics::PhysicsWorld& physics, Physics::PhysicsBodyId ignore, Physics::PhysicsBodyId groundBody, float terrainY, float x, float z, float probeY);

    struct PlayWallProbe
    {
        const Physics::PhysicsWorld* physics = nullptr;
        Physics::PhysicsBodyId       ignore  = Physics::kNullPhysicsBody;
        Physics::PhysicsBodyId       ground  = Physics::kNullPhysicsBody;
    };
    void bindWallProbe(PlayerGroundQuery& query, const PlayWallProbe& probe);

} // namespace Dark
