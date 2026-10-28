#pragma once

#include "Assets/Image.h"
#include "Math/Color.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    // How lower mip levels are built from mip 0. Mip 0 is copied unchanged.
    enum class MipFilter : uint8_t
    {
        None = 0,      // one level (height fields, BRDF LUT, fonts)
        LinearUnorm,   // RGBA8 average per channel
        SrgbColor,     // RGBA8: RGB in linear light, alpha is coverage
        NormalTangent, // RGBA8 tangent-space normal, renormalized
        LinearFloat,   // RGBA32F, four float channels
    };

    // Tightly packed CPU mip. rowPitchBytes is width * bytes per pixel.
    struct MipLevel
    {
        uint32_t             width         = 0;
        uint32_t             height        = 0;
        uint32_t             rowPitchBytes = 0;
        std::vector<uint8_t> pixels;
    };

    // Non-owning view of one mip, used for the GPU upload.
    struct MipSource
    {
        const void* data          = nullptr;
        uint32_t    width         = 0;
        uint32_t    height        = 0;
        uint32_t    rowPitchBytes = 0;
    };

    // Levels including mip 0. Zero size returns 0.
    uint32_t textureMipCount(uint32_t width, uint32_t height);

    // 1 when filter is None or either dimension is 0. Otherwise textureMipCount.
    uint32_t textureUploadMipCount(uint32_t width, uint32_t height, MipFilter filter);

    // R32F, Height, and Font stay one level. RGBA32F (IBL equirect) is LinearFloat.
    MipFilter mipFilterForImage(Color::TextureUsage usage, Color::ColorSpace gpuSpace, ImageFormat format);

    // Full chain, including a tightly packed mip 0. False on bad input or MipFilter::None.
    // Odd edges fold the leftover source texels into the last destination texel.
    bool buildMipChain(const void* base, uint32_t width, uint32_t height, uint32_t rowPitchBytes, MipFilter filter, std::vector<MipLevel>& out);

} // namespace Dark
