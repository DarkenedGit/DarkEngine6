#include "Render/TextureMips.h"

#include <cmath>
#include <cstring>

namespace Dark
{
    namespace
    {

        uint32_t bytesPerPixel(MipFilter filter)
        {
            return filter == MipFilter::LinearFloat ? 16u : 4u;
        }

        void texelRange(uint32_t dst, uint32_t dstCount, uint32_t srcCount, uint32_t& begin, uint32_t& end)
        {
            begin = dst << 1u;
            end   = (dst + 1u == dstCount) ? srcCount : (begin + 2u);
            if (begin > srcCount)
                begin = srcCount;
            if (end > srcCount)
                end = srcCount;
        }

        const uint8_t* pixelAt(const MipLevel& level, uint32_t x, uint32_t y, uint32_t bpp)
        {
            return level.pixels.data() + static_cast<size_t>(y) * level.rowPitchBytes + static_cast<size_t>(x) * bpp;
        }

        uint8_t* pixelAt(MipLevel& level, uint32_t x, uint32_t y, uint32_t bpp)
        {
            return level.pixels.data() + static_cast<size_t>(y) * level.rowPitchBytes + static_cast<size_t>(x) * bpp;
        }

        uint8_t roundUnorm(float sum, uint32_t count)
        {
            const float v = sum / static_cast<float>(count) + 0.5f;
            if (v <= 0.0f)
                return 0;
            if (v >= 255.0f)
                return 255;
            return static_cast<uint8_t>(v);
        }

        float decodeNormal(uint8_t u)
        {
            return static_cast<float>(u) / 255.0f * 2.0f - 1.0f;
        }

        uint8_t encodeNormal(float n)
        {
            float u = n * 0.5f + 0.5f;
            if (u < 0.0f)
                u = 0.0f;
            if (u > 1.0f)
                u = 1.0f;
            return static_cast<uint8_t>(u * 255.0f + 0.5f);
        }

        void downsample(const MipLevel& src, MipLevel& dst, MipFilter filter, uint32_t bpp)
        {
            for (uint32_t y = 0; y < dst.height; ++y)
            {
                uint32_t y0 = 0;
                uint32_t y1 = 0;
                texelRange(y, dst.height, src.height, y0, y1);
                for (uint32_t x = 0; x < dst.width; ++x)
                {
                    uint32_t x0 = 0;
                    uint32_t x1 = 0;
                    texelRange(x, dst.width, src.width, x0, x1);
                    const uint32_t count = (x1 - x0) * (y1 - y0);
                    uint8_t*       out   = pixelAt(dst, x, y, bpp);
                    if (count == 0)
                    {
                        std::memset(out, 0, bpp);
                        continue;
                    }

                    if (filter == MipFilter::LinearFloat)
                    {
                        float sum[4] = {};
                        for (uint32_t sy = y0; sy < y1; ++sy)
                        {
                            for (uint32_t sx = x0; sx < x1; ++sx)
                            {
                                float sample[4];
                                std::memcpy(sample, pixelAt(src, sx, sy, bpp), sizeof(sample));
                                sum[0] += sample[0];
                                sum[1] += sample[1];
                                sum[2] += sample[2];
                                sum[3] += sample[3];
                            }
                        }
                        float avg[4] = { sum[0] / static_cast<float>(count), sum[1] / static_cast<float>(count), sum[2] / static_cast<float>(count), sum[3] / static_cast<float>(count) };
                        std::memcpy(out, avg, sizeof(avg));
                        continue;
                    }

                    if (filter == MipFilter::NormalTangent)
                    {
                        float sumN[3] = {};
                        float sumA    = 0.0f;
                        for (uint32_t sy = y0; sy < y1; ++sy)
                        {
                            for (uint32_t sx = x0; sx < x1; ++sx)
                            {
                                const uint8_t* p = pixelAt(src, sx, sy, bpp);
                                sumN[0] += decodeNormal(p[0]);
                                sumN[1] += decodeNormal(p[1]);
                                sumN[2] += decodeNormal(p[2]);
                                sumA += static_cast<float>(p[3]);
                            }
                        }
                        const float inv  = 1.0f / static_cast<float>(count);
                        const float nx   = sumN[0] * inv;
                        const float ny   = sumN[1] * inv;
                        const float nz   = sumN[2] * inv;
                        const float len2 = nx * nx + ny * ny + nz * nz;
                        if (len2 < 1.0e-4f)
                        {
                            out[0] = 128;
                            out[1] = 128;
                            out[2] = 255;
                        }
                        else
                        {
                            const float invLen = 1.0f / std::sqrt(len2);
                            out[0]             = encodeNormal(nx * invLen);
                            out[1]             = encodeNormal(ny * invLen);
                            out[2]             = encodeNormal(nz * invLen);
                        }
                        out[3] = roundUnorm(sumA, count);
                        continue;
                    }

                    if (filter == MipFilter::SrgbColor)
                    {
                        float sumRgb[3] = {};
                        float sumA      = 0.0f;
                        for (uint32_t sy = y0; sy < y1; ++sy)
                        {
                            for (uint32_t sx = x0; sx < x1; ++sx)
                            {
                                const uint8_t* p = pixelAt(src, sx, sy, bpp);
                                sumRgb[0] += Color::srgb8ToLinear(p[0]);
                                sumRgb[1] += Color::srgb8ToLinear(p[1]);
                                sumRgb[2] += Color::srgb8ToLinear(p[2]);
                                sumA += static_cast<float>(p[3]);
                            }
                        }
                        const float inv = 1.0f / static_cast<float>(count);
                        out[0]          = Color::linearToSrgb8(sumRgb[0] * inv);
                        out[1]          = Color::linearToSrgb8(sumRgb[1] * inv);
                        out[2]          = Color::linearToSrgb8(sumRgb[2] * inv);
                        out[3]          = roundUnorm(sumA, count);
                        continue;
                    }

                    float sum[4] = {};
                    for (uint32_t sy = y0; sy < y1; ++sy)
                    {
                        for (uint32_t sx = x0; sx < x1; ++sx)
                        {
                            const uint8_t* p = pixelAt(src, sx, sy, bpp);
                            sum[0] += static_cast<float>(p[0]);
                            sum[1] += static_cast<float>(p[1]);
                            sum[2] += static_cast<float>(p[2]);
                            sum[3] += static_cast<float>(p[3]);
                        }
                    }
                    out[0] = roundUnorm(sum[0], count);
                    out[1] = roundUnorm(sum[1], count);
                    out[2] = roundUnorm(sum[2], count);
                    out[3] = roundUnorm(sum[3], count);
                }
            }
        }

    } // namespace

    uint32_t textureMipCount(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return 0;
        uint32_t count = 1;
        while (width > 1 || height > 1)
        {
            width = width > 1 ? (width >> 1u) : 1u;
            height = height > 1 ? (height >> 1u) : 1u;
            ++count;
        }
        return count;
    }

    uint32_t textureUploadMipCount(uint32_t width, uint32_t height, MipFilter filter)
    {
        if (filter == MipFilter::None)
            return (width == 0 || height == 0) ? 0u : 1u;
        return textureMipCount(width, height);
    }

    MipFilter mipFilterForImage(Color::TextureUsage usage, Color::ColorSpace gpuSpace, ImageFormat format)
    {
        if (format == ImageFormat::R32F || usage == Color::TextureUsage::Font || usage == Color::TextureUsage::Height)
            return MipFilter::None;
        if (format == ImageFormat::RGBA32F)
            return MipFilter::LinearFloat;
        if (format != ImageFormat::RGBA8)
            return MipFilter::None;
        if (usage == Color::TextureUsage::Normal)
            return MipFilter::NormalTangent;
        if ((usage == Color::TextureUsage::Albedo || usage == Color::TextureUsage::Emissive) && gpuSpace == Color::ColorSpace::sRGB)
            return MipFilter::SrgbColor;
        return MipFilter::LinearUnorm;
    }

    bool buildMipChain(const void* base, uint32_t width, uint32_t height, uint32_t rowPitchBytes, MipFilter filter, std::vector<MipLevel>& out)
    {
        out.clear();
        if (!base || width == 0 || height == 0)
            return false;
        if (filter != MipFilter::LinearUnorm && filter != MipFilter::SrgbColor && filter != MipFilter::NormalTangent && filter != MipFilter::LinearFloat)
            return false;

        const uint32_t bpp = bytesPerPixel(filter);
        if (rowPitchBytes < width * bpp)
            return false;

        const uint32_t count = textureMipCount(width, height);
        out.resize(count);

        MipLevel& top    = out[0];
        top.width        = width;
        top.height       = height;
        top.rowPitchBytes = width * bpp;
        top.pixels.resize(static_cast<size_t>(height) * top.rowPitchBytes);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* src = static_cast<const uint8_t*>(base) + static_cast<size_t>(y) * rowPitchBytes;
            uint8_t*       dst = top.pixels.data() + static_cast<size_t>(y) * top.rowPitchBytes;
            std::memcpy(dst, src, top.rowPitchBytes);
        }

        for (uint32_t mip = 1; mip < count; ++mip)
        {
            const MipLevel& src = out[mip - 1];
            MipLevel&       dst = out[mip];
            dst.width           = src.width > 1 ? (src.width >> 1u) : 1u;
            dst.height          = src.height > 1 ? (src.height >> 1u) : 1u;
            dst.rowPitchBytes   = dst.width * bpp;
            dst.pixels.resize(static_cast<size_t>(dst.height) * dst.rowPitchBytes);
            downsample(src, dst, filter, bpp);
        }
        return true;
    }

} // namespace Dark
