#include <gtest/gtest.h>

#include "Particles/LeafRenderer.h"

#include <cmath>
#include <vector>

using namespace Dark;
using Dark::Math::Vector3f;

namespace
{
    Vector3f corner(const ParticleVertex& v)
    {
        return Vector3f(v.px, v.py, v.pz);
    }

    float dot(const Vector3f& a, const Vector3f& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
} // namespace

TEST(LeafCard, AirborneFacesTheCamera)
{
    LeafCardInput leaf;
    leaf.phase = LeafCardPhase::Airborne;
    leaf.x     = 2.0f;
    leaf.y     = 5.0f;
    leaf.z     = -3.0f;
    const Vector3f right(1.0f, 0.0f, 0.0f);
    const Vector3f up(0.0f, 1.0f, 0.0f);

    ParticleVertex verts[6];
    ASSERT_TRUE(buildLeafCard(leaf, right, up, verts));
    for (const ParticleVertex& v : verts)
    {
        EXPECT_NEAR(v.pz, leaf.z, 1.0e-5f);
        EXPECT_FLOAT_EQ(v.a, 1.0f);
    }

    float minX = verts[0].px;
    float maxX = verts[0].px;
    float minY = verts[0].py;
    float maxY = verts[0].py;
    for (const ParticleVertex& v : verts)
    {
        minX = std::min(minX, v.px);
        maxX = std::max(maxX, v.px);
        minY = std::min(minY, v.py);
        maxY = std::max(maxY, v.py);
    }
    EXPECT_NEAR(maxX - minX, 0.16f, 1.0e-4f);
    EXPECT_NEAR(maxY - minY, 0.16f, 1.0e-4f);
}

TEST(LeafCard, RestLiesOnTheNormalAFewCentimetresUp)
{
    LeafCardInput leaf;
    leaf.phase = LeafCardPhase::Rest;
    leaf.x     = 1.0f;
    leaf.y     = 2.0f;
    leaf.z     = 3.0f;
    leaf.nx    = 0.0f;
    leaf.ny    = 0.6f;
    leaf.nz    = 0.8f;
    Vector3f normal(leaf.nx, leaf.ny, leaf.nz);
    normal.Normalize();

    ParticleVertex verts[6];
    ASSERT_TRUE(buildLeafCard(leaf, Vector3f(1, 0, 0), Vector3f(0, 1, 0), verts));

    const Vector3f lifted(leaf.x + normal.x * 0.04f, leaf.y + normal.y * 0.04f, leaf.z + normal.z * 0.04f);
    for (const ParticleVertex& v : verts)
    {
        const Vector3f d(v.px - lifted.x, v.py - lifted.y, v.pz - lifted.z);
        EXPECT_NEAR(dot(d, normal), 0.0f, 1.0e-4f);
        EXPECT_FLOAT_EQ(v.a, 1.0f);
    }
}

TEST(LeafCard, FadeAlphaDropsAndAFreeSlotWritesNothing)
{
    LeafCardInput fade;
    fade.phase  = LeafCardPhase::Fade;
    fade.fade01 = 0.4f;
    fade.ny     = 1.0f;
    ParticleVertex verts[6];
    ASSERT_TRUE(buildLeafCard(fade, Vector3f(1, 0, 0), Vector3f(0, 0, 1), verts));
    for (const ParticleVertex& v : verts)
        EXPECT_NEAR(v.a, 0.6f, 1.0e-5f);

    LeafCardInput empty;
    empty.phase = LeafCardPhase::None;
    EXPECT_FALSE(buildLeafCard(empty, Vector3f(1, 0, 0), Vector3f(0, 1, 0), verts));
}

TEST(LeafCard, EmptyAndFullPoolMatchUploadCapacity)
{
    EXPECT_GE(kLeafQuadCapacity, 256);
    const Vector3f right(1, 0, 0);
    const Vector3f up(0, 1, 0);

    EXPECT_EQ(buildLeafCards(nullptr, 0, right, up, nullptr, 0), 0);

    std::vector<LeafCardInput> leaves(256);
    for (LeafCardInput& leaf : leaves)
    {
        leaf.phase = LeafCardPhase::Airborne;
        leaf.y     = 4.0f;
    }
    std::vector<ParticleVertex> verts(static_cast<size_t>(kLeafQuadCapacity) * 6u);
    EXPECT_EQ(buildLeafCards(leaves.data(), 256, right, up, verts.data(), static_cast<int>(verts.size())), 256);

    leaves[3].phase = LeafCardPhase::None;
    EXPECT_EQ(buildLeafCards(leaves.data(), 256, right, up, verts.data(), static_cast<int>(verts.size())), 255);
}
