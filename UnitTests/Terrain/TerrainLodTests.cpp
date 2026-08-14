#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainLod.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::Geometry;

namespace
{

HeightMap MakeRamp(int samples, float cell = 1.0f)
{
    HeightMap hm;
    hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), cell, 1.0f);
    for (int z = 0; z < samples; ++z)
    {
        for (int x = 0; x < samples; ++x)
            hm.setHeight(x, z, 0.15f * static_cast<float>(x) + 0.07f * static_cast<float>(z));
    }
    return hm;
}

bool Near3(const Vector3f& a, const Vector3f& b, float eps = 1.0e-4f)
{
    return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
}

// Point lies on the segment a-b (inclusive).
bool PointOnSegment(const Vector3f& p, const Vector3f& a, const Vector3f& b, float eps = 1.5e-3f)
{
    const Vector3f ab = b - a;
    const Vector3f ap = p - a;
    const float ab2 = ab.MagnitudeSqrd();
    if (ab2 < eps * eps)
        return Near3(p, a, eps);
    const float t = ap.Dot(ab) / ab2;
    if (t < -0.01f || t > 1.01f)
        return false;
    const Vector3f proj = a + ab * t;
    return Near3(p, proj, eps);
}

struct EdgeKey
{
    uint32_t a;
    uint32_t b;
    bool operator<(const EdgeKey& o) const
    {
        if (a != o.a)
            return a < o.a;
        return b < o.b;
    }
};

EdgeKey MakeEdge(uint32_t i, uint32_t j)
{
    return i < j ? EdgeKey{ i, j } : EdgeKey{ j, i };
}

bool PatchManifold(const MeshData& mesh)
{
    if (mesh.indices.size() < 3 || (mesh.indices.size() % 3) != 0)
        return false;

    std::map<EdgeKey, int> counts;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const uint32_t i0 = mesh.indices[t];
        const uint32_t i1 = mesh.indices[t + 1];
        const uint32_t i2 = mesh.indices[t + 2];
        if (i0 == i1 || i1 == i2 || i2 == i0)
            return false;
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size())
            return false;
        ++counts[MakeEdge(i0, i1)];
        ++counts[MakeEdge(i1, i2)];
        ++counts[MakeEdge(i2, i0)];
    }
    for (const auto& kv : counts)
    {
        if (kv.second != 1 && kv.second != 2)
            return false;
    }
    return true;
}

bool IndicesAvoidSkipped(const MeshData& mesh, int cells, EdgeMask mask)
{
    const int verts = cells + 1;
    for (uint32_t idx : mesh.indices)
    {
        const int x = static_cast<int>(idx % static_cast<uint32_t>(verts));
        const int z = static_cast<int>(idx / static_cast<uint32_t>(verts));
        if (isSkippedEdgeVertex(x, z, cells, mask))
            return false;
    }
    return true;
}

void CollectEdgePositions(const MeshData& mesh, int cells, int edge, std::vector<Vector3f>& out)
{
    // edge: 0=N 1=E 2=S 3=W
    const int verts = cells + 1;
    out.clear();
    for (int i = 0; i < verts; ++i)
    {
        int x = 0;
        int z = 0;
        if (edge == 0)
        {
            x = i;
            z = cells;
        }
        else if (edge == 1)
        {
            x = cells;
            z = i;
        }
        else if (edge == 2)
        {
            x = i;
            z = 0;
        }
        else
        {
            x = 0;
            z = i;
        }
        const size_t vi = static_cast<size_t>(z * verts + x);
        if (vi < mesh.positions.size())
            out.push_back(mesh.positions[vi]);
    }
}

bool FineEdgeLiesOnCoarse(const std::vector<Vector3f>& fine, const std::vector<Vector3f>& coarse)
{
    if (coarse.size() < 2)
        return false;
    for (const Vector3f& p : fine)
    {
        bool on = false;
        for (size_t i = 0; i + 1 < coarse.size(); ++i)
        {
            if (PointOnSegment(p, coarse[i], coarse[i + 1]))
            {
                on = true;
                break;
            }
        }
        if (!on)
            return false;
    }
    for (const Vector3f& p : coarse)
    {
        bool found = false;
        for (const Vector3f& q : fine)
        {
            if (Near3(p, q, 1.5e-3f))
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

} // namespace

TEST(TerrainLod, Helpers)
{
    EXPECT_TRUE(isPowerOfTwo(16));
    EXPECT_FALSE(isPowerOfTwo(15));
    EXPECT_EQ(lodStep(0), 1);
    EXPECT_EQ(lodStep(3), 8);
    EXPECT_EQ(maxLodForChunkCells(16), 4);
    EXPECT_EQ(maxLodForChunkCells(1), 0);

    const float dist[] = { 10.0f, 20.0f, 40.0f };
    EXPECT_EQ(lodFromDistance(0.0f, dist, 3, 4), 0);
    EXPECT_EQ(lodFromDistance(10.0f, dist, 3, 4), 1);
    EXPECT_EQ(lodFromDistance(25.0f, dist, 3, 4), 2);
    EXPECT_EQ(lodFromDistance(100.0f, dist, 3, 4), 3);
}

TEST(TerrainLod, RestrictCreatesGradient)
{
    // 4 chunks in a row: requested lods 0, 0, 0, 4
    int lods[4] = { 0, 0, 0, 4 };
    restrictNeighborLods(lods, 4, 1);
    EXPECT_EQ(lods[0], 0);
    EXPECT_EQ(lods[1], 0);
    EXPECT_EQ(lods[2], 0);
    EXPECT_LE(lods[3] - lods[2], 1);
    EXPECT_EQ(lods[3], 1);
}

TEST(TerrainLod, SameLodPatchIsRegularGrid)
{
    HeightMap hm = MakeRamp(17);
    PatchBuildDesc d;
    d.heightMap     = &hm;
    d.originSampleX = 0;
    d.originSampleZ = 0;
    d.chunkCells    = 16;
    d.lod           = 0;
    d.edges         = {};

    MeshData mesh;
    ASSERT_TRUE(buildPatchMesh(d, mesh));
    EXPECT_EQ(mesh.positions.size(), 17u * 17u);
    EXPECT_EQ(mesh.indices.size(), 16u * 16u * 6u);
    EXPECT_TRUE(PatchManifold(mesh));
}

TEST(TerrainLod, WindingIsFrontFacingFromAbove)
{
    // Flat patch: D3D FrontCounterClockwise + Y-down viewport wants CW math winding
    // (geometric cross Y < 0) so the top surface is not backface-culled.
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, 0.0f);
    }

    PatchBuildDesc d;
    d.heightMap     = &hm;
    d.originSampleX = 0;
    d.originSampleZ = 0;
    d.chunkCells    = 16;
    d.lod           = 0;
    d.edges         = {};

    MeshData mesh;
    ASSERT_TRUE(buildPatchMesh(d, mesh));
    ASSERT_GE(mesh.indices.size(), 3u);

    int negativeY = 0;
    int positiveY = 0;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const Vector3f& a = mesh.positions[mesh.indices[t]];
        const Vector3f& b = mesh.positions[mesh.indices[t + 1]];
        const Vector3f& c = mesh.positions[mesh.indices[t + 2]];
        const float cy = (b - a).Cross(c - a).y;
        if (cy < 0.0f)
            ++negativeY;
        else if (cy > 0.0f)
            ++positiveY;
    }
    EXPECT_GT(negativeY, 0);
    EXPECT_EQ(positiveY, 0);
}

TEST(TerrainLod, AllEdgeMasksAvoidTJunctions)
{
    HeightMap hm = MakeRamp(17);
    const int chunkCells = 16;
    for (int lod = 0; lod <= 3; ++lod)
    {
        const int cells = chunkCells / lodStep(lod);
        for (int mask = 0; mask < 16; ++mask)
        {
            PatchBuildDesc d;
            d.heightMap     = &hm;
            d.originSampleX = 0;
            d.originSampleZ = 0;
            d.chunkCells    = chunkCells;
            d.lod           = lod;
            d.edges         = EdgeMask::fromBits(static_cast<uint8_t>(mask));

            MeshData mesh;
            ASSERT_TRUE(buildPatchMesh(d, mesh)) << "lod=" << lod << " mask=" << mask;
            EXPECT_FALSE(mesh.indices.empty());
            EXPECT_TRUE(IndicesAvoidSkipped(mesh, cells, d.edges)) << "lod=" << lod << " mask=" << mask;
            EXPECT_TRUE(PatchManifold(mesh)) << "lod=" << lod << " mask=" << mask;
        }
    }
}

TEST(TerrainLod, WeldedSharedEdgeMatchesCoarse)
{
    HeightMap hm = MakeRamp(33);

    PatchBuildDesc fine;
    fine.heightMap     = &hm;
    fine.originSampleX = 0;
    fine.originSampleZ = 0;
    fine.chunkCells    = 16;
    fine.lod           = 0;
    fine.edges         = EdgeMask::fromBits(2u); // east neighbor coarser

    PatchBuildDesc coarse;
    coarse.heightMap     = &hm;
    coarse.originSampleX = 16;
    coarse.originSampleZ = 0;
    coarse.chunkCells    = 16;
    coarse.lod           = 1;
    coarse.edges         = {};

    MeshData fineMesh;
    MeshData coarseMesh;
    ASSERT_TRUE(buildPatchMesh(fine, fineMesh));
    ASSERT_TRUE(buildPatchMesh(coarse, coarseMesh));

    std::vector<Vector3f> fineEdge;
    std::vector<Vector3f> coarseEdge;
    CollectEdgePositions(fineMesh, 16, 1, fineEdge);        // fine east
    CollectEdgePositions(coarseMesh, 8, 3, coarseEdge);     // coarse west

    ASSERT_EQ(fineEdge.size(), 17u);
    ASSERT_EQ(coarseEdge.size(), 9u);
    EXPECT_TRUE(FineEdgeLiesOnCoarse(fineEdge, coarseEdge));

    // Shared even vertices must be bit-identical after welding (same samples).
    for (size_t i = 0; i < coarseEdge.size(); ++i)
        EXPECT_TRUE(Near3(fineEdge[i * 2], coarseEdge[i], 1.0e-5f)) << i;
}

TEST(TerrainLod, WeldedNorthSouthEdge)
{
    HeightMap hm = MakeRamp(33);

    PatchBuildDesc south;
    south.heightMap     = &hm;
    south.originSampleX = 0;
    south.originSampleZ = 0;
    south.chunkCells    = 16;
    south.lod           = 0;
    south.edges         = EdgeMask::fromBits(1u); // north coarser

    PatchBuildDesc north;
    north.heightMap     = &hm;
    north.originSampleX = 0;
    north.originSampleZ = 16;
    north.chunkCells    = 16;
    north.lod           = 1;
    north.edges         = {};

    MeshData a;
    MeshData b;
    ASSERT_TRUE(buildPatchMesh(south, a));
    ASSERT_TRUE(buildPatchMesh(north, b));

    std::vector<Vector3f> southN;
    std::vector<Vector3f> northS;
    CollectEdgePositions(a, 16, 0, southN);
    CollectEdgePositions(b, 8, 2, northS);
    EXPECT_TRUE(FineEdgeLiesOnCoarse(southN, northS));
}

TEST(TerrainWorld, BuildsRestrictedChunks)
{
    TerrainDesc desc;
    desc.chunkCells = 8;
    ASSERT_TRUE(desc.heightMap.createFbm(17, 17, 7u, 4, 3.0f, 1.0f, 2.0f, 0.5f, 2.0f, 8.0f));

    TerrainWorld world;
    ASSERT_TRUE(world.create(std::move(desc)));
    EXPECT_EQ(world.chunksX(), 2);
    EXPECT_EQ(world.chunksZ(), 2);
    EXPECT_EQ(world.maxLod(), 3);

    world.updateLod(Vector3f{ 0.0f, 5.0f, 0.0f });
    world.rebuildDirtyCpuMeshes();

    for (int z = 0; z < world.chunksZ(); ++z)
    {
        for (int x = 0; x < world.chunksX(); ++x)
        {
            const TerrainChunk* c = world.chunk(x, z);
            ASSERT_NE(c, nullptr);
            EXPECT_FALSE(c->cpu.indices.empty());
            EXPECT_LE(c->lod, world.maxLod());
        }
    }

    // Adjacent lods differ by at most 1.
    for (int z = 0; z < world.chunksZ(); ++z)
    {
        for (int x = 0; x < world.chunksX(); ++x)
        {
            const int lod = world.chunk(x, z)->lod;
            if (x + 1 < world.chunksX())
                EXPECT_LE(std::abs(lod - world.chunk(x + 1, z)->lod), 1);
            if (z + 1 < world.chunksZ())
                EXPECT_LE(std::abs(lod - world.chunk(x, z + 1)->lod), 1);
        }
    }
}

TEST(TerrainWorld, NeighborSeamsWelded)
{
    TerrainDesc desc;
    desc.chunkCells          = 8;
    desc.lodDistanceCount    = 4;
    desc.lodDistances[0]     = 6.0f;
    desc.lodDistances[1]     = 12.0f;
    desc.lodDistances[2]     = 24.0f;
    desc.lodDistances[3]     = 48.0f;
    ASSERT_TRUE(desc.heightMap.createFbm(17, 17, 99u, 5, 4.0f, 1.0f, 2.0f, 0.5f, 1.0f, 4.0f));

    TerrainWorld world;
    ASSERT_TRUE(world.create(std::move(desc)));

    // Camera over the SW chunk so NE is coarser.
    const float half = 8.0f * 0.5f;
    world.updateLod(Vector3f{ half, 2.0f, half });
    world.rebuildDirtyCpuMeshes();

    int maxDelta = 0;
    for (int z = 0; z < 2; ++z)
    {
        for (int x = 0; x < 2; ++x)
        {
            if (x + 1 < 2)
                maxDelta = std::max(maxDelta, std::abs(world.chunk(x, z)->lod - world.chunk(x + 1, z)->lod));
            if (z + 1 < 2)
                maxDelta = std::max(maxDelta, std::abs(world.chunk(x, z)->lod - world.chunk(x, z + 1)->lod));
        }
    }
    EXPECT_LE(maxDelta, 1);

    auto seamOk = [&](int ax, int az, int bx, int bz, int aEdge, int bEdge)
    {
        const TerrainChunk* a = world.chunk(ax, az);
        const TerrainChunk* b = world.chunk(bx, bz);
        const int aCells = 8 / lodStep(a->lod);
        const int bCells = 8 / lodStep(b->lod);
        std::vector<Vector3f> ea;
        std::vector<Vector3f> eb;
        CollectEdgePositions(a->cpu, aCells, aEdge, ea);
        CollectEdgePositions(b->cpu, bCells, bEdge, eb);
        if (a->lod <= b->lod)
            return FineEdgeLiesOnCoarse(ea, eb);
        return FineEdgeLiesOnCoarse(eb, ea);
    };

    EXPECT_TRUE(seamOk(0, 0, 1, 0, 1, 3)); // east-west
    EXPECT_TRUE(seamOk(0, 0, 0, 1, 0, 2)); // north-south
}
