#include "Render/ColorRemapCb.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/SceneBuffers.h"
#include "Render/FrameSetup.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Sky/SkyPipeline.h"
#include "Water/WaterPipeline.h"

#include <cstring>
#include <gtest/gtest.h>

using Dark::ColorRemap;
using Dark::ColorRemapGpu;
using Dark::applyColorRemap;
using Dark::packColorRemap;

namespace
{
    void expectBitsEqual(const float a[3], const float b[3])
    {
        EXPECT_EQ(std::memcmp(a, b, 3 * sizeof(float)), 0);
    }
} // namespace

TEST(ColorRemap, IdentityAndStrengthZeroAreBitIdentical)
{
    const float src[3] = { 0.25f, 0.5f, 0.875f };
    float       dst[3] = { 9, 9, 9 };
    applyColorRemap(dst, src, ColorRemap{});
    expectBitsEqual(dst, src);

    ColorRemap wild{};
    wild.m[0][0] = 0.0f;
    wild.m[0][1] = 2.0f;
    wild.aux     = 0.3f;
    wild.strength = 0.0f;
    applyColorRemap(dst, src, wild);
    expectBitsEqual(dst, src);

    wild.strength = -2.0f;
    applyColorRemap(dst, src, wild);
    expectBitsEqual(dst, src);
}

TEST(ColorRemap, PackClampsStrengthAndCopiesTheMatrix)
{
    ColorRemap src{};
    src.m[1][2]   = 0.4f;
    src.aux       = 0.25f;
    src.strength  = 1.5f;
    const ColorRemapGpu g = packColorRemap(src);
    EXPECT_FLOAT_EQ(g.m0[0], 1.0f);
    EXPECT_FLOAT_EQ(g.m1[2], 0.4f);
    EXPECT_FLOAT_EQ(g.aux, 0.25f);
    EXPECT_FLOAT_EQ(g.strength, 1.0f);

    src.strength          = -0.5f;
    const ColorRemapGpu n = packColorRemap(src);
    EXPECT_FLOAT_EQ(n.strength, 0.0f);

    const ColorRemapGpu id = packColorRemap(ColorRemap{});
    EXPECT_FLOAT_EQ(id.m0[0], 1.0f);
    EXPECT_FLOAT_EQ(id.m1[1], 1.0f);
    EXPECT_FLOAT_EQ(id.m2[2], 1.0f);
    EXPECT_FLOAT_EQ(id.aux, 0.0f);
    EXPECT_FLOAT_EQ(id.strength, 0.0f);
}

TEST(ColorRemap, StrengthOneAppliesTheMatrixAndAux)
{
    // UV-like channel shift: g->r, b->g, aux->b (plan §3.4.2).
    ColorRemap uv{};
    uv.m[0][0] = 0;
    uv.m[0][1] = 1;
    uv.m[0][2] = 0;
    uv.m[0][3] = 0;
    uv.m[1][0] = 0;
    uv.m[1][1] = 0;
    uv.m[1][2] = 1;
    uv.m[1][3] = 0;
    uv.m[2][0] = 0;
    uv.m[2][1] = 0;
    uv.m[2][2] = 0;
    uv.m[2][3] = 1;
    uv.aux       = 0.3f;
    uv.strength  = 1.0f;
    const float src[3] = { 0.1f, 0.2f, 0.4f };
    float       dst[3]{};
    applyColorRemap(dst, src, uv);
    EXPECT_FLOAT_EQ(dst[0], 0.2f);
    EXPECT_FLOAT_EQ(dst[1], 0.4f);
    EXPECT_FLOAT_EQ(dst[2], 0.3f);

    uv.strength = 0.5f;
    applyColorRemap(dst, src, uv);
    EXPECT_FLOAT_EQ(dst[0], 0.1f + (0.2f - 0.1f) * 0.5f);
    EXPECT_FLOAT_EQ(dst[1], 0.2f + (0.4f - 0.2f) * 0.5f);
    EXPECT_FLOAT_EQ(dst[2], 0.4f + (0.3f - 0.4f) * 0.5f);
}

TEST(ColorRemap, ExistingConstantLayoutsAreUnchangedAndRemapsAppend)
{
    EXPECT_EQ(sizeof(Dark::LightingConstants), 57u * sizeof(float));
    EXPECT_EQ(sizeof(Dark::LocalLightPassConstants), 56u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::SkyFrameConstants, thermal), 76u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::SkyFrameConstants, ultraviolet), 77u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::SkyFrameConstants, skyRemap), 80u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::SkyFrameConstants, fogRemap), 96u * sizeof(float));
    EXPECT_EQ(sizeof(Dark::SkyFrameConstants), 112u * sizeof(float));
    EXPECT_LE(sizeof(Dark::SkyFrameConstants), 512u);
    EXPECT_EQ(offsetof(Dark::WaterFrameConstants, underAmount), 186u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::WaterFrameConstants, waterRemap), 188u * sizeof(float));
    EXPECT_EQ(offsetof(Dark::WaterFrameConstants, fogRemap), 204u * sizeof(float));
    EXPECT_EQ(sizeof(Dark::WaterFrameConstants), 220u * sizeof(float));
    EXPECT_EQ((sizeof(Dark::WaterFrameConstants) + 255u) & ~255u, 1024u);
    EXPECT_EQ(sizeof(ColorRemapGpu), 64u);
}

TEST(ColorRemap, PackedIdentityLeavesTheSkyPrefixUnchanged)
{
    Dark::SkyFrameConstants cb{};
    const auto              prefix = 78u * sizeof(float);
    unsigned char           before[78 * sizeof(float)];
    std::memcpy(before, &cb, prefix);
    cb.skyRemap = packColorRemap(ColorRemap{});
    cb.fogRemap = packColorRemap(ColorRemap{});
    EXPECT_EQ(std::memcmp(&cb, before, prefix), 0);
    EXPECT_FLOAT_EQ(cb.skyRemap.strength, 0.0f);
    EXPECT_FLOAT_EQ(cb.fogRemap.strength, 0.0f);
}

TEST(ColorRemap, NoHookDefaultsPackToANoOp)
{
    const Dark::FrameSetup setup{};
    EXPECT_EQ(setup.albedoRemap, ColorRemap{});
    EXPECT_EQ(setup.fogRemap, ColorRemap{});
    EXPECT_EQ(setup.waterRemap, ColorRemap{});
    EXPECT_EQ(setup.skyRemap, ColorRemap{});
    EXPECT_FLOAT_EQ(packColorRemap(setup.albedoRemap).strength, 0.0f);
    EXPECT_FLOAT_EQ(packColorRemap(setup.fogRemap).strength, 0.0f);
}

TEST(ColorRemap, LightingTablesIncludeTheColorRemapCbv)
{
    EXPECT_EQ(Dark::SceneBuffers::kLightingSsr, 9u);
    EXPECT_EQ(Dark::SceneBuffers::kLightingColorRemap, 10u);
    EXPECT_EQ(Dark::SceneBuffers::kLightingCount, 11u);
    EXPECT_EQ(Dark::LocalLightVolumePipeline::kTableColorRemap, 7u);
    EXPECT_EQ(Dark::LocalLightVolumePipeline::kTableCount, 8u);
}

TEST(ColorRemap, AMissingCbvHasNoCpuHandle)
{
    // A CBV that was never created must not present a descriptor for rgTable to copy.
    Dark::ColorRemapCb cb;
    EXPECT_FALSE(cb.valid());
    EXPECT_EQ(cb.cpu(0).ptr, 0ull);
    EXPECT_EQ(cb.cpu(1).ptr, 0ull);
}
