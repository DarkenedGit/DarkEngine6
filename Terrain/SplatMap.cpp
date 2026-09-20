#include "Terrain/SplatMap.h"
#include "Terrain/HeightMap.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"

#include <cmath>
#include <cstring>

namespace Dark::Terrain
{
    using namespace Math;

    float ChannelWeight(float value, float start, float end, float blend)
    {
        if (blend <= 1.0e-5f)
            return (value >= start && value <= end) ? 1.0f : 0.0f;
        const float a = SmoothStep(start - blend, start + blend, value);
        const float b = 1.0f - SmoothStep(end - blend, end + blend, value);
        const float w = a * b;
        return w < 0.0f ? 0.0f : w;
    }

    bool SplatMap::create(uint32_t width, uint32_t height)
    {
        return createWithMax(width, height, kMaxSplatMapSize);
    }

    bool SplatMap::createWorking(uint32_t width, uint32_t height)
    {
        return createWithMax(width, height, kMaxWorkingSplatMapSize);
    }

    bool SplatMap::createWithMax(uint32_t width, uint32_t height, uint32_t maxSize)
    {
        if (width == 0 || height == 0)
        {
            DE_LOG_ERROR("SplatMap: size must be > 0");
            return false;
        }
        if (width > maxSize || height > maxSize)
        {
            DE_LOG_ERROR("SplatMap: {}x{} exceeds {}", width, height, maxSize);
            return false;
        }
        m_width  = width;
        m_height = height;
        m_rgba.assign(static_cast<size_t>(width) * height * 4u, 0);
        return true;
    }

    bool SplatMap::createFromRGBA(uint32_t width, uint32_t height, const uint8_t* rgba)
    {
        if (!rgba)
        {
            DE_LOG_ERROR("SplatMap: null rgba");
            return false;
        }
        if (!create(width, height))
            return false;
        memcpy(m_rgba.data(), rgba, m_rgba.size());
        return true;
    }

    bool SplatMap::generateFromHeight(const HeightMap& heightMap, const SplatRules& rules)
    {
        if (!heightMap.valid())
        {
            DE_LOG_ERROR("SplatMap::generateFromHeight: invalid height map");
            return false;
        }
        if (!createWorking(heightMap.width(), heightMap.height()))
            return false;

        float minH = heightMap.height(0, 0);
        float maxH = minH;
        for (uint32_t z = 0; z < m_height; ++z)
        {
            for (uint32_t x = 0; x < m_width; ++x)
            {
                const float h = heightMap.height(static_cast<int>(x), static_cast<int>(z));
                if (h < minH)
                    minH = h;
                if (h > maxH)
                    maxH = h;
            }
        }
        const float span = (maxH - minH);
        const float invSpan = (span > 1.0e-5f) ? (1.0f / span) : 0.0f;

        for (uint32_t z = 0; z < m_height; ++z)
        {
            for (uint32_t x = 0; x < m_width; ++x)
            {
                const float hNorm = (heightMap.height(static_cast<int>(x), static_cast<int>(z)) - minH) * invSpan;
                const Vector3f n = heightMap.normalAtWorld(
                    heightMap.worldX(static_cast<int>(x)),
                    heightMap.worldZ(static_cast<int>(z)));
                const float slope = 1.0f - Clamp(n.y, 0.0f, 1.0f);

                float w[kMaxTerrainLayers];
                w[0] = ChannelWeight(hNorm, 0.0f, rules.dirtMax, rules.blend);
                w[1] = ChannelWeight(hNorm, rules.grassMin, rules.grassMax, rules.blend);
                w[2] = SmoothStep(rules.rockSlope - rules.blend, rules.rockSlope + rules.blend, slope);
                w[3] = ChannelWeight(hNorm, rules.snowMin, 1.0f, rules.blend);

                // Flattened surfaces keep dirt/grass; steep slopes push rock.
                const float flat = 1.0f - w[2];
                w[0] *= flat;
                w[1] *= flat;

                float sum = w[0] + w[1] + w[2] + w[3];
                if (sum < 1.0e-5f)
                {
                    w[1] = 1.0f;
                    sum  = 1.0f;
                }
                const float inv = 255.0f / sum;
                setTexel(
                    static_cast<int>(x),
                    static_cast<int>(z),
                    static_cast<uint8_t>(Clamp(w[0] * inv, 0.0f, 255.0f)),
                    static_cast<uint8_t>(Clamp(w[1] * inv, 0.0f, 255.0f)),
                    static_cast<uint8_t>(Clamp(w[2] * inv, 0.0f, 255.0f)),
                    static_cast<uint8_t>(Clamp(w[3] * inv, 0.0f, 255.0f)));
            }
        }
        return true;
    }

    void SplatMap::setTexel(int x, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if (!valid())
            return;
        x = clampX(x);
        z = clampZ(z);
        uint8_t* p = &m_rgba[(static_cast<size_t>(z) * m_width + static_cast<size_t>(x)) * 4u];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }

    bool SplatMap::renormalizeTexel(int x, int z)
    {
        if (!valid())
            return false;
        x = clampX(x);
        z = clampZ(z);
        uint8_t* p = &m_rgba[(static_cast<size_t>(z) * m_width + static_cast<size_t>(x)) * 4u];
        const int sum = static_cast<int>(p[0]) + static_cast<int>(p[1]) + static_cast<int>(p[2]) + static_cast<int>(p[3]);
        if (sum <= 0)
        {
            p[0] = 0;
            p[1] = 255; // empty weights: grass, matching generateFromHeight
            p[2] = 0;
            p[3] = 0;
            return true;
        }
        if (sum == 255)
            return true;

        int out[kMaxTerrainLayers];
        int outSum = 0;
        int maxI   = 0;
        for (int i = 0; i < kMaxTerrainLayers; ++i)
        {
            out[i] = (static_cast<int>(p[i]) * 255 + sum / 2) / sum;
            if (out[i] < 0)
                out[i] = 0;
            if (out[i] > 255)
                out[i] = 255;
            outSum += out[i];
            if (p[i] > p[maxI])
                maxI = i;
        }
        int adj = out[maxI] + (255 - outSum);
        if (adj < 0)
            adj = 0;
        if (adj > 255)
            adj = 255;
        outSum += adj - out[maxI];
        out[maxI] = adj;
        for (int i = 0; i < kMaxTerrainLayers && outSum != 255; ++i)
        {
            int v = out[i] + (255 - outSum);
            if (v < 0)
                v = 0;
            if (v > 255)
                v = 255;
            outSum += v - out[i];
            out[i] = v;
        }
        for (int i = 0; i < kMaxTerrainLayers; ++i)
            p[i] = static_cast<uint8_t>(out[i]);
        return true;
    }

    void SplatMap::paintTexel(int x, int z, int layer, float amount)
    {
        if (!valid())
            return;
        if (layer < 0 || layer >= kMaxTerrainLayers)
            return;
        x = clampX(x);
        z = clampZ(z);
        uint8_t c[4];
        getTexel(x, z, c);
        const float v = static_cast<float>(c[layer]) + amount * 255.0f;
        c[layer] = static_cast<uint8_t>(Clamp(v, 0.0f, 255.0f));
        setTexel(x, z, c[0], c[1], c[2], c[3]);
        renormalizeTexel(x, z);
    }

    void SplatMap::paintDisk(float sampleX, float sampleZ, float radiusSamples, int layer, float amount)
    {
        if (!valid())
            return;
        if (layer < 0 || layer >= kMaxTerrainLayers)
            return;
        if (radiusSamples <= 0.0f)
        {
            paintTexel(static_cast<int>(floorf(sampleX + 0.5f)), static_cast<int>(floorf(sampleZ + 0.5f)), layer, amount);
            return;
        }

        const int x0 = clampX(static_cast<int>(floorf(sampleX - radiusSamples)));
        const int x1 = clampX(static_cast<int>(ceilf(sampleX + radiusSamples)));
        const int z0 = clampZ(static_cast<int>(floorf(sampleZ - radiusSamples)));
        const int z1 = clampZ(static_cast<int>(ceilf(sampleZ + radiusSamples)));
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const float dx = static_cast<float>(x) - sampleX;
                const float dz = static_cast<float>(z) - sampleZ;
                const float dist = sqrtf(dx * dx + dz * dz);
                if (dist > radiusSamples)
                    continue;
                paintTexel(x, z, layer, amount * (1.0f - dist / radiusSamples));
            }
        }
    }

    void SplatMap::getTexel(int x, int z, uint8_t outRgba[4]) const
    {
        if (!outRgba)
            return;
        if (!valid())
        {
            outRgba[0] = outRgba[1] = outRgba[2] = outRgba[3] = 0;
            return;
        }
        x = clampX(x);
        z = clampZ(z);
        const uint8_t* p = &m_rgba[(static_cast<size_t>(z) * m_width + static_cast<size_t>(x)) * 4u];
        outRgba[0] = p[0];
        outRgba[1] = p[1];
        outRgba[2] = p[2];
        outRgba[3] = p[3];
    }

    void SplatMap::sampleWeights(float fx, float fz, float outW[kMaxTerrainLayers]) const
    {
        if (!outW)
            return;
        if (!valid())
        {
            outW[0] = outW[1] = outW[2] = outW[3] = 0.0f;
            return;
        }

        const float maxX = static_cast<float>(m_width - 1);
        const float maxZ = static_cast<float>(m_height - 1);
        fx = Clamp(fx, 0.0f, maxX);
        fz = Clamp(fz, 0.0f, maxZ);

        const int   x0 = static_cast<int>(floorf(fx));
        const int   z0 = static_cast<int>(floorf(fz));
        const int   x1 = clampX(x0 + 1);
        const int   z1 = clampZ(z0 + 1);
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);

        uint8_t c00[4], c10[4], c01[4], c11[4];
        getTexel(x0, z0, c00);
        getTexel(x1, z0, c10);
        getTexel(x0, z1, c01);
        getTexel(x1, z1, c11);

        float sum = 0.0f;
        for (int i = 0; i < kMaxTerrainLayers; ++i)
        {
            const float a = Lerp(static_cast<float>(c00[i]), static_cast<float>(c10[i]), tx);
            const float b = Lerp(static_cast<float>(c01[i]), static_cast<float>(c11[i]), tx);
            outW[i] = Lerp(a, b, tz) * (1.0f / 255.0f);
            sum += outW[i];
        }
        if (sum > 1.0e-5f)
        {
            const float inv = 1.0f / sum;
            for (int i = 0; i < kMaxTerrainLayers; ++i)
                outW[i] *= inv;
        }
    }

    int SplatMap::clampX(int x) const
    {
        if (x < 0)
            return 0;
        const int maxX = static_cast<int>(m_width) - 1;
        return x > maxX ? maxX : x;
    }

    int SplatMap::clampZ(int z) const
    {
        if (z < 0)
            return 0;
        const int maxZ = static_cast<int>(m_height) - 1;
        return z > maxZ ? maxZ : z;
    }
} // namespace Dark::Terrain
