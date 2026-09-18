#include "Render/IblBake.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/IblBakePipeline.h"

#include <cmath>
#include <cstring>

namespace Dark
{

    namespace
    {
        float GeometrySchlickGgxIbl(float ndotX, float roughness)
        {
            const float k = (roughness * roughness) * 0.5f;
            return ndotX / std::fmax(ndotX * (1.0f - k) + k, 1.0e-6f);
        }
    } // namespace

    namespace IblBake
    {

        bool validateSettings(const IblBakeSettings& settings)
        {
            if (settings.equirectToCubeSize == 0 || settings.irradianceSize == 0 || settings.prefilterSize == 0 || settings.prefilterMips == 0
                || settings.sampleCountIrr == 0 || settings.sampleCountPref == 0)
                return false;
            // Divide: prefilterMips * 6 can overflow uint32 for huge mips.
            if (settings.prefilterMips > IblBakePipeline::kRtvCount / 6u)
                return false;
            uint32_t maxMips = 0;
            for (uint32_t s = settings.prefilterSize; s > 0; s >>= 1)
                ++maxMips;
            if (settings.prefilterMips > maxMips)
                return false;
            return true;
        }

        Math::Vector2f integrateBrdf(float ndotV, float roughness, uint32_t sampleCount)
        {
            if (sampleCount == 0)
                return Math::Vector2f(0.0f, 0.0f);

            ndotV = Math::Clamp(ndotV, 1.0e-4f, 1.0f);
            roughness = Math::Clamp(roughness, 0.0f, 1.0f);

            const Math::Vector3f n(0.0f, 0.0f, 1.0f);
            const float          sinV = std::sqrt(std::fmax(0.0f, 1.0f - ndotV * ndotV));
            const Math::Vector3f v(sinV, 0.0f, ndotV);

            float scale = 0.0f;
            float bias  = 0.0f;
            for (uint32_t i = 0; i < sampleCount; ++i)
            {
                const Math::Vector2f xi = hammersley(i, sampleCount);
                const Math::Vector3f h  = importanceSampleGgx(xi, n, roughness);
                Math::Vector3f       l  = h * (2.0f * v.Dot(h)) - v;
                l.Normalize();

                const float ndotL = std::fmax(l.z, 0.0f);
                const float ndotH = std::fmax(h.z, 0.0f);
                const float vdotH = std::fmax(v.Dot(h), 0.0f);
                if (ndotL <= 0.0f || ndotH <= 0.0f)
                    continue;

                const float g    = GeometrySchlickGgxIbl(ndotV, roughness) * GeometrySchlickGgxIbl(ndotL, roughness);
                const float gVis = (g * vdotH) / std::fmax(ndotH * ndotV, 1.0e-6f);
                const float fc   = std::pow(1.0f - vdotH, 5.0f);
                scale += (1.0f - fc) * gVis;
                bias += fc * gVis;
            }

            const float invN = 1.0f / static_cast<float>(sampleCount);
            return Math::Vector2f(scale * invN, bias * invN);
        }

        bool generateBrdfLut(float* rgOut, uint32_t size, uint32_t sampleCount)
        {
            if (!rgOut || size == 0 || sampleCount == 0)
            {
                DE_LOG_ERROR(LogCategory::Render, "IblBake::generateBrdfLut: invalid args");
                return false;
            }

            const float invSize = 1.0f / static_cast<float>(size);
            for (uint32_t y = 0; y < size; ++y)
            {
                const float roughness = (static_cast<float>(y) + 0.5f) * invSize;
                for (uint32_t x = 0; x < size; ++x)
                {
                    const float          ndotV = (static_cast<float>(x) + 0.5f) * invSize;
                    const Math::Vector2f dfg   = integrateBrdf(ndotV, roughness, sampleCount);
                    const size_t         i     = (static_cast<size_t>(y) * size + x) * 2u;
                    rgOut[i + 0]               = dfg.x;
                    rgOut[i + 1]               = dfg.y;
                }
            }
            return true;
        }

        Math::Vector3f convolveIrradianceUniformWhite(const Math::Vector3f& n, uint32_t sampleCount)
        {
            if (sampleCount == 0)
                return Math::Vector3f(0.0f, 0.0f, 0.0f);

            Math::Vector3f normal = n;
            normal.Normalize();

            Math::Vector3f e(0.0f, 0.0f, 0.0f);
            for (uint32_t i = 0; i < sampleCount; ++i)
            {
                const Math::Vector2f xi = hammersley(i, sampleCount);
                const Math::Vector3f l  = cosineSampleHemisphere(xi, normal);
                (void)l;
                e += Math::Vector3f(1.0f, 1.0f, 1.0f);
            }
            e *= Math::Pi / static_cast<float>(sampleCount);
            return e;
        }

        Math::Vector3f sampleEquirect(const Image& image, const Math::Vector3f& dir)
        {
            if (!image.valid() || image.format() != ImageFormat::RGBA32F || !image.pixels() || image.width() == 0 || image.height() == 0)
                return Math::Vector3f(0.0f, 0.0f, 0.0f);

            const Math::Vector2f uv = dirToEquirectUv(dir);
            const float          w  = static_cast<float>(image.width());
            const float          h  = static_cast<float>(image.height());
            float                x  = uv.x * w - 0.5f;
            float                y  = uv.y * h - 0.5f;
            x                       = x - w * std::floor(x / w);
            y                       = Math::Clamp(y, 0.0f, h - 1.0f);

            const int x0 = static_cast<int>(x) % static_cast<int>(image.width());
            const int y0 = static_cast<int>(y);
            const int x1 = (x0 + 1) % static_cast<int>(image.width());
            const int y1 = static_cast<int>(std::fmin(static_cast<float>(y0 + 1), h - 1.0f));
            const float fx = x - std::floor(x);
            const float fy = y - std::floor(y);

            const auto texel = [&](int tx, int ty) {
                const uint8_t* row = image.pixels() + static_cast<size_t>(ty) * image.rowPitchBytes();
                float          rgba[4]{};
                std::memcpy(rgba, row + static_cast<size_t>(tx) * 16u, sizeof(rgba));
                return Math::Vector3f(rgba[0], rgba[1], rgba[2]);
            };

            const Math::Vector3f c00 = texel(x0, y0);
            const Math::Vector3f c10 = texel(x1, y0);
            const Math::Vector3f c01 = texel(x0, y1);
            const Math::Vector3f c11 = texel(x1, y1);
            const Math::Vector3f c0  = c00 * (1.0f - fx) + c10 * fx;
            const Math::Vector3f c1  = c01 * (1.0f - fx) + c11 * fx;
            return c0 * (1.0f - fy) + c1 * fy;
        }

    } // namespace IblBake

} // namespace Dark
