#include <gtest/gtest.h>

#include "Render/Mesh.h"

#include <cstdint>

using namespace Dark;

namespace
{
uint32_t weightSum(uint32_t packed)
{
    return (packed & 0xFFu) + ((packed >> 8) & 0xFFu) + ((packed >> 16) & 0xFFu) + ((packed >> 24) & 0xFFu);
}
} // namespace

TEST(SkinnedMeshVertex, StrideIs48)
{
    EXPECT_EQ(sizeof(SkinnedMeshVertex), 48u);
}

TEST(SkinnedMeshVertex, PackWeightsSumTo255)
{
    EXPECT_EQ(packBlendWeightsUnorm8(1.0f, 0.0f, 0.0f, 0.0f), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(1.0f, 0.0f, 0.0f, 0.0f)), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(0.5f, 0.5f, 0.0f, 0.0f)), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(0.25f, 0.25f, 0.25f, 0.25f)), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(0.4f, 0.3f, 0.2f, 0.1f)), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(0.0f, 0.0f, 0.0f, 0.0f)), 255u);
    EXPECT_EQ(weightSum(packBlendWeightsUnorm8(0.7f, 0.1f, 0.1f, 0.1f)), 255u);
}

TEST(SkinnedMeshVertex, PackRemainderGoesToMaxWeight)
{
    const uint32_t packed = packBlendWeightsUnorm8(0.9f, 0.05f, 0.04f, 0.01f);
    const uint32_t b0     = packed & 0xFFu;
    const uint32_t b1     = (packed >> 8) & 0xFFu;
    const uint32_t b2     = (packed >> 16) & 0xFFu;
    const uint32_t b3     = (packed >> 24) & 0xFFu;
    EXPECT_EQ(b0 + b1 + b2 + b3, 255u);
    EXPECT_GE(b0, b1);
    EXPECT_GE(b0, b2);
    EXPECT_GE(b0, b3);
}
