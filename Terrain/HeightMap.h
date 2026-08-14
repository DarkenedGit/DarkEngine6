#pragma once

#include "Collision/HitResult.h"
#include "Math/AABox3f.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <vector>

namespace Dark
{
namespace Terrain
{

// Regular-grid height field. Sample (0,0) sits at `origin` in world XZ;
// +X / +Z walk the grid by `cellSize`. Stored heights are multiplied by
// `heightScale` when converted to world Y (origin.y is added).
//
// Height and ray queries share the bilinear patch of each cell:
//   heightAtWorld  — O(1) bilinear evaluate
//   raycast        — min-max pyramid cull + per-cell quadratic
class HeightMap
{
public:
    HeightMap() = default;

    bool create(uint32_t width, uint32_t height, float cellSize = 1.0f, float heightScale = 1.0f);
    bool createFrom(uint32_t width, uint32_t height, const float* samples, float cellSize = 1.0f, float heightScale = 1.0f);
    bool createFromU16(
        uint32_t width,
        uint32_t height,
        const uint16_t* samples,
        float minHeight,
        float maxHeight,
        float cellSize = 1.0f,
        float heightScale = 1.0f);

    // Deterministic fractal value-noise. Amplitude is in raw sample units.
    bool createFbm(
        uint32_t width,
        uint32_t height,
        uint32_t seed,
        int octaves = 5,
        float frequency = 4.0f,
        float amplitude = 1.0f,
        float lacunarity = 2.0f,
        float gain = 0.5f,
        float cellSize = 1.0f,
        float heightScale = 1.0f);

    // Composite another height field (same resolution) on top of this one.
    bool addLayer(const HeightMap& other, float scale = 1.0f);

    void setOrigin(const Math::Vector3f& origin);
    void setCellSize(float cellSize);
    void setHeightScale(float scale);

    void setHeight(int x, int z, float height);
    float height(int x, int z) const;          // raw sample, clamped
    float sampleBilinear(float fx, float fz) const; // raw, sample-space

    float worldY(float rawHeight) const { return m_origin.y + rawHeight * m_heightScale; }

    // O(1) bilinear height. XZ outside the map is clamped to the rim.
    float heightAtWorld(float worldX, float worldZ) const;
    // Same evaluate, but returns false when XZ is outside the map (no clamp).
    bool tryHeightAtWorld(float worldX, float worldZ, float& outY) const;
    bool containsXZ(float worldX, float worldZ) const;

    Math::Vector3f normalAtWorld(float worldX, float worldZ) const;
    Math::Vector3f positionAtSample(int x, int z) const;

    // Closest hit against the bilinear field. t is distance if ray.Direction is unit.
    Collision::RayHit3D raycast(const Math::Ray3f& ray, float maxDistance = Math::Infinity) const;

    float worldX(int x) const { return m_origin.x + static_cast<float>(x) * m_cellSize; }
    float worldZ(int z) const { return m_origin.z + static_cast<float>(z) * m_cellSize; }

    void worldToSample(float worldX, float worldZ, float& outFx, float& outFz) const;

    Math::Aabb3f bounds() const;

    bool     valid() const { return m_width >= 2 && m_height >= 2 && m_samples.size() == size_t(m_width) * m_height; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    float    cellSize() const { return m_cellSize; }
    float    heightScale() const { return m_heightScale; }
    const Math::Vector3f& origin() const { return m_origin; }
    const float* samples() const { return m_samples.data(); }

private:
    struct MinMaxY
    {
        float minY = 0.0f;
        float maxY = 0.0f;
    };

    struct PyramidLevel
    {
        int                  w = 0;
        int                  h = 0;
        std::vector<MinMaxY> nodes;
    };

    int clampX(int x) const;
    int clampZ(int z) const;
    float sampleRaw(int x, int z) const;

    void refreshCached();
    void markAccelDirty() { m_accelDirty = true; }
    void ensureAccel() const;
    void buildAccel() const;

    Math::Aabb3f nodeBounds(int level, int nx, int nz) const;
    bool intersectCell(int cx, int cz, const Math::Ray3f& ray, float tMin, float tMax, float& outT) const;
    bool intersectVertical(const Math::Ray3f& ray, float maxDistance, Collision::RayHit3D& out) const;

    uint32_t             m_width  = 0;
    uint32_t             m_height = 0;
    float                m_cellSize    = 1.0f;
    float                m_heightScale = 1.0f;
    float                m_invCellSize = 1.0f;
    float                m_worldMaxX   = 0.0f;
    float                m_worldMaxZ   = 0.0f;
    Math::Vector3f       m_origin{ 0.0f, 0.0f, 0.0f };
    std::vector<float>   m_samples;

    mutable bool                   m_accelDirty = true;
    mutable std::vector<PyramidLevel> m_pyramid;
    mutable Math::Aabb3f           m_cachedBounds;
};

} // namespace Terrain
} // namespace Dark
