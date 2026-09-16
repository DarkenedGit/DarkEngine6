#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Particles/ParticleMaterials.h"

using Dark::AssetManager;
using Dark::internParticleSpriteMaterial;
using Dark::kParticleBillboardMaterialKey;
using Dark::NULL_ASSET;

TEST(ParticleMaterials, InternIsStable)
{
    AssetManager assets;
    auto         a = internParticleSpriteMaterial(assets, false);
    auto         b = internParticleSpriteMaterial(assets, false);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_NE(a->id, NULL_ASSET);
    EXPECT_EQ(a->id, b->id);
    EXPECT_TRUE(a->isValid());
    EXPECT_FLOAT_EQ(a->emissive(), 1.0f);
}

TEST(ParticleMaterials, BillboardAndRibbonAreDistinct)
{
    AssetManager assets;
    auto         billboard = internParticleSpriteMaterial(assets, false);
    auto         ribbon    = internParticleSpriteMaterial(assets, true);
    ASSERT_TRUE(billboard);
    ASSERT_TRUE(ribbon);
    EXPECT_NE(billboard->id, ribbon->id);
}
