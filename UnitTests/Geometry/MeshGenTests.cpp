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
    const MeshData cube = CreateCube(2.0f);
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
    const MeshData ground = CreateGroundPlane(10.0f, 0.0f, 1.0f);
    EXPECT_EQ(CountTrianglesWithCrossYSign(ground, -1.0f), 2);
    EXPECT_EQ(CountTrianglesWithCrossYSign(ground, 1.0f), 0);
}
