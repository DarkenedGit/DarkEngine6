#pragma once

#include <cstdint>
#include <vector>

namespace Dark
{
namespace Terrain
{

class HeightMap;

constexpr int kMaxTerrainLayers = 4;

struct TerrainLayerDesc
{
    float tiling = 8.0f;
    float tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// Height / slope thresholds used to paint the four splat channels:
//   R = dirt/sand (low, flat)
//   G = grass     (mid, flat)
//   B = rock      (steep)
//   A = snow      (high)
struct SplatRules
{
    float dirtMax     = 0.28f; // normalized height
    float grassMin    = 0.18f;
    float grassMax    = 0.72f;
    float snowMin     = 0.62f;
    float rockSlope   = 0.45f; // 0 = flat, 1 = vertical
    float blend       = 0.08f;
};

// RGBA8 weight map, one texel per height sample. Channels sum is not required
// to be 255; the shader / blender renormalizes.
class SplatMap
{
public:
    bool create(uint32_t width, uint32_t height);
    bool createFromRGBA(uint32_t width, uint32_t height, const uint8_t* rgba);

    // Paint weights from a height map (uses min/max height to normalize).
    bool generateFromHeight(const HeightMap& heightMap, const SplatRules& rules = {});

    void setTexel(int x, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void getTexel(int x, int z, uint8_t outRgba[4]) const;

    // Bilinear normalized weights in [0,1], 4 channels.
    void sampleWeights(float fx, float fz, float outW[kMaxTerrainLayers]) const;

    bool     valid() const { return m_width > 0 && m_height > 0 && m_rgba.size() == size_t(m_width) * m_height * 4u; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    const uint8_t* rgba() const { return m_rgba.data(); }

private:
    int clampX(int x) const;
    int clampZ(int z) const;

    uint32_t            m_width  = 0;
    uint32_t            m_height = 0;
    std::vector<uint8_t> m_rgba;
};

} // namespace Terrain
} // namespace Dark
