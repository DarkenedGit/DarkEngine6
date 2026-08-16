#include <gtest/gtest.h>

#include <cmath>

#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRibbon.h"

using namespace Dark;
using namespace Dark::Math;

TEST(ParticleRibbon, ClampRibbonCount)
{
    EXPECT_EQ(clampRibbonCount(0), 1u);
    EXPECT_EQ(clampRibbonCount(1), 1u);
    EXPECT_EQ(clampRibbonCount(4), 4u);
    EXPECT_EQ(clampRibbonCount(kMaxRibbonCount + 8), kMaxRibbonCount);
}

TEST(ParticleRibbon, CollectNeedsTwoAlive)
{
    std::vector<Particle> parts(3);
    parts[0].alive    = true;
    parts[0].ribbonId = 0;
    parts[0].seq      = 0;
    parts[0].maxLife  = 1.0f;
    parts[0].life     = 1.0f;

    std::vector<RibbonNode> nodes;
    collectRibbonNodes(parts, 0, nodes);
    EXPECT_TRUE(nodes.empty());

    parts[1].alive    = true;
    parts[1].ribbonId = 0;
    parts[1].seq      = 1;
    parts[1].maxLife  = 1.0f;
    parts[1].life     = 1.0f;
    collectRibbonNodes(parts, 0, nodes);
    EXPECT_EQ(nodes.size(), 2u);

    collectRibbonNodes(parts, 1, nodes);
    EXPECT_TRUE(nodes.empty());
}

TEST(ParticleRibbon, CollectSortsBySeq)
{
    std::vector<Particle> parts(2);
    parts[0].alive     = true;
    parts[0].ribbonId  = 0;
    parts[0].seq       = 7;
    parts[0].position  = Vector3f(7.0f, 0.0f, 0.0f);
    parts[0].maxLife   = 1.0f;
    parts[0].life      = 1.0f;
    parts[1].alive     = true;
    parts[1].ribbonId  = 0;
    parts[1].seq       = 2;
    parts[1].position  = Vector3f(2.0f, 0.0f, 0.0f);
    parts[1].maxLife   = 1.0f;
    parts[1].life      = 1.0f;

    std::vector<RibbonNode> nodes;
    collectRibbonNodes(parts, 0, nodes);
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_NEAR(nodes[0].position.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(nodes[1].position.x, 7.0f, 1.0e-5f);
}

TEST(ParticleRibbon, DegenerateAppend)
{
    std::vector<ParticleVertex> verts;
    EXPECT_EQ(appendCameraFacingRibbon(nullptr, 0, Vector3f(0, 5, 0), 1.0f, verts), 0u);

    RibbonNode one{};
    one.position = Vector3f(0, 0, 0);
    one.size     = 1.0f;
    EXPECT_EQ(appendCameraFacingRibbon(&one, 1, Vector3f(0, 5, 0), 1.0f, verts), 0u);
    EXPECT_TRUE(verts.empty());
}

TEST(ParticleRibbon, TwoNodesMakeOneQuad)
{
    RibbonNode nodes[2]{};
    nodes[0].position = Vector3f(0.0f, 0.0f, 0.0f);
    nodes[1].position = Vector3f(2.0f, 0.0f, 0.0f);
    nodes[0].size     = 2.0f;
    nodes[1].size     = 2.0f;
    for (int i = 0; i < 4; ++i)
    {
        nodes[0].color[i] = 1.0f;
        nodes[1].color[i] = 1.0f;
    }

    std::vector<ParticleVertex> verts;
    EXPECT_EQ(appendCameraFacingRibbon(nodes, 2, Vector3f(1.0f, 4.0f, 0.0f), 1.0f, verts), 6u);
    ASSERT_EQ(verts.size(), 6u);

    // U runs along the path; first verts at u=0, last at u=1.
    EXPECT_NEAR(verts[0].u, 0.0f, 1.0e-4f);
    EXPECT_NEAR(verts[2].u, 1.0f, 1.0e-4f);
    EXPECT_NEAR(verts[0].v, 0.0f, 1.0e-4f);
    EXPECT_NEAR(verts[1].v, 1.0f, 1.0e-4f);

    const float width0 = std::sqrt(
        (verts[1].px - verts[0].px) * (verts[1].px - verts[0].px)
        + (verts[1].py - verts[0].py) * (verts[1].py - verts[0].py)
        + (verts[1].pz - verts[0].pz) * (verts[1].pz - verts[0].pz));
    EXPECT_NEAR(width0, 2.0f, 1.0e-3f);
}

TEST(ParticleRibbon, FourNodesMakeThreeQuads)
{
    RibbonNode nodes[4]{};
    for (int i = 0; i < 4; ++i)
    {
        nodes[i].position = Vector3f(static_cast<float>(i), 0.0f, 0.0f);
        nodes[i].size     = 0.5f;
        nodes[i].color[0] = nodes[i].color[1] = nodes[i].color[2] = nodes[i].color[3] = 1.0f;
    }

    std::vector<ParticleVertex> verts;
    EXPECT_EQ(appendCameraFacingRibbon(nodes, 4, Vector3f(1.5f, 3.0f, 0.0f), 2.0f, verts), 18u);
    EXPECT_NEAR(verts.front().u, 0.0f, 1.0e-4f);
    EXPECT_NEAR(verts.back().u, 2.0f, 1.0e-4f);
    for (size_t i = 6; i < verts.size(); ++i)
        EXPECT_GE(verts[i].u + 1.0e-5f, verts[i - 6].u);
}

TEST(ParticleRibbon, EmitterRoundRobinsRibbonIds)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.maxParticles = 16;
    desc.emissionRate = 0.0f;
    desc.ribbonCount  = 3;
    emitter.setDesc(desc);
    emitter.stop(true);
    emitter.emitBurst(6);

    EXPECT_EQ(emitter.aliveCount(), 6u);
    uint32_t found = 0;
    for (const Particle& p : emitter.particles())
    {
        if (!p.alive)
            continue;
        EXPECT_EQ(p.ribbonId, found % 3u);
        EXPECT_EQ(p.seq, found / 3u);
        ++found;
    }
    EXPECT_EQ(found, 6u);
}

TEST(ParticleRibbon, AgeLerpsSizeIntoNodes)
{
    std::vector<Particle> parts(2);
    for (int i = 0; i < 2; ++i)
    {
        parts[i].alive    = true;
        parts[i].ribbonId = 0;
        parts[i].seq      = static_cast<uint32_t>(i);
        parts[i].maxLife  = 1.0f;
        parts[i].size0    = 1.0f;
        parts[i].size1    = 0.0f;
    }
    parts[0].life = 1.0f; // t = 0
    parts[1].life = 0.0f; // t = 1, but still alive for this collect

    parts[1].life = 0.25f; // t = 0.75
    std::vector<RibbonNode> nodes;
    collectRibbonNodes(parts, 0, nodes);
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_NEAR(nodes[0].size, 1.0f, 1.0e-5f);
    EXPECT_NEAR(nodes[1].size, 0.25f, 1.0e-5f);
}
