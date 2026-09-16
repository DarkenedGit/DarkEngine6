#include <gtest/gtest.h>

#include "Particles/ParticleRenderer.h"

using Dark::particleUploadVertOffset;
using Dark::ParticleRenderer;

TEST(ParticleRendererUpload, SequentialDrawsInOneFrameDoNotOverlap)
{
    const uint32_t cap = 1024;
    const uint32_t a   = particleUploadVertOffset(0, cap, 0);
    const uint32_t b   = particleUploadVertOffset(0, cap, 40);
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 40u * 6u);
    EXPECT_LE(a + 40u * 6u, b);
}

TEST(ParticleRendererUpload, FrameSlotsAreDisjoint)
{
    const uint32_t cap = 1024;
    const uint32_t f0  = particleUploadVertOffset(0, cap, 0);
    const uint32_t f1  = particleUploadVertOffset(1, cap, 0);
    EXPECT_EQ(f1, cap * 6u);
    EXPECT_LE(f0 + cap * 6u, f1);
}

TEST(ParticleRendererUpload, RingUsesTwoFrames)
{
    EXPECT_EQ(ParticleRenderer::kFrameCount, 2u);
}
