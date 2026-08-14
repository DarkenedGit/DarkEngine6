#include "Terrain/HeightMap.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/MathDefines.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace Dark
{
namespace Terrain
{

using namespace Math;

namespace
{

float Hash21(int x, int z, uint32_t seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
        + static_cast<uint32_t>(z) * 668265263u
        + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float ValueNoise(float x, float z, uint32_t seed)
{
    const int   x0 = static_cast<int>(floorf(x));
    const int   z0 = static_cast<int>(floorf(z));
    const float tx = x - static_cast<float>(x0);
    const float tz = z - static_cast<float>(z0);
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sz = tz * tz * (3.0f - 2.0f * tz);

    const float n00 = Hash21(x0, z0, seed);
    const float n10 = Hash21(x0 + 1, z0, seed);
    const float n01 = Hash21(x0, z0 + 1, seed);
    const float n11 = Hash21(x0 + 1, z0 + 1, seed);
    const float nx0 = Lerp(n00, n10, sx);
    const float nx1 = Lerp(n01, n11, sx);
    return Lerp(nx0, nx1, sz);
}

} // namespace

bool HeightMap::create(uint32_t width, uint32_t height, float cellSize, float heightScale)
{
    if (width < 2 || height < 2)
    {
        DE_LOG_ERROR("HeightMap: size must be at least 2x2");
        return false;
    }
    if (cellSize <= 0.0f)
    {
        DE_LOG_ERROR("HeightMap: cellSize must be > 0");
        return false;
    }

    m_width       = width;
    m_height      = height;
    m_cellSize    = cellSize;
    m_heightScale = heightScale;
    m_samples.assign(static_cast<size_t>(width) * height, 0.0f);
    refreshCached();
    return true;
}

bool HeightMap::createFrom(uint32_t width, uint32_t height, const float* samples, float cellSize, float heightScale)
{
    if (!samples)
    {
        DE_LOG_ERROR("HeightMap: null sample pointer");
        return false;
    }
    if (!create(width, height, cellSize, heightScale))
        return false;
    memcpy(m_samples.data(), samples, m_samples.size() * sizeof(float));
    return true;
}

bool HeightMap::createFromU16(
    uint32_t width,
    uint32_t height,
    const uint16_t* samples,
    float minHeight,
    float maxHeight,
    float cellSize,
    float heightScale)
{
    if (!samples)
    {
        DE_LOG_ERROR("HeightMap: null u16 sample pointer");
        return false;
    }
    if (!create(width, height, cellSize, heightScale))
        return false;

    const float span = maxHeight - minHeight;
    const size_t count = m_samples.size();
    for (size_t i = 0; i < count; ++i)
        m_samples[i] = minHeight + (static_cast<float>(samples[i]) / 65535.0f) * span;
    return true;
}

bool HeightMap::createFbm(
    uint32_t width,
    uint32_t height,
    uint32_t seed,
    int octaves,
    float frequency,
    float amplitude,
    float lacunarity,
    float gain,
    float cellSize,
    float heightScale)
{
    if (!create(width, height, cellSize, heightScale))
        return false;
    if (octaves < 1)
        octaves = 1;
    if (frequency <= 0.0f)
        frequency = 1.0f;

    const float invW = 1.0f / static_cast<float>(width - 1);
    const float invH = 1.0f / static_cast<float>(height - 1);

    for (uint32_t z = 0; z < height; ++z)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            float freq = frequency;
            float amp  = amplitude;
            float sum  = 0.0f;
            for (int o = 0; o < octaves; ++o)
            {
                const float nx = static_cast<float>(x) * invW * freq;
                const float nz = static_cast<float>(z) * invH * freq;
                sum += (ValueNoise(nx, nz, seed + static_cast<uint32_t>(o) * 1013u) * 2.0f - 1.0f) * amp;
                freq *= lacunarity;
                amp *= gain;
            }
            m_samples[static_cast<size_t>(z) * width + x] = sum;
        }
    }
    return true;
}

bool HeightMap::addLayer(const HeightMap& other, float scale)
{
    if (!valid() || !other.valid())
        return false;
    if (other.m_width != m_width || other.m_height != m_height)
    {
        DE_LOG_ERROR("HeightMap::addLayer: size mismatch");
        return false;
    }
    const size_t count = m_samples.size();
    for (size_t i = 0; i < count; ++i)
        m_samples[i] += other.m_samples[i] * scale;
    markAccelDirty();
    return true;
}

void HeightMap::setOrigin(const Vector3f& origin)
{
    m_origin = origin;
    refreshCached();
}

void HeightMap::setCellSize(float cellSize)
{
    if (cellSize > 0.0f)
    {
        m_cellSize = cellSize;
        refreshCached();
    }
}

void HeightMap::setHeightScale(float scale)
{
    m_heightScale = scale;
    markAccelDirty();
}

void HeightMap::setHeight(int x, int z, float height)
{
    if (!valid())
        return;
    x = clampX(x);
    z = clampZ(z);
    m_samples[static_cast<size_t>(z) * m_width + static_cast<size_t>(x)] = height;
    markAccelDirty();
}

float HeightMap::height(int x, int z) const
{
    return sampleRaw(clampX(x), clampZ(z));
}

float HeightMap::sampleBilinear(float fx, float fz) const
{
    if (!valid())
        return 0.0f;

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

    const float h00 = sampleRaw(x0, z0);
    const float h10 = sampleRaw(x1, z0);
    const float h01 = sampleRaw(x0, z1);
    const float h11 = sampleRaw(x1, z1);
    return Lerp(Lerp(h00, h10, tx), Lerp(h01, h11, tx), tz);
}

void HeightMap::worldToSample(float wx, float wz, float& outFx, float& outFz) const
{
    outFx = (wx - m_origin.x) * m_invCellSize;
    outFz = (wz - m_origin.z) * m_invCellSize;
}

bool HeightMap::containsXZ(float worldX, float worldZ) const
{
    if (!valid())
        return false;
    return worldX >= m_origin.x && worldX <= m_worldMaxX
        && worldZ >= m_origin.z && worldZ <= m_worldMaxZ;
}

float HeightMap::heightAtWorld(float worldX, float worldZ) const
{
    if (!valid())
        return m_origin.y;
    const float fx = (worldX - m_origin.x) * m_invCellSize;
    const float fz = (worldZ - m_origin.z) * m_invCellSize;
    return worldY(sampleBilinear(fx, fz));
}

bool HeightMap::tryHeightAtWorld(float worldX, float worldZ, float& outY) const
{
    if (!containsXZ(worldX, worldZ))
        return false;
    outY = heightAtWorld(worldX, worldZ);
    return true;
}

Vector3f HeightMap::normalAtWorld(float worldX, float worldZ) const
{
    if (!valid())
        return Vector3f(0.0f, 1.0f, 0.0f);

    const float step = m_cellSize;
    const float hL = heightAtWorld(worldX - step, worldZ);
    const float hR = heightAtWorld(worldX + step, worldZ);
    const float hD = heightAtWorld(worldX, worldZ - step);
    const float hU = heightAtWorld(worldX, worldZ + step);

    // LH, Y-up: tangent X = (2*cell, dY, 0), tangent Z = (0, dY, 2*cell)
    Vector3f n(hL - hR, 2.0f * step, hD - hU);
    n.Normalize();
    return n;
}

Vector3f HeightMap::positionAtSample(int x, int z) const
{
    x = clampX(x);
    z = clampZ(z);
    return Vector3f(worldX(x), worldY(sampleRaw(x, z)), worldZ(z));
}

Aabb3f HeightMap::bounds() const
{
    if (!valid())
        return Aabb3f::Empty();
    ensureAccel();
    return m_cachedBounds;
}

int HeightMap::clampX(int x) const
{
    if (x < 0)
        return 0;
    const int maxX = static_cast<int>(m_width) - 1;
    return x > maxX ? maxX : x;
}

int HeightMap::clampZ(int z) const
{
    if (z < 0)
        return 0;
    const int maxZ = static_cast<int>(m_height) - 1;
    return z > maxZ ? maxZ : z;
}

float HeightMap::sampleRaw(int x, int z) const
{
    if (!valid())
        return 0.0f;
    return m_samples[static_cast<size_t>(z) * m_width + static_cast<size_t>(x)];
}

void HeightMap::refreshCached()
{
    m_invCellSize = (m_cellSize > 0.0f) ? (1.0f / m_cellSize) : 0.0f;
    if (valid())
    {
        m_worldMaxX = m_origin.x + static_cast<float>(m_width - 1) * m_cellSize;
        m_worldMaxZ = m_origin.z + static_cast<float>(m_height - 1) * m_cellSize;
    }
    else
    {
        m_worldMaxX = m_origin.x;
        m_worldMaxZ = m_origin.z;
    }
    markAccelDirty();
}

void HeightMap::ensureAccel() const
{
    if (!m_accelDirty)
        return;
    buildAccel();
}

void HeightMap::buildAccel() const
{
    m_pyramid.clear();
    m_cachedBounds = Aabb3f::Empty();
    if (!valid())
    {
        m_accelDirty = false;
        return;
    }

    const int cellsX = static_cast<int>(m_width) - 1;
    const int cellsZ = static_cast<int>(m_height) - 1;

    PyramidLevel fine;
    fine.w = cellsX;
    fine.h = cellsZ;
    fine.nodes.resize(static_cast<size_t>(cellsX) * cellsZ);

    float globalMin = Infinity;
    float globalMax = -Infinity;
    for (int z = 0; z < cellsZ; ++z)
    {
        for (int x = 0; x < cellsX; ++x)
        {
            const float y00 = worldY(sampleRaw(x, z));
            const float y10 = worldY(sampleRaw(x + 1, z));
            const float y01 = worldY(sampleRaw(x, z + 1));
            const float y11 = worldY(sampleRaw(x + 1, z + 1));
            MinMaxY mm;
            mm.minY = Min(Min(y00, y10), Min(y01, y11));
            mm.maxY = Max(Max(y00, y10), Max(y01, y11));
            fine.nodes[static_cast<size_t>(z) * cellsX + x] = mm;
            if (mm.minY < globalMin)
                globalMin = mm.minY;
            if (mm.maxY > globalMax)
                globalMax = mm.maxY;
        }
    }
    m_pyramid.push_back(std::move(fine));

    while (m_pyramid.back().w > 1 || m_pyramid.back().h > 1)
    {
        const PyramidLevel& prev = m_pyramid.back();
        PyramidLevel next;
        next.w = (prev.w + 1) / 2;
        next.h = (prev.h + 1) / 2;
        next.nodes.resize(static_cast<size_t>(next.w) * next.h);

        for (int z = 0; z < next.h; ++z)
        {
            for (int x = 0; x < next.w; ++x)
            {
                MinMaxY mm;
                mm.minY = Infinity;
                mm.maxY = -Infinity;
                for (int dz = 0; dz < 2; ++dz)
                {
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const int cx = x * 2 + dx;
                        const int cz = z * 2 + dz;
                        if (cx >= prev.w || cz >= prev.h)
                            continue;
                        const MinMaxY& c = prev.nodes[static_cast<size_t>(cz) * prev.w + cx];
                        if (c.minY < mm.minY)
                            mm.minY = c.minY;
                        if (c.maxY > mm.maxY)
                            mm.maxY = c.maxY;
                    }
                }
                next.nodes[static_cast<size_t>(z) * next.w + x] = mm;
            }
        }
        m_pyramid.push_back(std::move(next));
    }

    m_cachedBounds = Aabb3f(
        Vector3f(m_origin.x, globalMin, m_origin.z),
        Vector3f(m_worldMaxX, globalMax, m_worldMaxZ));
    m_accelDirty = false;
}

Aabb3f HeightMap::nodeBounds(int level, int nx, int nz) const
{
    const int cellsX = static_cast<int>(m_width) - 1;
    const int cellsZ = static_cast<int>(m_height) - 1;
    const int span   = 1 << level;
    const int x0     = nx * span;
    const int z0     = nz * span;
    int       x1     = x0 + span;
    int       z1     = z0 + span;
    if (x1 > cellsX)
        x1 = cellsX;
    if (z1 > cellsZ)
        z1 = cellsZ;

    const PyramidLevel& lvl = m_pyramid[static_cast<size_t>(level)];
    const MinMaxY&      mm  = lvl.nodes[static_cast<size_t>(nz) * lvl.w + nx];
    return Aabb3f(
        Vector3f(worldX(x0), mm.minY, worldZ(z0)),
        Vector3f(worldX(x1), mm.maxY, worldZ(z1)));
}

bool HeightMap::intersectCell(int cx, int cz, const Ray3f& ray, float tMin, float tMax, float& outT) const
{
    const float x0 = worldX(cx);
    const float z0 = worldZ(cz);
    const float inv = m_invCellSize;

    const float h00 = worldY(sampleRaw(cx, cz));
    const float h10 = worldY(sampleRaw(cx + 1, cz));
    const float h01 = worldY(sampleRaw(cx, cz + 1));
    const float h11 = worldY(sampleRaw(cx + 1, cz + 1));

    const float u0 = (ray.Origin.x - x0) * inv;
    const float v0 = (ray.Origin.z - z0) * inv;
    const float du = ray.Direction.x * inv;
    const float dv = ray.Direction.z * inv;

    const float e1 = h10 - h00;
    const float e2 = h01 - h00;
    const float e3 = h00 - h10 - h01 + h11;

    const float A = du * dv * e3;
    const float B = du * e1 + dv * e2 + (u0 * dv + v0 * du) * e3 - ray.Direction.y;
    const float C = h00 + u0 * e1 + v0 * e2 + u0 * v0 * e3 - ray.Origin.y;

    constexpr float kUvEps = 1.0e-4f;
    constexpr float kQuadEps = 1.0e-10f;

    float roots[2];
    int   nRoots = 0;

    if (fabsf(A) < kQuadEps)
    {
        if (fabsf(B) < kQuadEps)
        {
            if (fabsf(C) > 1.0e-4f)
                return false;
            // Ray lies in the (planar) patch. First XZ entry is the hit.
            const float u = u0 + tMin * du;
            const float v = v0 + tMin * dv;
            if (u >= -kUvEps && u <= 1.0f + kUvEps && v >= -kUvEps && v <= 1.0f + kUvEps)
            {
                outT = tMin;
                return tMin <= tMax;
            }
            return false;
        }
        roots[nRoots++] = -C / B;
    }
    else
    {
        const float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f)
            return false;
        const float s = sqrtf(disc);
        roots[nRoots++] = (-B - s) / (2.0f * A);
        roots[nRoots++] = (-B + s) / (2.0f * A);
    }

    bool  hit  = false;
    float best = tMax;
    for (int i = 0; i < nRoots; ++i)
    {
        const float t = roots[i];
        if (t < tMin || t > best)
            continue;
        const float u = u0 + t * du;
        const float v = v0 + t * dv;
        if (u < -kUvEps || u > 1.0f + kUvEps || v < -kUvEps || v > 1.0f + kUvEps)
            continue;
        best = t;
        hit  = true;
    }
    if (hit)
        outT = best;
    return hit;
}

bool HeightMap::intersectVertical(const Ray3f& ray, float maxDistance, Collision::RayHit3D& out) const
{
    if (!containsXZ(ray.Origin.x, ray.Origin.z))
        return false;
    if (fabsf(ray.Direction.y) < Epsilon)
        return false;

    const float hy = heightAtWorld(ray.Origin.x, ray.Origin.z);
    const float t  = (hy - ray.Origin.y) / ray.Direction.y;
    if (t < 0.0f || t > maxDistance)
        return false;

    out.hit    = true;
    out.t      = t;
    out.tExit  = t;
    out.point  = ray.PointAt(t);
    out.point.y = hy;
    out.normal = normalAtWorld(out.point.x, out.point.z);
    if (out.normal.Dot(ray.Direction) > 0.0f)
        out.normal = -out.normal;
    return true;
}

Collision::RayHit3D HeightMap::raycast(const Ray3f& ray, float maxDistance) const
{
    Collision::RayHit3D hit{};
    if (!valid() || maxDistance < 0.0f)
        return hit;

    const float dirLenSq = ray.Direction.MagnitudeSqrd();
    if (dirLenSq < Epsilon * Epsilon)
        return hit;

    ensureAccel();
    if (m_pyramid.empty() || !m_cachedBounds.IsValid())
        return hit;

    if (fabsf(ray.Direction.x) < Epsilon && fabsf(ray.Direction.z) < Epsilon)
    {
        intersectVertical(ray, maxDistance, hit);
        return hit;
    }

    float tBox0 = 0.0f;
    float tBox1 = 0.0f;
    if (!ray.IntersectAabb(m_cachedBounds, tBox0, tBox1))
        return hit;
    if (tBox0 > maxDistance || tBox1 < 0.0f)
        return hit;
    if (tBox0 < 0.0f)
        tBox0 = 0.0f;
    if (tBox1 > maxDistance)
        tBox1 = maxDistance;
    if (tBox0 > tBox1)
        return hit;

    struct NodeRef
    {
        int   level;
        int   x;
        int   z;
        float tEnter;
    };

    NodeRef stack[64];
    int     sp = 0;
    stack[sp++] = NodeRef{ static_cast<int>(m_pyramid.size()) - 1, 0, 0, tBox0 };

    float bestT = tBox1 + 1.0f;

    while (sp > 0)
    {
        const NodeRef node = stack[--sp];
        if (node.tEnter >= bestT)
            continue;

        const Aabb3f box = nodeBounds(node.level, node.x, node.z);
        float t0 = 0.0f;
        float t1 = 0.0f;
        if (!ray.IntersectAabb(box, t0, t1))
            continue;
        if (t0 < tBox0)
            t0 = tBox0;
        if (t1 > tBox1)
            t1 = tBox1;
        if (t0 > t1 || t0 >= bestT)
            continue;

        if (node.level == 0)
        {
            float tHit = 0.0f;
            if (intersectCell(node.x, node.z, ray, t0, Min(t1, bestT), tHit) && tHit < bestT)
                bestT = tHit;
            continue;
        }

        const int cl = node.level - 1;
        const int cw = m_pyramid[static_cast<size_t>(cl)].w;
        const int ch = m_pyramid[static_cast<size_t>(cl)].h;

        NodeRef kids[4];
        int     nk = 0;
        for (int dz = 0; dz < 2; ++dz)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                const int cx = node.x * 2 + dx;
                const int cz = node.z * 2 + dz;
                if (cx >= cw || cz >= ch)
                    continue;

                const Aabb3f cb = nodeBounds(cl, cx, cz);
                float ct0 = 0.0f;
                float ct1 = 0.0f;
                if (!ray.IntersectAabb(cb, ct0, ct1))
                    continue;
                if (ct0 < tBox0)
                    ct0 = tBox0;
                if (ct1 > tBox1)
                    ct1 = tBox1;
                if (ct0 > ct1 || ct0 >= bestT)
                    continue;
                kids[nk++] = NodeRef{ cl, cx, cz, ct0 };
            }
        }

        // Far to near so the nearest child is popped first.
        for (int i = 1; i < nk; ++i)
        {
            NodeRef key = kids[i];
            int     j   = i;
            while (j > 0 && kids[j - 1].tEnter < key.tEnter)
            {
                kids[j] = kids[j - 1];
                --j;
            }
            kids[j] = key;
        }
        for (int i = 0; i < nk && sp < 64; ++i)
            stack[sp++] = kids[i];
    }

    if (bestT > tBox1)
        return hit;

    hit.hit   = true;
    hit.t     = bestT;
    hit.tExit = bestT;
    hit.point = ray.PointAt(bestT);
    // Snap Y to the field so the hit sits on the same surface as heightAtWorld.
    hit.point.y = heightAtWorld(hit.point.x, hit.point.z);
    hit.normal  = normalAtWorld(hit.point.x, hit.point.z);
    if (hit.normal.Dot(ray.Direction) > 0.0f)
        hit.normal = -hit.normal;
    return hit;
}

} // namespace Terrain
} // namespace Dark
