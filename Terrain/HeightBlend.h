#pragma once

#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Terrain/SplatMap.h"

#include <cstdint>

namespace Dark
{
namespace Terrain
{

bool heightBlendWeights(const float height[kMaxTerrainLayers], const float splat[kMaxTerrainLayers], float k, float t, float outW[kMaxTerrainLayers]);

Math::Vector3f whiteoutBlend(const Math::Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers]);
Math::Vector3f lerpRgbNormals(const Math::Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers]);
Math::Vector3f lerpRgbNormal(const Math::Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers]);
Math::Vector3f whiteoutNormal(const Math::Vector3f& geomN, const Math::Vector3f& tangentN);

Math::Vector4f packTerrainAttrib(const Math::Vector3f& nWorld, float roughness, float metallic);

inline float layerTilingWorldScale(float tilingRepeatsAcrossMap, float worldSizeX, float worldSizeZ)
{
    const float extent = worldSizeX > worldSizeZ ? worldSizeX : worldSizeZ;
    return (extent > 1.0e-3f) ? (tilingRepeatsAcrossMap / extent) : 0.0f;
}

inline uint32_t packLayerTintLinear(const float tint[4])
{
    if (!tint)
        return 255u << 24;
    const uint32_t r = static_cast<uint32_t>(Math::Clamp(tint[0] * 255.0f + 0.5f, 0.0f, 255.0f));
    const uint32_t g = static_cast<uint32_t>(Math::Clamp(tint[1] * 255.0f + 0.5f, 0.0f, 255.0f));
    const uint32_t b = static_cast<uint32_t>(Math::Clamp(tint[2] * 255.0f + 0.5f, 0.0f, 255.0f));
    return r | (g << 8) | (b << 16) | (255u << 24);
}

} // namespace Terrain
} // namespace Dark
