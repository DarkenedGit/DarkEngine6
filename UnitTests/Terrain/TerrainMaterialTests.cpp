#include <gtest/gtest.h>

#include "Render/TerrainPipeline.h"
#include "Terrain/HeightBlend.h"
#include "Terrain/TerrainMaterial.h"

#include <cstddef>
#include <cstdint>

using Dark::TerrainFrameConstants;
using Dark::TerrainGBufferConstants;
using Dark::TerrainMaterial;
using Dark::Terrain::TerrainMaterialParams;
using Dark::Terrain::TerrainSurfaceDesc;
using Dark::Terrain::kMaxTerrainLayers;
using Dark::Terrain::packLayerTintLinear;

TEST(TerrainMaterial, SlotMap)
{
    EXPECT_EQ(TerrainMaterial::kAlbedo0, 0u);
    EXPECT_EQ(TerrainMaterial::kNormal0, 1u);
    EXPECT_EQ(TerrainMaterial::kOrm0, 2u);
    EXPECT_EQ(TerrainMaterial::layerAlbedoSlot(0), 0u);
    EXPECT_EQ(TerrainMaterial::layerAlbedoSlot(1), 3u);
    EXPECT_EQ(TerrainMaterial::layerAlbedoSlot(2), 6u);
    EXPECT_EQ(TerrainMaterial::layerAlbedoSlot(3), 9u);
    EXPECT_EQ(TerrainMaterial::layerNormalSlot(0), 1u);
    EXPECT_EQ(TerrainMaterial::layerNormalSlot(3), 10u);
    EXPECT_EQ(TerrainMaterial::layerOrmSlot(0), 2u);
    EXPECT_EQ(TerrainMaterial::layerOrmSlot(3), 11u);
    EXPECT_EQ(TerrainMaterial::kSplatSlot, 12u);
    EXPECT_EQ(TerrainMaterial::kShadowSlot, 13u);
    EXPECT_EQ(TerrainMaterial::kSrvCount, 14u);
    EXPECT_EQ(TerrainMaterial::kMapSrvCount, 13u);
    EXPECT_EQ(Dark::TerrainPipeline::kSrvCount, 14u);
    EXPECT_EQ(Dark::TerrainPipeline::kMapSrvCount, 13u);
    EXPECT_EQ(TerrainMaterial::kMaxLayerImageSize, 2048u);
}

TEST(TerrainGBufferConstants, Size)
{
    EXPECT_EQ(sizeof(TerrainGBufferConstants), 63u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, heightBlendK), 56u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, heightBlendT), 57u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, triplanarSlope), 58u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, layerTint0), 59u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, layerTint1), 60u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, layerTint2), 61u * sizeof(float));
    EXPECT_EQ(offsetof(TerrainGBufferConstants, layerTint3), 62u * sizeof(float));
    EXPECT_LE(63u + 1u, 64u);
    EXPECT_EQ(sizeof(TerrainFrameConstants), 60u * sizeof(float));
    EXPECT_LE(60u + 1u + 2u, 64u);
}

TEST(TerrainMaterial, ApplySurface_GBufferWorldScaleAndTints)
{
    TerrainMaterial mat;
    mat.layer(0).tiling = 24.0f;
    mat.layer(1).tiling = 20.0f;
    mat.layer(2).tiling = 16.0f;
    mat.layer(3).tiling = 12.0f;
    mat.layer(0).tint[0] = 1.0f;
    mat.layer(0).tint[1] = 0.0f;
    mat.layer(0).tint[2] = 0.0f;
    mat.layer(2).tint[0] = 2.0f;
    mat.layer(2).tint[1] = -1.0f;
    mat.layer(2).tint[2] = 0.5f;
    mat.params().heightBlendK   = 0.0f;
    mat.params().heightBlendT   = 0.1f;
    mat.params().triplanarSlope = 0.45f;

    TerrainGBufferConstants cb{};
    mat.applySurface(cb, 256.0f, 256.0f);
    EXPECT_NEAR(cb.layerTiling[0], 24.0f / 256.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[1], 20.0f / 256.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[2], 16.0f / 256.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[3], 12.0f / 256.0f, 1.0e-6f);
    EXPECT_EQ(cb.color[0], 1.0f);
    EXPECT_EQ(cb.color[3], 0.0f);
    EXPECT_EQ(cb.heightBlendK, 0.0f);
    EXPECT_EQ(cb.heightBlendT, 0.1f);
    EXPECT_EQ(cb.triplanarSlope, 0.45f);
    EXPECT_EQ(cb.layerTint0, packLayerTintLinear(mat.layer(0).tint));
    EXPECT_EQ(cb.layerTint0 & 255u, 255u);
    EXPECT_EQ((cb.layerTint0 >> 8) & 255u, 0u);
    EXPECT_EQ(cb.layerTint2 & 255u, 255u);
    EXPECT_EQ((cb.layerTint2 >> 8) & 255u, 0u);
    EXPECT_EQ((cb.layerTint2 >> 16) & 255u, 128u);
}

TEST(TerrainMaterial, ApplySurface_ForwardUnconvertedTiling)
{
    TerrainMaterial mat;
    mat.layer(0).tiling = 24.0f;
    mat.layer(1).tiling = 20.0f;
    mat.layer(2).tiling = 16.0f;
    mat.layer(3).tiling = 12.0f;

    TerrainFrameConstants cb{};
    mat.applySurface(cb);
    EXPECT_NEAR(cb.layerTiling[0], 24.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[1], 20.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[2], 16.0f, 1.0e-6f);
    EXPECT_NEAR(cb.layerTiling[3], 12.0f, 1.0e-6f);
    EXPECT_EQ(cb.color[3], 1.0f);
}

TEST(TerrainMaterial, ApplySurface_ZeroExtentTiling)
{
    TerrainMaterial mat;
    mat.layer(0).tiling = 24.0f;
    TerrainGBufferConstants cb{};
    mat.applySurface(cb, 0.0f, 0.0f);
    EXPECT_EQ(cb.layerTiling[0], 0.0f);
    mat.applySurface(cb, -10.0f, -20.0f);
    EXPECT_EQ(cb.layerTiling[0], 0.0f);
}

TEST(TerrainMaterial, DefaultParams_AuthoredK)
{
    TerrainMaterialParams p{};
    EXPECT_NEAR(p.heightBlendK, 0.5f, 1.0e-6f);
    EXPECT_NEAR(p.heightBlendT, 0.1f, 1.0e-6f);
    EXPECT_NEAR(p.triplanarSlope, 0.45f, 1.0e-6f);
}

TEST(TerrainMaterial, SurfaceDesc_EmptyRefs_UseDefaults)
{
    TerrainSurfaceDesc desc{};
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        EXPECT_FALSE(desc.albedo[i]);
        EXPECT_FALSE(desc.normal[i]);
        EXPECT_FALSE(desc.orm[i]);
        EXPECT_EQ(desc.layers[i].tint[0], 1.0f);
    }
    EXPECT_NEAR(desc.params.heightBlendK, 0.5f, 1.0e-6f);
}

TEST(TerrainMaterial, IsValid_DefaultConstructed)
{
    TerrainMaterial mat;
    EXPECT_FALSE(mat.isValid());
}
