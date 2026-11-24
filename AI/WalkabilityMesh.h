#pragma once

#include "AI/DebugColorMesh.h"

#include <cstdint>
#include <vector>

namespace Dark::AI
{
    class Walkability;

    // The overlay vertex and mesh are WalkabilityDebug's plain debug types.
    using WalkabilityMeshVertex = DebugColorVertex;
    using WalkabilityMeshData   = DebugColorMesh;

    // Quads sit this far above the heightfield so the overlay clears the terrain.
    constexpr float kWalkabilityMeshLift = 0.25f;
    // Long rectangles chord through hills. Four cells keeps the surface on the slope.
    constexpr int kWalkabilityMeshSpan = 4;

    // One quad per run of walkable cells that share an island. Empty when nothing is walkable.
    bool buildWalkabilityMesh(const Walkability& walk, WalkabilityMeshData& out);

    // What WalkabilityDebug draws for this bake: rebuilt when the bake serial changes, nothing when walk is null or invalid.
    DebugMeshSource walkabilityMeshSource(const Walkability* walk);

} // namespace Dark::AI
