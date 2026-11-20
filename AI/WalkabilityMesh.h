#pragma once

#include <cstdint>
#include <vector>

namespace Dark::AI
{
    class Walkability;

    struct WalkabilityMeshVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    static_assert(sizeof(WalkabilityMeshVertex) == 28, "walk mesh vertex is position + color");

    struct WalkabilityMeshData
    {
        std::vector<WalkabilityMeshVertex> vertices;
        std::vector<uint32_t>              indices;
    };

    // Quads sit this far above the heightfield so the overlay clears the terrain.
    constexpr float kWalkabilityMeshLift = 0.25f;
    // Long rectangles chord through hills. Four cells keeps the surface on the slope.
    constexpr int kWalkabilityMeshSpan = 4;

    // One quad per run of walkable cells that share an island. Empty when nothing is walkable.
    bool buildWalkabilityMesh(const Walkability& walk, WalkabilityMeshData& out);

} // namespace Dark::AI
