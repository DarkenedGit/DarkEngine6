#include "AI/Pathfinder.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Dark::AI
{
namespace
{
    constexpr float kStraight = 1.0f;
    constexpr float kDiag     = 1.41421356f;
    constexpr float kAgentMul = 12.0f;

    struct OpenItem
    {
        float f = 0.0f;
        float g = 0.0f;
        int   i = 0;
        bool operator<(const OpenItem& o) const
        {
            if (f != o.f)
                return f > o.f;
            return g < o.g;
        }
    };
} // namespace

bool Pathfinder::bind(const Walkability* walk)
{
    m_walk = walk;
    if (!m_walk || !m_walk->valid())
    {
        DE_LOG_ERROR(LogCategory::AI, "Pathfinder::bind: walkability is invalid");
        m_walk = nullptr;
        return false;
    }
    return true;
}

bool Pathfinder::find(const PathRequest& req, PathResult& out) const
{
    out.points.clear();
    if (!m_walk || !m_walk->valid())
        return false;

    int sx = 0;
    int sz = 0;
    if (!m_walk->worldToCell(req.startX, req.startZ, sx, sz) || !m_walk->walkable(sx, sz))
        return false;

    if (!m_walk->heightMap()->containsXZ(req.destX, req.destZ))
        return false;
    if (m_walk->destWet(req.destX, req.destZ) && !destInCube(req.destX, req.destZ))
        return false;

    int dx = 0;
    int dz = 0;
    if (!snapDest(req, sx, sz, dx, dz))
        return false;
    if (m_walk->island(sx, sz) != m_walk->island(dx, dz) || m_walk->island(sx, sz) == 0)
        return false;

    std::vector<int> cells;
    if (!search(sx, sz, dx, dz, req, cells))
        return false;
    stringPull(cells, out);
    return !out.points.empty();
}

bool Pathfinder::destInCube(float x, float z) const
{
    const WalkabilityDesc* d = nullptr;
    (void)d;
    // Cubes live on WalkabilityDesc; Pathfinder only sees baked cells.
    // Snap uses unwalkable cells around dest: if dest cell is unwalkable and dry, treat as cube/inflate.
    int cx = 0;
    int cz = 0;
    if (!m_walk->worldToCell(x, z, cx, cz))
        return false;
    return !m_walk->walkable(cx, cz) && !m_walk->destWet(x, z);
}

bool Pathfinder::snapDest(const PathRequest& req, int startCx, int startCz, int& dx, int& dz) const
{
    if (!m_walk->worldToCell(req.destX, req.destZ, dx, dz))
        return false;
    if (m_walk->walkable(dx, dz))
        return true;
    if (m_walk->destWet(req.destX, req.destZ))
        return false;

    const int startIsland = m_walk->island(startCx, startCz);
    int       bestCx      = -1;
    int       bestCz      = -1;
    float     bestD2      = 1.0e30f;
    const int cellsX      = m_walk->cellsX();
    const int cellsZ      = m_walk->cellsZ();
    for (int cz = 0; cz < cellsZ; ++cz)
    {
        for (int cx = 0; cx < cellsX; ++cx)
        {
            if (!m_walk->walkable(cx, cz) || m_walk->island(cx, cz) != startIsland)
                continue;
            const float wx = m_walk->cellCenterX(cx);
            const float wz = m_walk->cellCenterZ(cz);
            const float ddx = wx - req.destX;
            const float ddz = wz - req.destZ;
            const float d2  = ddx * ddx + ddz * ddz;
            if (d2 < bestD2)
            {
                bestD2 = d2;
                bestCx = cx;
                bestCz = cz;
            }
        }
    }
    if (bestCx < 0)
        return false;
    dx = bestCx;
    dz = bestCz;
    return true;
}

bool Pathfinder::blockedByAgent(int cx, int cz, const PathRequest& req, int startCx, int startCz) const
{
    if (cx == startCx && cz == startCz)
        return false;
    if (!req.others || req.otherCount <= 0)
        return false;
    const float wx = m_walk->cellCenterX(cx);
    const float wz = m_walk->cellCenterZ(cz);
    const float r  = 0.8f;
    for (int i = 0; i < req.otherCount; ++i)
    {
        const float dx = wx - req.others[i].x;
        const float dz = wz - req.others[i].z;
        const float rad = req.others[i].radius + r;
        if (dx * dx + dz * dz <= rad * rad)
            return true;
    }
    return false;
}

float Pathfinder::heuristic(int ax, int az, int bx, int bz) const
{
    const int dx = std::abs(ax - bx);
    const int dz = std::abs(az - bz);
    const int mn = dx < dz ? dx : dz;
    const int mx = dx > dz ? dx : dz;
    return static_cast<float>(mn) * kDiag + static_cast<float>(mx - mn) * kStraight;
}

float Pathfinder::stepCost(int x0, int z0, int x1, int z1, const PathRequest& req, int sx, int sz) const
{
    const int  adx  = std::abs(x1 - x0);
    const int  adz  = std::abs(z1 - z0);
    const float base = (adx != 0 && adz != 0) ? kDiag : kStraight;
    if (blockedByAgent(x1, z1, req, sx, sz))
        return base * kAgentMul;
    return base;
}

bool Pathfinder::search(int sx, int sz, int dx, int dz, const PathRequest& req, std::vector<int>& cells) const
{
    const int cellsX = m_walk->cellsX();
    const int cellsZ = m_walk->cellsZ();
    const int n      = cellsX * cellsZ;
    const int start  = sz * cellsX + sx;
    const int goal   = dz * cellsX + dx;

    std::vector<float> g(static_cast<size_t>(n), 1.0e30f);
    std::vector<int>   parent(static_cast<size_t>(n), -1);
    std::vector<uint8_t> closed(static_cast<size_t>(n), 0);
    std::priority_queue<OpenItem> open;

    g[static_cast<size_t>(start)] = 0.0f;
    open.push(OpenItem{ heuristic(sx, sz, dx, dz), 0.0f, start });

    const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    const int kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int       expanded = 0;
    const int cap      = req.expansionCap > 0 ? req.expansionCap : 4096;

    while (!open.empty())
    {
        const OpenItem cur = open.top();
        open.pop();
        if (closed[static_cast<size_t>(cur.i)])
            continue;
        closed[static_cast<size_t>(cur.i)] = 1;
        ++expanded;
        if (expanded > cap)
            return false;
        if (cur.i == goal)
            break;

        const int x = cur.i % cellsX;
        const int z = cur.i / cellsX;
        for (int d = 0; d < 8; ++d)
        {
            const int nx = x + kDx[d];
            const int nz = z + kDz[d];
            if (!m_walk->edgeOk(x, z, nx, nz))
                continue;
            if (blockedByAgent(nx, nz, req, sx, sz) && !(nx == dx && nz == dz))
            {
                // Occupied cells are high-cost, not walls, except we still skip
                // expanding through them when they are not the goal — fail-closed
                // if the only remaining path is through an agent.
                continue;
            }
            const int ni = nz * cellsX + nx;
            if (closed[static_cast<size_t>(ni)])
                continue;
            const float ng = g[static_cast<size_t>(cur.i)] + stepCost(x, z, nx, nz, req, sx, sz);
            if (ng >= g[static_cast<size_t>(ni)])
                continue;
            g[static_cast<size_t>(ni)]      = ng;
            parent[static_cast<size_t>(ni)] = cur.i;
            open.push(OpenItem{ ng + heuristic(nx, nz, dx, dz), ng, ni });
        }
    }

    if (parent[static_cast<size_t>(goal)] < 0 && start != goal)
        return false;

    cells.clear();
    int at = goal;
    while (at >= 0)
    {
        cells.push_back(at);
        if (at == start)
            break;
        at = parent[static_cast<size_t>(at)];
    }
    if (cells.empty() || cells.back() != start)
        return false;
    std::reverse(cells.begin(), cells.end());
    return true;
}

void Pathfinder::stringPull(const std::vector<int>& cells, PathResult& out) const
{
    out.points.clear();
    if (cells.empty())
        return;
    const int cellsX = m_walk->cellsX();
    auto cellXZ = [&](int i, int& x, int& z) {
        x = i % cellsX;
        z = i / cellsX;
    };

    std::vector<int> keep;
    keep.push_back(cells.front());
    size_t anchor = 0;
    for (size_t i = 1; i < cells.size(); ++i)
    {
        int ax, az, ix, iz;
        cellXZ(cells[anchor], ax, az);
        cellXZ(cells[i], ix, iz);
        if (!m_walk->lineOfSight(ax, az, ix, iz))
        {
            keep.push_back(cells[i - 1]);
            anchor = i - 1;
        }
    }
    keep.push_back(cells.back());

    out.points.reserve(keep.size());
    for (int i : keep)
    {
        const int cx = i % cellsX;
        const int cz = i / cellsX;
        const float x = m_walk->cellCenterX(cx);
        const float z = m_walk->cellCenterZ(cz);
        const float y = m_walk->heightMap()->heightAtWorld(x, z);
        out.points.push_back(Math::Vector3f{ x, y, z });
    }
}

} // namespace Dark::AI
