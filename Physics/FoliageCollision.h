#pragma once

#include "Physics/PhysicsIds.h"
#include "Terrain/FoliageFile.h"

#include <cstdint>
#include <vector>

namespace Dark::Terrain
{
    class TerrainGrid;
}

namespace Dark::Physics
{
    class PhysicsWorld;

    constexpr uint32_t kMaxLiveFoliageBodies = 2048u;
    constexpr float    kFoliageBodyRadiusM   = 64.0f;
    constexpr float    kFoliageBodyRebuildM  = 8.0f;

    struct FoliageCollisionSet
    {
        std::vector<PhysicsBodyId> bodies;
        float                      anchorX       = 0.0f;
        float                      anchorZ       = 0.0f;
        uint64_t                   sourceStamp   = 0;
        bool                       built         = false;
        bool                       invalidLogged = false;
    };

    // Tree and rock records inside kFoliageBodyRadiusM, nearest first, at most kMaxLiveFoliageBodies.
    // *qualified (optional) is the count before the cap. Returns how many indices were written.
    uint32_t selectFoliageBodies(
        const Terrain::FoliageRecord* records,
        uint32_t count,
        float playerX,
        float playerZ,
        uint32_t* outIndices,
        uint32_t outCapacity,
        uint32_t* qualified);

    // editorFoliage non-null: that list, and only records whose tile isResident.
    // null: walk residentFoliage. No bodies when the world is not valid (logs once).
    void syncFoliageCollision(
        PhysicsWorld& world,
        FoliageCollisionSet& set,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorFoliage,
        float playerX,
        float playerZ);

    void destroyFoliageCollision(PhysicsWorld& world, FoliageCollisionSet& set);

} // namespace Dark::Physics
