#include <gtest/gtest.h>

#include "Render/GpuMaterial.h"
#include "Render/Texture2D.h"

using Dark::GpuMaterial;
using Dark::Texture2D;

TEST(GpuMaterial, SlotMap)
{
    EXPECT_EQ(GpuMaterial::kAlbedoSlot, 0u);
    EXPECT_EQ(GpuMaterial::kNormalSlot, 1u);
    EXPECT_EQ(GpuMaterial::kOrmSlot, 2u);
    EXPECT_EQ(GpuMaterial::kEmissiveSlot, 3u);
    EXPECT_EQ(GpuMaterial::kShadowSlot, 4u);
    EXPECT_EQ(GpuMaterial::kSrvCount, 5u);
    EXPECT_EQ(GpuMaterial::kMapSrvCount, 4u);
}

TEST(GpuMaterial, Pack_NullAlbedo_Fails)
{
    Texture2D   albedo;
    Texture2D   normal;
    Texture2D   orm;
    Texture2D   emissive;
    GpuMaterial mat;
    EXPECT_FALSE(mat.pack(nullptr, albedo, normal, orm, emissive));
    EXPECT_FALSE(mat.isValid());
}

TEST(GpuMaterial, PackedMapIdsDefaultNull)
{
    GpuMaterial mat;
    const Dark::AssetID* ids = mat.packedMapIds();
    ASSERT_NE(ids, nullptr);
    EXPECT_EQ(ids[GpuMaterial::kAlbedoSlot], Dark::NULL_ASSET);
    EXPECT_EQ(ids[GpuMaterial::kNormalSlot], Dark::NULL_ASSET);
    EXPECT_EQ(ids[GpuMaterial::kOrmSlot], Dark::NULL_ASSET);
    EXPECT_EQ(ids[GpuMaterial::kEmissiveSlot], Dark::NULL_ASSET);
}
