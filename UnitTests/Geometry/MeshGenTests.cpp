#include <gtest/gtest.h>

#include "Geometry/MeshGen.h"
#include "Math/MathHelper.h"

using namespace Dark::Geometry;
using namespace Dark::Math;

namespace
{

int CountTrianglesWithCrossYSign(const MeshData& mesh, float ySign)
{
    int count = 0;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const Vector3f& a = mesh.positions[mesh.indices[t]];
        const Vector3f& b = mesh.positions[mesh.indices[t + 1]];
        const Vector3f& c = mesh.positions[mesh.indices[t + 2]];
        const float cy = (b - a).Cross(c - a).y;
        if (cy * ySign > 0.0f)
            ++count;
    }
    return count;
}

} // namespace

TEST(MeshGen, CuboidTopIsFrontFacingFromAbove)
{
    MeshData cube;
    CreateCube(cube, 2.0f);
    ASSERT_GE(cube.indices.size(), 6u);

    // First face emitted by CreateCuboid is +Y. Those two triangles must be
    // CW in Y-up (geometric cross Y < 0) to pass D3D backface cull from above.
    int topFront = 0;
    int topBack  = 0;
    for (size_t t = 0; t < 6 && t + 2 < cube.indices.size(); t += 3)
    {
        const Vector3f& a = cube.positions[cube.indices[t]];
        const Vector3f& b = cube.positions[cube.indices[t + 1]];
        const Vector3f& c = cube.positions[cube.indices[t + 2]];
        EXPECT_NEAR(a.y, 1.0f, 1.0e-4f);
        EXPECT_NEAR(b.y, 1.0f, 1.0e-4f);
        EXPECT_NEAR(c.y, 1.0f, 1.0e-4f);
        const float cy = (b - a).Cross(c - a).y;
        if (cy < 0.0f)
            ++topFront;
        else if (cy > 0.0f)
            ++topBack;
    }
    EXPECT_EQ(topFront, 2);
    EXPECT_EQ(topBack, 0);
}

TEST(MeshGen, GroundPlaneIsFrontFacingFromAbove)
{
    MeshData ground;
    CreateGroundPlane(ground, 10.0f, 0.0f, 1.0f);
    EXPECT_EQ(CountTrianglesWithCrossYSign(ground, -1.0f), 2);
    EXPECT_EQ(CountTrianglesWithCrossYSign(ground, 1.0f), 0);
}

TEST(MeshGen, QuadXYIsCenteredInPlane)
{
    MeshData q;
    CreateQuadXY(q, 4.0f, 2.0f);
    ASSERT_EQ(q.positions.size(), 4u);
    ASSERT_EQ(q.indices.size(), 6u);
    ASSERT_EQ(q.uvs.size(), 4u);

    Vector3f minP(1.0e9f, 1.0e9f, 1.0e9f);
    Vector3f maxP(-1.0e9f, -1.0e9f, -1.0e9f);
    for (const Vector3f& p : q.positions)
    {
        EXPECT_NEAR(p.z, 0.0f, 1.0e-5f);
        minP.x = Min(minP.x, p.x);
        minP.y = Min(minP.y, p.y);
        maxP.x = Max(maxP.x, p.x);
        maxP.y = Max(maxP.y, p.y);
    }
    EXPECT_NEAR(minP.x, -2.0f, 1.0e-5f);
    EXPECT_NEAR(maxP.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(minP.y, -1.0f, 1.0e-5f);
    EXPECT_NEAR(maxP.y, 1.0f, 1.0e-5f);
}

TEST(MeshGen, CrossSpansBothAxes)
{
    MeshData cross;
    ASSERT_TRUE(CreateCross(cross, 2.0f, 0.4f, 0.3f));
    ASSERT_GE(cross.positions.size(), 16u);
    ASSERT_GE(cross.indices.size(), 72u);

    float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f, minZ = 1.0e9f, maxZ = -1.0e9f;
    for (const Vector3f& p : cross.positions)
    {
        minX = Min(minX, p.x);
        maxX = Max(maxX, p.x);
        minY = Min(minY, p.y);
        maxY = Max(maxY, p.y);
        minZ = Min(minZ, p.z);
        maxZ = Max(maxZ, p.z);
    }
    EXPECT_NEAR(minX, -1.0f, 1.0e-4f);
    EXPECT_NEAR(maxX, 1.0f, 1.0e-4f);
    EXPECT_NEAR(minY, -1.0f, 1.0e-4f);
    EXPECT_NEAR(maxY, 1.0f, 1.0e-4f);
    EXPECT_NEAR(minZ, -0.15f, 1.0e-4f);
    EXPECT_NEAR(maxZ, 0.15f, 1.0e-4f);
}

TEST(MeshGen, BoxOutlineXYHasFourEdges)
{
    LineMeshData outline;
    CreateBoxOutlineXY(outline);
    EXPECT_EQ(outline.positions.size(), 4u);
    EXPECT_EQ(outline.indices.size(), 8u);
}
