#include <gtest/gtest.h>

#include "Assets/Material.h"
#include "Render/MaterialSurface.h"
#include "Render/MeshConstants.h"

using Dark::applyMaterialSurface;
using Dark::Material;
using Dark::MeshFrameConstants;
using Dark::MeshGBufferConstants;

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
