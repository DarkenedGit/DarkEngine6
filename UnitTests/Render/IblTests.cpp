#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Math/MathDefines.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/GpuIbl.h"
#include "Render/GpuResourceCache.h"
#include "Render/IblBake.h"
#include "Render/IblSampling.h"

#include <cmath>
#include <memory>
#include <vector>

using Dark::GpuIbl;
using Dark::GpuResourceCache;
using Dark::IblBakeSettings;
using Dark::Image;
using Dark::ImageFormat;
using Dark::Renderer;
using Dark::cubeFaceDir;
using Dark::dirToEquirectUv;
using Dark::hammersley;
using Dark::iblRotateY;
using Dark::IblBake::convolveIrradianceUniformWhite;
using Dark::IblBake::generateBrdfLut;
using Dark::IblBake::integrateBrdf;
using Dark::IblBake::sampleEquirect;
using Dark::IblBake::validateSettings;
using Dark::Math::Pi;
using Dark::Math::Vector2f;
using Dark::Math::Vector3f;

namespace
{
    Image MakeUniqueColorEquirect(uint32_t w, uint32_t h)
    {
        std::vector<float> px(static_cast<size_t>(w) * h * 4u);
        const float        invW = w > 1 ? 1.0f / static_cast<float>(w - 1u) : 1.0f;
        const float        invH = h > 1 ? 1.0f / static_cast<float>(h - 1u) : 1.0f;
        const float        invN = 1.0f / static_cast<float>(w * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4u;
                px[i + 0]      = static_cast<float>(x) * invW;
                px[i + 1]      = static_cast<float>(y) * invH;
                px[i + 2]      = static_cast<float>(y * w + x) * invN;
                px[i + 3]      = 1.0f;
            }
        }
        Image img;
        EXPECT_TRUE(img.createFromRgba32f(px.data(), w, h, w * 16u));
        return img;
    }
} // namespace

TEST(Ibl, BrdfLut_Rough0_Edge)
{
    const Vector2f dfg = integrateBrdf(1.0f, 0.0f, 1024);
    EXPECT_NEAR(dfg.x, 1.0f, 0.05f);
    EXPECT_NEAR(dfg.y, 0.0f, 0.05f);
}

TEST(Ibl, BrdfLut_Rough1)
{
    constexpr uint32_t size = 8;
    std::vector<float> lut(static_cast<size_t>(size) * size * 2u);
    ASSERT_TRUE(generateBrdfLut(lut.data(), size, 128));
    for (float c : lut)
    {
        EXPECT_TRUE(std::isfinite(c));
        EXPECT_GE(c, 0.0f);
        EXPECT_LE(c, 1.05f);
    }
}

TEST(Ibl, PrefilterMipCount)
{
    const IblBakeSettings settings{};
    EXPECT_EQ(settings.prefilterMips, 5u);
    EXPECT_FLOAT_EQ(settings.maxRoughnessMip(), 4.0f);
}

TEST(Ibl, RotationMatrix_Y90)
{
    // Matches Matrix4f::RotationY (row-vector). +90° takes +X → −Z, not +Z.
    const Vector3f rotated = iblRotateY(Vector3f(1.0f, 0.0f, 0.0f), Pi * 0.5f);
    EXPECT_NEAR(rotated.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(rotated.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(rotated.z, -1.0f, 1.0e-5f);
}

TEST(Ibl, EquirectUv_PosZ)
{
    const Vector2f posZ = dirToEquirectUv(Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(posZ.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(posZ.y, 0.5f, 1.0e-5f);
    const Vector2f posY = dirToEquirectUv(Vector3f(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(posY.y, 0.0f, 1.0e-5f);
}

TEST(Ibl, CubeFaceDir_PosZCenter)
{
    const Vector3f dir = cubeFaceDir(Vector2f(0.0f, 0.0f), 4);
    EXPECT_NEAR(dir.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(dir.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(dir.z, 1.0f, 1.0e-5f);

    const Image    img   = MakeUniqueColorEquirect(16, 8);
    const Vector3f viaFace = sampleEquirect(img, dir);
    const Vector3f viaWorld = sampleEquirect(img, Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(viaFace.x, viaWorld.x, 1.0e-5f);
    EXPECT_NEAR(viaFace.y, viaWorld.y, 1.0e-5f);
    EXPECT_NEAR(viaFace.z, viaWorld.z, 1.0e-5f);
}

TEST(Ibl, CubeFaceDir_PosYCenter)
{
    // +Y up for ±X/±Z, not Filament −Y.
    const Vector3f dir = cubeFaceDir(Vector2f(0.0f, 0.0f), 2);
    EXPECT_NEAR(dir.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(dir.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(dir.z, 0.0f, 1.0e-5f);

    const Image    img      = MakeUniqueColorEquirect(16, 8);
    const Vector3f viaFace  = sampleEquirect(img, dir);
    const Vector3f viaWorld = sampleEquirect(img, Vector3f(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(viaFace.x, viaWorld.x, 1.0e-5f);
    EXPECT_NEAR(viaFace.y, viaWorld.y, 1.0e-5f);
    EXPECT_NEAR(viaFace.z, viaWorld.z, 1.0e-5f);
}

TEST(Ibl, Irradiance_UniformWhite_EqualsPi)
{
    const Vector3f e = convolveIrradianceUniformWhite(Vector3f(0.0f, 1.0f, 0.0f), 64);
    EXPECT_NEAR(e.x, Pi, 0.1f * Pi);
    EXPECT_NEAR(e.y, Pi, 0.1f * Pi);
    EXPECT_NEAR(e.z, Pi, 0.1f * Pi);
}

TEST(Ibl, DiffuseEnergy_WhiteLambert)
{
    const Vector3f albedo(1.0f, 1.0f, 1.0f);
    const Vector3f e      = convolveIrradianceUniformWhite(Vector3f(0.0f, 1.0f, 0.0f), 64);
    const Vector3f fd     = albedo * Dark::Math::InvPi;
    EXPECT_NEAR(fd.x * e.x, albedo.x, 0.10f);
    EXPECT_NEAR(fd.y * e.y, albedo.y, 0.10f);
    EXPECT_NEAR(fd.z * e.z, albedo.z, 0.10f);
}

TEST(Ibl, StudioGradient_InMemory_NoSun)
{
    Image img;
    ASSERT_TRUE(Dark::IblBake::fillStudioGradient(img, 16, 8));
    ASSERT_TRUE(img.valid());
    EXPECT_EQ(img.format(), ImageFormat::RGBA32F);
    EXPECT_EQ(img.width(), 16u);
    EXPECT_EQ(img.height(), 8u);

    const Vector3f zenith = sampleEquirect(img, Vector3f(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(zenith.x, 0.12f, 0.05f);
    EXPECT_NEAR(zenith.y, 0.16f, 0.05f);
    EXPECT_NEAR(zenith.z, 0.22f, 0.05f);

    const Vector3f horizon = sampleEquirect(img, Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(horizon.x, 0.35f, 0.05f);
    EXPECT_NEAR(horizon.y, 0.38f, 0.05f);
    EXPECT_NEAR(horizon.z, 0.42f, 0.05f);

    const auto* px = reinterpret_cast<const float*>(img.pixels());
    ASSERT_NE(px, nullptr);
    float peak = 0.0f;
    for (uint32_t i = 0; i < img.width() * img.height(); ++i)
    {
        if (px[i * 4u + 0] > peak)
            peak = px[i * 4u + 0];
        if (px[i * 4u + 1] > peak)
            peak = px[i * 4u + 1];
        if (px[i * 4u + 2] > peak)
            peak = px[i * 4u + 2];
    }
    EXPECT_LT(peak, 2.0f);
    EXPECT_GT(peak, 0.30f);
}

TEST(Ibl, LoadMissing_ReturnsFalse)
{
    Image missing;
    EXPECT_FALSE(missing.createFromHdrFile("this_ibl_does_not_exist_a54de7b6.hdr"));

    GpuResourceCache cache(static_cast<Renderer*>(nullptr));
    EXPECT_FALSE(cache.ensureIbl({}));
    const auto unregistered = std::make_shared<Image>();
    EXPECT_FALSE(cache.ensureIbl(unregistered));
}

TEST(Ibl, Bake_NullRenderer_ReturnsFalse)
{
    const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    Image       img;
    ASSERT_TRUE(img.createFromRgba32f(white, 1, 1, 16u));
    GpuIbl ibl;
    EXPECT_FALSE(ibl.bake(nullptr, img));
    EXPECT_FALSE(ibl.isReady());
}

TEST(Ibl, BakeSettings_RejectsRtvOverflow)
{
    IblBakeSettings s{};
    EXPECT_TRUE(validateSettings(s));
    s.prefilterMips = 6;
    EXPECT_FALSE(validateSettings(s));
    s.prefilterMips = 5;
    s.prefilterSize = 8; // log2(8)+1 = 4 mips (8,4,2,1)
    EXPECT_FALSE(validateSettings(s));
    s.prefilterMips = 4;
    EXPECT_TRUE(validateSettings(s));
    s.prefilterMips = 0;
    EXPECT_FALSE(validateSettings(s));
}

TEST(IblSampling, Hammersley_First)
{
    const Vector2f h0 = hammersley(0, 1024);
    EXPECT_NEAR(h0.x, 0.0f, 1.0e-6f);
    EXPECT_NEAR(h0.y, 0.0f, 1.0e-6f);
}
