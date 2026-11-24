#include "AI/Walkability.h"
#include "AI/WalkabilityMesh.h"
#include "Math/AABox3f.h"
#include "Terrain/HeightMap.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::AI;

namespace
{
    HeightMap MakeFlat(int samples, float cell, float y)
    {
        HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), cell, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }

    bool quadCovers(const WalkabilityMeshData& mesh, float x, float z)
    {
        for (size_t v = 0; v + 3 < mesh.vertices.size(); v += 4)
        {
            const WalkabilityMeshVertex& a = mesh.vertices[v];
            const WalkabilityMeshVertex& c = mesh.vertices[v + 2];
            const float x0 = std::min(a.x, c.x);
            const float x1 = std::max(a.x, c.x);
            const float z0 = std::min(a.z, c.z);
            const float z1 = std::max(a.z, c.z);
            if (x >= x0 && x <= x1 && z >= z0 && z <= z1)
                return true;
        }
        return false;
    }

    const WalkabilityMeshVertex* quadAt(const WalkabilityMeshData& mesh, float x, float z)
    {
        for (size_t v = 0; v + 3 < mesh.vertices.size(); v += 4)
        {
            const WalkabilityMeshVertex& a = mesh.vertices[v];
            const WalkabilityMeshVertex& c = mesh.vertices[v + 2];
            const float x0 = std::min(a.x, c.x);
            const float x1 = std::max(a.x, c.x);
            const float z0 = std::min(a.z, c.z);
            const float z1 = std::max(a.z, c.z);
            if (x >= x0 && x <= x1 && z >= z0 && z <= z1)
                return &a;
        }
        return nullptr;
    }
} // namespace

TEST(WalkabilityMesh, FlatIslandIsOneQuad)
{
    HeightMap hm = MakeFlat(5, 1.0f, 10.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    EXPECT_EQ(w.bakeSerial(), 0u);
    ASSERT_TRUE(w.bake(d));
    EXPECT_EQ(w.bakeSerial(), 1u);

    WalkabilityMeshData mesh;
    ASSERT_TRUE(buildWalkabilityMesh(w, mesh));
    EXPECT_EQ(mesh.vertices.size(), 4u);
    EXPECT_EQ(mesh.indices.size(), 6u);
    EXPECT_FLOAT_EQ(mesh.vertices[0].y, 10.0f + kWalkabilityMeshLift);
    EXPECT_TRUE(quadCovers(mesh, w.cellCenterX(0), w.cellCenterZ(0)));
    EXPECT_TRUE(quadCovers(mesh, w.cellCenterX(3), w.cellCenterZ(3)));
}

TEST(WalkabilityMesh, WetGapAndBlockedCellStayOpen)
{
    HeightMap hm = MakeFlat(9, 1.0f, 10.0f);
    for (int z = 0; z < 9; ++z)
        hm.setHeight(4, z, 0.0f);
    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 1.5f, 10.5f, 1.5f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    Walkability w;
    WalkabilityDesc d;
    d.heightMap   = &hm;
    d.waterLevel  = 5.0f;
    d.agentRadius = 0.8f;
    d.cubes       = &cube;
    d.cubeCount   = 1;
    ASSERT_TRUE(w.bake(d));

    WalkabilityMeshData mesh;
    ASSERT_TRUE(buildWalkabilityMesh(w, mesh));
    EXPECT_GT(mesh.vertices.size(), 4u);

    int wetX = 0;
    int wetZ = 0;
    ASSERT_TRUE(w.worldToCell(4.0f, 4.0f, wetX, wetZ));
    EXPECT_FALSE(w.walkable(wetX, wetZ));
    EXPECT_FALSE(quadCovers(mesh, w.cellCenterX(wetX), w.cellCenterZ(wetZ)));

    int blockX = 0;
    int blockZ = 0;
    ASSERT_TRUE(w.worldToCell(1.5f, 1.5f, blockX, blockZ));
    EXPECT_FALSE(w.walkable(blockX, blockZ));
    EXPECT_FALSE(quadCovers(mesh, w.cellCenterX(blockX), w.cellCenterZ(blockZ)));

    int leftX = 0;
    int leftZ = 0;
    int rightX = 0;
    int rightZ = 0;
    ASSERT_TRUE(w.worldToCell(1.0f, 6.0f, leftX, leftZ));
    ASSERT_TRUE(w.worldToCell(7.0f, 6.0f, rightX, rightZ));
    ASSERT_TRUE(w.walkable(leftX, leftZ));
    ASSERT_TRUE(w.walkable(rightX, rightZ));
    EXPECT_TRUE(quadCovers(mesh, w.cellCenterX(leftX), w.cellCenterZ(leftZ)));
    EXPECT_TRUE(quadCovers(mesh, w.cellCenterX(rightX), w.cellCenterZ(rightZ)));

    const WalkabilityMeshVertex* left = quadAt(mesh, w.cellCenterX(leftX), w.cellCenterZ(leftZ));
    const WalkabilityMeshVertex* right = quadAt(mesh, w.cellCenterX(rightX), w.cellCenterZ(rightZ));
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_NE(w.island(leftX, leftZ), w.island(rightX, rightZ));
    const bool sameColor = left->r == right->r && left->g == right->g && left->b == right->b;
    EXPECT_FALSE(sameColor);
}

TEST(WalkabilityMesh, QuadSpanStaysShort)
{
    HeightMap hm = MakeFlat(21, 1.0f, 2.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));

    WalkabilityMeshData mesh;
    ASSERT_TRUE(buildWalkabilityMesh(w, mesh));
    ASSERT_GE(mesh.vertices.size(), 4u);
    const float limit = static_cast<float>(kWalkabilityMeshSpan) * hm.cellSize() + 0.001f;
    for (size_t v = 0; v + 3 < mesh.vertices.size(); v += 4)
    {
        const float dx = std::fabs(mesh.vertices[v + 2].x - mesh.vertices[v].x);
        const float dz = std::fabs(mesh.vertices[v + 2].z - mesh.vertices[v].z);
        EXPECT_LE(dx, limit);
        EXPECT_LE(dz, limit);
    }
}

TEST(WalkabilityMesh, SourceFollowsTheBake)
{
    const Dark::DebugMeshSource none = walkabilityMeshSource(nullptr);
    EXPECT_EQ(none.serial, 0u);
    EXPECT_EQ(none.build, nullptr);

    HeightMap   hm = MakeFlat(5, 1.0f, 10.0f);
    Walkability w;
    EXPECT_EQ(walkabilityMeshSource(&w).serial, 0u);

    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    const Dark::DebugMeshSource baked = walkabilityMeshSource(&w);
    EXPECT_EQ(baked.serial, w.bakeSerial());
    EXPECT_EQ(baked.user, &w);
    ASSERT_NE(baked.build, nullptr);
    WalkabilityMeshData fromSource;
    WalkabilityMeshData direct;
    ASSERT_TRUE(baked.build(baked.user, fromSource));
    ASSERT_TRUE(buildWalkabilityMesh(w, direct));
    EXPECT_EQ(fromSource.vertices.size(), direct.vertices.size());
    EXPECT_EQ(fromSource.indices, direct.indices);

    w.clear();
    const Dark::DebugMeshSource cleared = walkabilityMeshSource(&w);
    EXPECT_EQ(cleared.serial, 0u);
    EXPECT_EQ(cleared.build, nullptr);

    ASSERT_TRUE(w.bake(d));
    const Dark::DebugMeshSource rebaked = walkabilityMeshSource(&w);
    EXPECT_EQ(rebaked.serial, w.bakeSerial());
    EXPECT_NE(rebaked.serial, baked.serial);
}
