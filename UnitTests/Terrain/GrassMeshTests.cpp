#include <gtest/gtest.h>

#include "Terrain/GrassMesh.h"

#include <cmath>
#include <cstdint>

using namespace Dark;
using namespace Dark::Terrain;

namespace
{
    void dirty(MeshData& mesh)
    {
        mesh.positions.push_back(Math::Vector3f(1.0f, 2.0f, 3.0f));
        mesh.normals.push_back(Math::Vector3f(1.0f, 0.0f, 0.0f));
        mesh.uvs.push_back(Math::Vector2f(0.5f, 0.5f));
        mesh.tangents.push_back(Math::Vector4f(0.0f, 1.0f, 0.0f, -1.0f));
        mesh.indices.push_back(7u);
    }

    void expectEmpty(const MeshData& mesh)
    {
        EXPECT_TRUE(mesh.positions.empty());
        EXPECT_TRUE(mesh.normals.empty());
        EXPECT_TRUE(mesh.uvs.empty());
        EXPECT_TRUE(mesh.tangents.empty());
        EXPECT_TRUE(mesh.indices.empty());
    }

    int countY(const MeshData& mesh, float y)
    {
        int n = 0;
        for (const Math::Vector3f& p : mesh.positions)
        {
            if (p.y == y)
                ++n;
        }
        return n;
    }
} // namespace

TEST(GrassMesh, LodCountsAndBladeShape)
{
    const int   kVerts[4]   = { 10, 6, 4, 4 };
    const int   kIndices[4] = { 24, 12, 6, 6 };
    const int   kJoints[4]  = { 5, 3, 2, 2 };
    const float kT0[5]      = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
    const float kT1[3]      = { 0.0f, 0.50f, 1.0f };
    const float kT2[2]      = { 0.0f, 1.0f };

    MeshData lod2;
    MeshData lod3;
    for (int lod = 0; lod < 4; ++lod)
    {
        MeshData mesh;
        ASSERT_TRUE(buildGrassBladeMesh(lod, mesh));
        EXPECT_EQ(static_cast<int>(mesh.positions.size()), kVerts[lod]);
        EXPECT_EQ(static_cast<int>(mesh.indices.size()), kIndices[lod]);
        EXPECT_EQ(mesh.normals.size(), mesh.positions.size());
        EXPECT_EQ(mesh.uvs.size(), mesh.positions.size());
        EXPECT_EQ(mesh.tangents.size(), mesh.positions.size());
        EXPECT_EQ(countY(mesh, 0.0f), 2);
        EXPECT_EQ(countY(mesh, 1.0f), 2);

        const float* ts = kT0;
        if (lod == 1)
            ts = kT1;
        else if (lod >= 2)
            ts = kT2;

        for (int j = 0; j < kJoints[lod]; ++j)
        {
            const Math::Vector3f& left  = mesh.positions[static_cast<size_t>(j * 2)];
            const Math::Vector3f& right = mesh.positions[static_cast<size_t>(j * 2 + 1)];
            EXPECT_FLOAT_EQ(left.y, ts[j]);
            EXPECT_FLOAT_EQ(right.y, ts[j]);
            EXPECT_LT(left.x, 0.0f);
            EXPECT_GT(right.x, 0.0f);
        }

        for (size_t i = 0; i < mesh.positions.size(); ++i)
        {
            const Math::Vector3f& p = mesh.positions[i];
            EXPECT_GE(p.y, 0.0f);
            EXPECT_LE(p.y, 1.0f);
            EXPECT_FLOAT_EQ(p.z, 0.0f);
            const float half = 0.035f * (1.0f - p.y) + 0.004f * p.y;
            EXPECT_FLOAT_EQ(std::fabs(p.x), half);
            EXPECT_GT(std::fabs(p.x), 0.003f);
            EXPECT_LT(std::fabs(p.x), 0.036f);

            EXPECT_FLOAT_EQ(mesh.normals[i].x, 0.0f);
            EXPECT_FLOAT_EQ(mesh.normals[i].y, 0.0f);
            EXPECT_FLOAT_EQ(mesh.normals[i].z, 1.0f);
            EXPECT_FLOAT_EQ(mesh.uvs[i].y, p.y);
            EXPECT_FLOAT_EQ(mesh.uvs[i].x, p.x < 0.0f ? 0.0f : 1.0f);
            EXPECT_FLOAT_EQ(mesh.tangents[i].x, 1.0f);
            EXPECT_FLOAT_EQ(mesh.tangents[i].y, 0.0f);
            EXPECT_FLOAT_EQ(mesh.tangents[i].z, 0.0f);
            EXPECT_FLOAT_EQ(mesh.tangents[i].w, 1.0f);
        }

        ASSERT_EQ(mesh.indices.size() % 3, 0u);
        for (size_t tri = 0; tri + 2 < mesh.indices.size(); tri += 3)
        {
            const uint32_t i0 = mesh.indices[tri];
            const uint32_t i1 = mesh.indices[tri + 1];
            const uint32_t i2 = mesh.indices[tri + 2];
            ASSERT_LT(i0, mesh.positions.size());
            ASSERT_LT(i1, mesh.positions.size());
            ASSERT_LT(i2, mesh.positions.size());
            const Math::Vector3f& a  = mesh.positions[i0];
            const Math::Vector3f& b  = mesh.positions[i1];
            const Math::Vector3f& c  = mesh.positions[i2];
            const float           nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            EXPECT_LT(nz, 0.0f);
        }

        if (lod == 2)
            lod2 = mesh;
        if (lod == 3)
            lod3 = mesh;
    }

    ASSERT_EQ(lod2.positions.size(), lod3.positions.size());
    ASSERT_EQ(lod2.indices.size(), lod3.indices.size());
    for (size_t i = 0; i < lod2.positions.size(); ++i)
    {
        EXPECT_FLOAT_EQ(lod2.positions[i].x, lod3.positions[i].x);
        EXPECT_FLOAT_EQ(lod2.positions[i].y, lod3.positions[i].y);
        EXPECT_FLOAT_EQ(lod2.positions[i].z, lod3.positions[i].z);
    }
    for (size_t i = 0; i < lod2.indices.size(); ++i)
        EXPECT_EQ(lod2.indices[i], lod3.indices[i]);
}

TEST(GrassMesh, BadLodLeavesMeshEmpty)
{
    MeshData mesh;
    dirty(mesh);
    EXPECT_FALSE(buildGrassBladeMesh(-1, mesh));
    expectEmpty(mesh);

    dirty(mesh);
    EXPECT_FALSE(buildGrassBladeMesh(4, mesh));
    expectEmpty(mesh);
}
