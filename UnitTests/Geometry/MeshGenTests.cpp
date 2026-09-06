#include <gtest/gtest.h>

#include "Render/MeshGen.h"
#include "Math/MathHelper.h"

#include <algorithm>
#include <cmath>

using namespace Dark;
using namespace Dark::Math;

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

namespace
{
    bool rayHitsTriangle(const Vector3f& dir, const Vector3f& a, const Vector3f& b, const Vector3f& c, float& t)
    {
        const Vector3f e1  = b - a;
        const Vector3f e2  = c - a;
        const Vector3f pvec = dir.Cross(e2);
        const float    det  = e1.Dot(pvec);
        if (std::fabs(det) < 1.0e-8f)
            return false;
        const float    inv  = 1.0f / det;
        const Vector3f tvec = Vector3f(0.0f, 0.0f, 0.0f) - a;
        const float    u    = tvec.Dot(pvec) * inv;
        if (u < -1.0e-4f || u > 1.0f + 1.0e-4f)
            return false;
        const Vector3f qvec = tvec.Cross(e1);
        const float    v    = dir.Dot(qvec) * inv;
        if (v < -1.0e-4f || (u + v) > 1.0f + 1.0e-4f)
            return false;
        t = e2.Dot(qvec) * inv;
        return t > 1.0e-5f;
    }

    float nearestHit(const MeshData& mesh, const Vector3f& dir)
    {
        float best = 1.0e30f;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            float t = 0.0f;
            if (rayHitsTriangle(dir, mesh.positions[mesh.indices[i]], mesh.positions[mesh.indices[i + 1]], mesh.positions[mesh.indices[i + 2]], t) && t < best)
                best = t;
        }
        return best;
    }

    Vector3f fibonacciDir(int i, int n)
    {
        const float ga = Pi * (3.0f - std::sqrt(5.0f));
        const float y  = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const float r  = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float th = ga * static_cast<float>(i);
        return Vector3f(std::cos(th) * r, y, std::sin(th) * r);
    }
} // namespace

TEST(MeshGen, IcosahedronBoundingCoversUnitSphere)
{
    MeshData mesh;
    ASSERT_TRUE(CreateIcosahedronBounding(mesh, 1.0f, 1));
    ASSERT_GE(mesh.indices.size(), 3u);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const Vector3f& a   = mesh.positions[mesh.indices[i]];
        const Vector3f& b   = mesh.positions[mesh.indices[i + 1]];
        const Vector3f& c   = mesh.positions[mesh.indices[i + 2]];
        const Vector3f  n   = (b - a).Cross(c - a);
        const Vector3f  ctd = (a + b + c) * (1.0f / 3.0f);
        EXPECT_GT(n.Dot(ctd), 0.0f);
    }

    constexpr int kDirs = 256;
    int           hits  = 0;
    for (int i = 0; i < kDirs; ++i)
    {
        const Vector3f dir = fibonacciDir(i, kDirs);
        const float    t   = nearestHit(mesh, dir);
        EXPECT_GE(t, 1.0f - 1.0e-3f) << i;
        if (t < 1.0e29f)
            ++hits;
    }
    EXPECT_EQ(hits, kDirs);
}

TEST(MeshGen, SpotVolumeConeCoversUnitCone)
{
    MeshData mesh;
    ASSERT_TRUE(CreateSpotVolumeCone(mesh, 16, true));
    ASSERT_GE(mesh.indices.size(), 3u);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const Vector3f& a   = mesh.positions[mesh.indices[i]];
        const Vector3f& b   = mesh.positions[mesh.indices[i + 1]];
        const Vector3f& c   = mesh.positions[mesh.indices[i + 2]];
        const Vector3f  n   = (b - a).Cross(c - a);
        const Vector3f  ctd = (a + b + c) * (1.0f / 3.0f);
        if (ctd.z > 0.15f && std::fabs(n.x) < 0.2f && std::fabs(n.y) < 0.2f && n.z > 0.5f)
            EXPECT_GT(n.z, 0.0f);
        else
        {
            const Vector3f radial(ctd.x, ctd.y, 0.0f);
            if (radial.MagnitudeSqrd() > 1.0e-6f)
                EXPECT_GT(n.Dot(radial), -1.0e-4f);
        }
    }

    const float minCos = std::cos(std::atan(1.0f));
    int         inside = 0;
    constexpr int kWant = 256;
    for (int i = 0; i < 2048 && inside < kWant; ++i)
    {
        Vector3f dir = fibonacciDir(i, 2048);
        if (dir.z < minCos)
            continue;
        dir.Normalize();
        const float t = nearestHit(mesh, dir);
        EXPECT_GE(t, 1.0f - 1.0e-3f) << inside;
        ++inside;
    }
    EXPECT_GE(inside, kWant);
}
