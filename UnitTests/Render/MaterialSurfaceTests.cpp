#include <gtest/gtest.h>

#include "Assets/Material.h"
#include "Render/MaterialSurface.h"
#include "Render/MeshConstants.h"

using Dark::applyMaterialSurface;
using Dark::Material;
using Dark::MeshFrameConstants;
using Dark::MeshGBufferConstants;

TEST(MaterialSurface, ConstantLayouts)
{
    EXPECT_EQ(sizeof(MeshGBufferConstants), 58u * sizeof(float));
    EXPECT_EQ(sizeof(MeshFrameConstants), 53u * sizeof(float));
}

TEST(MaterialSurface, GBufferWritesZeroEmissiveByDefault)
{
    Material mat;
    mat.setBaseColor(0.2f, 0.4f, 0.6f, 0.9f);
    mat.setMetallicRoughness(0.25f, 0.75f);

    MeshGBufferConstants cb{};
    cb.color[3] = 1.0f;
    applyMaterialSurface(mat, cb);

    EXPECT_FLOAT_EQ(cb.color[0], 0.2f);
    EXPECT_FLOAT_EQ(cb.color[1], 0.4f);
    EXPECT_FLOAT_EQ(cb.color[2], 0.6f);
    EXPECT_FLOAT_EQ(cb.color[3], 0.0f);
    EXPECT_FLOAT_EQ(cb.roughness, 0.75f);
    EXPECT_FLOAT_EQ(cb.metallic, 0.25f);
    EXPECT_FLOAT_EQ(cb.ao, 1.0f);
    EXPECT_FLOAT_EQ(cb.normalScale, 1.0f);
    EXPECT_FLOAT_EQ(cb.alphaCutoff, 0.5f);
    EXPECT_FLOAT_EQ(cb.alphaModeMask, 0.0f);
}

TEST(MaterialSurface, GBufferWritesMaterialEmissive)
{
    Material mat;
    mat.setBaseColor(1.0f, 0.5f, 0.2f, 1.0f);
    mat.setEmissive(0.8f);

    MeshGBufferConstants cb{};
    applyMaterialSurface(mat, cb);
    EXPECT_FLOAT_EQ(cb.color[3], 0.8f);
}

TEST(MaterialSurface, ForwardCopiesBaseColorIncludingAlpha)
{
    Material mat;
    mat.setBaseColor(0.1f, 0.2f, 0.3f, 0.8f);

    MeshFrameConstants cb{};
    applyMaterialSurface(mat, cb);

    EXPECT_FLOAT_EQ(cb.color[0], 0.1f);
    EXPECT_FLOAT_EQ(cb.color[1], 0.2f);
    EXPECT_FLOAT_EQ(cb.color[2], 0.3f);
    EXPECT_FLOAT_EQ(cb.color[3], 0.8f);
}

TEST(MaterialSurface, Apply_GBufferConstants)
{
    Material mat;
    mat.setBaseColor(0.2f, 0.4f, 0.6f, 0.9f);
    mat.setMetallicRoughness(0.3f, 0.4f);
    mat.setEmissive(2.0f);
    mat.setEmissiveColor(1.0f, 0.0f, 0.0f);
    mat.setAo(0.25f);
    mat.setNormalScale(1.5f);
    mat.setAlphaCutoff(0.3f);
    mat.setAlphaMode(Dark::MaterialAlphaMode::Mask);

    MeshGBufferConstants cb{};
    applyMaterialSurface(mat, cb);

    EXPECT_FLOAT_EQ(cb.color[0], 0.2f);
    EXPECT_FLOAT_EQ(cb.color[1], 0.4f);
    EXPECT_FLOAT_EQ(cb.color[2], 0.6f);
    EXPECT_FLOAT_EQ(cb.color[3], 2.0f * (0.2126f * 1.0f + 0.7152f * 0.0f + 0.0722f * 0.0f));
    EXPECT_FLOAT_EQ(cb.roughness, 0.4f);
    EXPECT_FLOAT_EQ(cb.metallic, 0.3f);
    EXPECT_FLOAT_EQ(cb.ao, 0.25f);
    EXPECT_FLOAT_EQ(cb.normalScale, 1.5f);
    EXPECT_FLOAT_EQ(cb.alphaCutoff, 0.3f);
    EXPECT_FLOAT_EQ(cb.alphaModeMask, 1.0f);
}

TEST(MaterialSurface, ForwardAppendsMapScalars)
{
    Material mat;
    mat.setBaseColor(0.1f, 0.2f, 0.3f, 0.8f);
    mat.setEmissive(2.0f);
    mat.setEmissiveColor(0.0f, 1.0f, 0.0f);
    mat.setAo(0.5f);
    mat.setNormalScale(1.25f);
    mat.setAlphaCutoff(0.4f);
    mat.setAlphaMode(Dark::MaterialAlphaMode::Opaque);

    MeshFrameConstants cb{};
    applyMaterialSurface(mat, cb);

    EXPECT_FLOAT_EQ(cb.color[3], 0.8f);
    EXPECT_FLOAT_EQ(cb.normalScale, 1.25f);
    EXPECT_FLOAT_EQ(cb.ao, 0.5f);
    EXPECT_FLOAT_EQ(cb.alphaCutoff, 0.4f);
    EXPECT_FLOAT_EQ(cb.alphaModeMask, 0.0f);
    EXPECT_FLOAT_EQ(cb.emissive, 2.0f * (0.2126f * 0.0f + 0.7152f * 1.0f + 0.0722f * 0.0f));
}
