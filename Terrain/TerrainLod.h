#pragma once

#include <cstdint>

#include "Math/Vector3f.h"
#include "Render/MeshGen.h"

namespace Dark::Terrain
{

    class HeightMap;

    constexpr int kMaxLodLevels = 8;

    // Bit 0 = North (+Z), 1 = East (+X), 2 = South (-Z), 3 = West (-X).
    // A set bit means that neighbor is coarser and this patch must weld / stitch.
    struct EdgeMask
    {
        uint8_t bits = 0;

        bool north() const { return (bits & 1u) != 0; }
        bool east() const { return (bits & 2u) != 0; }
        bool south() const { return (bits & 4u) != 0; }
        bool west() const { return (bits & 8u) != 0; }

        static EdgeMask fromBits(uint8_t bits)
        {
            EdgeMask m;
            m.bits = static_cast<uint8_t>(bits & 0x0Fu);
            return m;
        }
    };

    inline bool isPowerOfTwo(int v)
    {
        return v > 0 && (v & (v - 1)) == 0;
    }

    inline int lodStep(int lod)
    {
        if (lod < 0)
            lod = 0;
        return 1 << lod;
    }

    int maxLodForChunkCells(int chunkCells);

    // Distance-based LOD: distances[i] is the range at which we step from lod i to i+1.
    int lodFromDistance(float distance, const float* distances, int distanceCount, int maxLod);

    // Pull coarse patches toward their finer neighbors so adjacent LODs differ by at most 1.
    void restrictNeighborLods(int* lods, int chunksX, int chunksZ, int maxIterations = 32);

    EdgeMask neighborCoarserMask(const int* lods, int chunksX, int chunksZ, int cx, int cz);

    // True if patch-local vertex (x,z) is a T-junction that must not appear in the IB
    // (odd vertex on an edge whose neighbor is coarser).
    bool isSkippedEdgeVertex(int x, int z, int cells, EdgeMask mask);

    struct PatchBuildDesc
    {
        const HeightMap* heightMap = nullptr;
        int originSampleX = 0;
        int originSampleZ = 0;
        int chunkCells    = 16; // power of two
        int lod           = 0;
        EdgeMask edges{};
    };

    // Builds a world-space patch. Odd vertices on coarser-neighbor edges are welded
    // onto the coarse segment (same world position as the neighbor's edge) and then
    // omitted from the index buffer so there are no T-junctions.
    bool buildPatchMesh(const PatchBuildDesc& desc, MeshData& out);

    // Same crack-free index pattern as a terrain patch. Caller supplies a (cells+1)^2
    // vertex grid in row-major X then Z. Used by water so both surfaces share topology.
    bool buildGridIndices(int cells, EdgeMask edges, MeshData& out);

} // namespace Dark::Terrain
