#include "AI/Walkability.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <queue>

namespace Dark::AI
{
    constexpr float kDegToRad = 3.14159265f / 180.0f;

    bool aabbOverlapsCellXZ(const Math::Aabb3f& box, float x0, float z0, float x1, float z1)
    {
        return box.Max.x >= x0 && box.Min.x <= x1 && box.Max.z >= z0 && box.Min.z <= z1;
    }

    bool Walkability::bake(const WalkabilityDesc& desc)
    {
        m_walk.clear();
        m_island.clear();
        m_cellsX    = 0;
        m_cellsZ    = 0;
        m_heightMap = nullptr;

        if (!desc.heightMap || !desc.heightMap->valid())
        {
            DE_LOG_ERROR(LogCategory::AI, "Walkability::bake: height map is invalid");
            return false;
        }
        if (desc.heightMap->width() < 2 || desc.heightMap->height() < 2)
        {
            DE_LOG_ERROR(LogCategory::AI, "Walkability::bake: height map too small");
            return false;
        }

        m_desc      = desc;
        m_heightMap = desc.heightMap;
        m_cellSize  = m_heightMap->cellSize();
        m_cellsX    = static_cast<int>(m_heightMap->width()) - 1;
        m_cellsZ    = static_cast<int>(m_heightMap->height()) - 1;
        m_maxClimb  = std::tan(desc.maxSlopeDeg * kDegToRad) * m_cellSize;
        m_minNy     = std::cos(desc.maxSlopeDeg * kDegToRad);

        const int n = m_cellsX * m_cellsZ;
        m_walk.resize(static_cast<size_t>(n), 0);

        for (int cz = 0; cz < m_cellsZ; ++cz)
        {
            for (int cx = 0; cx < m_cellsX; ++cx)
            {
                const bool ok = !cellWet(cx, cz) && !cellSteep(cx, cz) && !cellHitsCube(cx, cz);
                m_walk[static_cast<size_t>(index(cx, cz))] = ok ? 1u : 0u;
            }
        }

        floodIslands();
        DE_LOG_INFO(LogCategory::AI, "Walkability: {}x{} cells, climb {:.2f}", m_cellsX, m_cellsZ, m_maxClimb);
        return true;
    }

    bool Walkability::worldToCell(float worldX, float worldZ, int& cx, int& cz) const
    {
        if (!valid() || !m_heightMap->containsXZ(worldX, worldZ))
            return false;
        float fx = 0.0f;
        float fz = 0.0f;
        m_heightMap->worldToSample(worldX, worldZ, fx, fz);
        cx = static_cast<int>(std::floor(fx));
        cz = static_cast<int>(std::floor(fz));
        if (cx < 0 || cz < 0 || cx >= m_cellsX || cz >= m_cellsZ)
            return false;
        return true;
    }

    float Walkability::cellCenterX(int cx) const
    {
        return m_heightMap->worldX(cx) + 0.5f * m_cellSize;
    }

    float Walkability::cellCenterZ(int cz) const
    {
        return m_heightMap->worldZ(cz) + 0.5f * m_cellSize;
    }

    bool Walkability::inBoundsCell(int cx, int cz) const
    {
        return valid() && cx >= 0 && cz >= 0 && cx < m_cellsX && cz < m_cellsZ;
    }

    bool Walkability::walkable(int cx, int cz) const
    {
        if (!inBoundsCell(cx, cz))
            return false;
        return m_walk[static_cast<size_t>(index(cx, cz))] != 0;
    }

    bool Walkability::walkableWorld(float worldX, float worldZ) const
    {
        int cx = 0;
        int cz = 0;
        if (!worldToCell(worldX, worldZ, cx, cz))
            return false;
        return walkable(cx, cz);
    }

    bool Walkability::destWet(float worldX, float worldZ) const
    {
        int cx = 0;
        int cz = 0;
        if (!worldToCell(worldX, worldZ, cx, cz))
            return true;
        return cellWet(cx, cz);
    }

    int Walkability::island(int cx, int cz) const
    {
        if (!inBoundsCell(cx, cz) || m_island.empty())
            return 0;
        return m_island[static_cast<size_t>(index(cx, cz))];
    }

    int Walkability::islandWorld(float worldX, float worldZ) const
    {
        int cx = 0;
        int cz = 0;
        if (!worldToCell(worldX, worldZ, cx, cz))
            return 0;
        return island(cx, cz);
    }

    bool Walkability::edgeOk(int x0, int z0, int x1, int z1) const
    {
        if (!walkable(x0, z0) || !walkable(x1, z1))
            return false;
        const int dx = x1 - x0;
        const int dz = z1 - z0;
        if (std::abs(dx) > 1 || std::abs(dz) > 1)
            return false;
        if (dx != 0 && dz != 0)
        {
            if (!walkable(x0 + dx, z0) || !walkable(x0, z0 + dz))
                return false;
        }
        const float y0 = m_heightMap->heightAtWorld(cellCenterX(x0), cellCenterZ(z0));
        const float y1 = m_heightMap->heightAtWorld(cellCenterX(x1), cellCenterZ(z1));
        return std::fabs(y1 - y0) <= m_maxClimb;
    }

    bool Walkability::lineOfSight(int x0, int z0, int x1, int z1) const
    {
        if (!walkable(x0, z0) || !walkable(x1, z1))
            return false;
        int x = x0;
        int z = z0;
        const int dx = std::abs(x1 - x0);
        const int dz = std::abs(z1 - z0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sz = z0 < z1 ? 1 : -1;
        int err = dx - dz;
        while (x != x1 || z != z1)
        {
            const int e2 = err * 2;
            int nx = x;
            int nz = z;
            if (e2 > -dz)
            {
                err -= dz;
                nx += sx;
            }
            if (e2 < dx)
            {
                err += dx;
                nz += sz;
            }
            if (!edgeOk(x, z, nx, nz))
                return false;
            x = nx;
            z = nz;
        }
        return true;
    }

    bool Walkability::cellWet(int cx, int cz) const
    {
        const Terrain::HeightMap& hm = *m_heightMap;
        const float wl = m_desc.waterLevel;
        const float h00 = hm.heightAtWorld(hm.worldX(cx), hm.worldZ(cz));
        const float h10 = hm.heightAtWorld(hm.worldX(cx + 1), hm.worldZ(cz));
        const float h01 = hm.heightAtWorld(hm.worldX(cx), hm.worldZ(cz + 1));
        const float h11 = hm.heightAtWorld(hm.worldX(cx + 1), hm.worldZ(cz + 1));
        return h00 < wl || h10 < wl || h01 < wl || h11 < wl;
    }

    bool Walkability::cellSteep(int cx, int cz) const
    {
        const Terrain::HeightMap& hm = *m_heightMap;
        const float h00 = hm.heightAtWorld(hm.worldX(cx), hm.worldZ(cz));
        const float h10 = hm.heightAtWorld(hm.worldX(cx + 1), hm.worldZ(cz));
        const float h01 = hm.heightAtWorld(hm.worldX(cx), hm.worldZ(cz + 1));
        const float h11 = hm.heightAtWorld(hm.worldX(cx + 1), hm.worldZ(cz + 1));
        Math::Vector3f n(h00 + h01 - h10 - h11, 2.0f * m_cellSize, h00 + h10 - h01 - h11);
        n.Normalize();
        return n.y < m_minNy;
    }

    bool Walkability::cellHitsCube(int cx, int cz) const
    {
        if (!m_desc.cubes || m_desc.cubeCount <= 0)
            return false;
        const float x0 = m_heightMap->worldX(cx);
        const float z0 = m_heightMap->worldZ(cz);
        const float x1 = x0 + m_cellSize;
        const float z1 = z0 + m_cellSize;
        for (int i = 0; i < m_desc.cubeCount; ++i)
        {
            Math::Aabb3f box = m_desc.cubes[i];
            box.Expand(m_desc.agentRadius);
            if (aabbOverlapsCellXZ(box, x0, z0, x1, z1))
                return true;
        }
        return false;
    }

    void Walkability::floodIslands()
    {
        const int n = m_cellsX * m_cellsZ;
        m_island.assign(static_cast<size_t>(n), 0);
        int nextId = 1;
        const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        const int kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

        for (int cz = 0; cz < m_cellsZ; ++cz)
        {
            for (int cx = 0; cx < m_cellsX; ++cx)
            {
                const int i = index(cx, cz);
                if (!walkable(cx, cz) || m_island[static_cast<size_t>(i)] != 0)
                    continue;
                std::queue<int> q;
                m_island[static_cast<size_t>(i)] = nextId;
                q.push(i);
                while (!q.empty())
                {
                    const int cur = q.front();
                    q.pop();
                    const int x = cur % m_cellsX;
                    const int z = cur / m_cellsX;
                    for (int d = 0; d < 8; ++d)
                    {
                        const int nx = x + kDx[d];
                        const int nz = z + kDz[d];
                        if (!edgeOk(x, z, nx, nz))
                            continue;
                        const int ni = index(nx, nz);
                        if (m_island[static_cast<size_t>(ni)] != 0)
                            continue;
                        m_island[static_cast<size_t>(ni)] = nextId;
                        q.push(ni);
                    }
                }
                ++nextId;
            }
        }
    }
} // namespace Dark::AI
