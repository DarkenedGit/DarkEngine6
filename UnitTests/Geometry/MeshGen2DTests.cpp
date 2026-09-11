#include <gtest/gtest.h>

#include "Render/MeshGen2D.h"
#include "Math/MathHelper.h"
#include "Math/MathDefines.h"

#include <cmath>

using namespace Dark;
using namespace Dark::Math;

namespace
{
int CountTrianglesWithCrossZSign(const MeshData& mesh, float zSign)
{
	int count = 0;
	for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
	{
		const Vector3f& a = mesh.positions[mesh.indices[t]];
		const Vector3f& b = mesh.positions[mesh.indices[t + 1]];
		const Vector3f& c = mesh.positions[mesh.indices[t + 2]];
		const float cz = (b - a).Cross(c - a).z;
		if (cz * zSign > 0.0f)
			++count;
	}
	return count;
}

void ExpectXyMesh(const MeshData& mesh)
{
	ASSERT_EQ(mesh.positions.size(), mesh.normals.size());
	ASSERT_EQ(mesh.positions.size(), mesh.uvs.size());
	ASSERT_EQ(mesh.indices.size() % 3u, 0u);
	ASSERT_GE(mesh.indices.size(), 3u);
	for (uint32_t idx : mesh.indices)
		EXPECT_LT(idx, mesh.positions.size());
	for (const Vector3f& p : mesh.positions)
		EXPECT_NEAR(p.z, 0.0f, 1.0e-5f);
	for (const Vector3f& n : mesh.normals)
	{
		EXPECT_NEAR(n.x, 0.0f, 1.0e-5f);
		EXPECT_NEAR(n.y, 0.0f, 1.0e-5f);
		EXPECT_NEAR(n.z, 1.0f, 1.0e-5f);
	}
	EXPECT_EQ(CountTrianglesWithCrossZSign(mesh, -1.0f), 0);
	EXPECT_EQ(CountTrianglesWithCrossZSign(mesh, 1.0f), static_cast<int>(mesh.indices.size() / 3));
}
} // namespace

TEST(MeshGen2D, SphereDiskIsCentered)
{
	MeshData mesh;
	Sphere2f circle{Vector2f{1.0f, 2.0f}, 3.0f};
	ASSERT_TRUE(CreateSphere2(mesh, circle, 16));
	ExpectXyMesh(mesh);
	EXPECT_EQ(mesh.positions.size(), 17u); // center + 16 rim
	EXPECT_EQ(mesh.indices.size(), 16u * 3u);

	EXPECT_NEAR(mesh.positions[0].x, 1.0f, 1.0e-5f);
	EXPECT_NEAR(mesh.positions[0].y, 2.0f, 1.0e-5f);
	for (size_t i = 1; i < mesh.positions.size(); ++i)
	{
		const float dx = mesh.positions[i].x - 1.0f;
		const float dy = mesh.positions[i].y - 2.0f;
		EXPECT_NEAR(sqrtf(dx * dx + dy * dy), 3.0f, 1.0e-4f);
	}
}

TEST(MeshGen2D, SphereRejectsBadSlices)
{
	MeshData mesh;
	EXPECT_FALSE(CreateSphere2(mesh, Sphere2f{Vector2f::ZERO, 1.0f}, 2));
	EXPECT_TRUE(mesh.positions.empty());
}

TEST(MeshGen2D, AABoxMatchesMinMax)
{
	MeshData mesh;
	AABox2f box{Vector2f{-2.0f, -1.0f}, Vector2f{2.0f, 1.0f}};
	ASSERT_TRUE(CreateAABox2(mesh, box));
	ExpectXyMesh(mesh);
	EXPECT_EQ(mesh.positions.size(), 4u);
	EXPECT_EQ(mesh.indices.size(), 6u);

	float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
	for (const Vector3f& p : mesh.positions)
	{
		minX = Min(minX, p.x);
		maxX = Max(maxX, p.x);
		minY = Min(minY, p.y);
		maxY = Max(maxY, p.y);
	}
	EXPECT_NEAR(minX, -2.0f, 1.0e-5f);
	EXPECT_NEAR(maxX, 2.0f, 1.0e-5f);
	EXPECT_NEAR(minY, -1.0f, 1.0e-5f);
	EXPECT_NEAR(maxY, 1.0f, 1.0e-5f);
}

TEST(MeshGen2D, AABoxRejectsInverted)
{
	MeshData mesh;
	EXPECT_FALSE(CreateAABox2(mesh, AABox2f::Empty()));
	EXPECT_TRUE(mesh.positions.empty());
}

TEST(MeshGen2D, OrientedBoxMatchesCorners)
{
	MeshData mesh;
	Box2f box = Box2f::FromCenterExtents(Vector2f{1.0f, 2.0f}, Vector2f{0.5f, 1.5f});
	ASSERT_TRUE(CreateBox2(mesh, box));
	ExpectXyMesh(mesh);
	EXPECT_EQ(mesh.positions.size(), 4u);

	Vector2f corners[4];
	box.GetCorners(corners);
	auto hasCorner = [&](const Vector2f& c) {
		for (const Vector3f& p : mesh.positions)
		{
			if (NearEqual(p.x, c.x, 1.0e-4f) && NearEqual(p.y, c.y, 1.0e-4f))
				return true;
		}
		return false;
	};
	EXPECT_TRUE(hasCorner(corners[0]));
	EXPECT_TRUE(hasCorner(corners[1]));
	EXPECT_TRUE(hasCorner(corners[2]));
	EXPECT_TRUE(hasCorner(corners[3]));
}

TEST(MeshGen2D, CapsuleVertsLieOnStadium)
{
	MeshData mesh;
	Capsule2f cap{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 0.5f};
	ASSERT_TRUE(CreateCapsule2(mesh, cap, 8));
	ExpectXyMesh(mesh);

	for (const Vector3f& p : mesh.positions)
	{
		const float sd = cap.SignedDistance(Vector2f{p.x, p.y});
		EXPECT_LE(sd, 1.0e-3f);
		EXPECT_GE(sd, -0.5f - 1.0e-3f);
	}

	int onShell = 0;
	for (const Vector3f& p : mesh.positions)
	{
		if (std::fabs(cap.SignedDistance(Vector2f{p.x, p.y})) < 1.0e-3f)
			++onShell;
	}
	EXPECT_GE(onShell, 8);
}

TEST(MeshGen2D, DegenerateCapsuleIsDisk)
{
	MeshData mesh;
	Capsule2f cap{Vector2f{3.0f, 4.0f}, Vector2f{3.0f, 4.0f}, 2.0f};
	ASSERT_TRUE(CreateCapsule2(mesh, cap, 8));
	ExpectXyMesh(mesh);
	EXPECT_EQ(mesh.positions.size(), 1u + 16u); // capSlices * 2 rim verts
	EXPECT_NEAR(mesh.positions[0].x, 3.0f, 1.0e-5f);
	EXPECT_NEAR(mesh.positions[0].y, 4.0f, 1.0e-5f);
}

TEST(MeshGen2D, CapsuleRejectsBadCapSlices)
{
	MeshData mesh;
	Capsule2f cap{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 0.5f};
	EXPECT_FALSE(CreateCapsule2(mesh, cap, 1));
	EXPECT_TRUE(mesh.positions.empty());
}

TEST(MeshGen2D, GeneratorsAppend)
{
	MeshData mesh;
	ASSERT_TRUE(CreateAABox2(mesh, AABox2f{Vector2f{-1.0f, -1.0f}, Vector2f{1.0f, 1.0f}}));
	const size_t firstCount = mesh.positions.size();
	ASSERT_TRUE(CreateSphere2(mesh, Sphere2f{Vector2f{5.0f, 0.0f}, 1.0f}, 8));
	EXPECT_GT(mesh.positions.size(), firstCount);
	ExpectXyMesh(mesh);
}
