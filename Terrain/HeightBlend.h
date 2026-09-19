#pragma once

#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Terrain/SplatMap.h"

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

} // namespace Terrain
} // namespace Dark
