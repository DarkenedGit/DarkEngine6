#include "Terrain/TerrainLod.h"
#include "Terrain/HeightMap.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark::Terrain
{

    using namespace Math;

    int maxLodForChunkCells(int chunkCells)
    {
        if (!isPowerOfTwo(chunkCells))
            return 0;
        int lod = 0;
        int cells = chunkCells;
        while (cells > 1)
        {
            cells >>= 1;
            ++lod;
        }
        return lod;
    }

    int lodFromDistance(float distance, const float* distances, int distanceCount, int maxLod)
    {
        if (maxLod < 0)
            maxLod = 0;
        if (!distances || distanceCount <= 0)
            return 0;

        int lod = 0;
        for (int i = 0; i < distanceCount && lod < maxLod; ++i)
        {
            if (distance >= distances[i])
                ++lod;
            else
                break;
        }
        return lod > maxLod ? maxLod : lod;
    }

    void restrictNeighborLods(int* lods, int chunksX, int chunksZ, int maxIterations)
    {
        if (!lods || chunksX <= 0 || chunksZ <= 0)
            return;
        if (maxIterations < 1)
            maxIterations = 1;

        const int count = chunksX * chunksZ;
        for (int iter = 0; iter < maxIterations; ++iter)
        {
            bool changed = false;
            for (int z = 0; z < chunksZ; ++z)
            {
                for (int x = 0; x < chunksX; ++x)
                {
                    const int i = z * chunksX + x;
                    int       lod = lods[i];
                    auto consider = [&](int nx, int nz)
                    {
                        if (nx < 0 || nz < 0 || nx >= chunksX || nz >= chunksZ)
                            return;
                        const int nLod = lods[nz * chunksX + nx];
                        if (lod > nLod + 1)
                        {
                            lod = nLod + 1;
                            changed = true;
                        }
                    };
                    consider(x, z - 1);
                    consider(x, z + 1);
                    consider(x - 1, z);
                    consider(x + 1, z);
                    lods[i] = lod;
                }
            }
            if (!changed)
                break;
            (void)count;
        }
    }

    EdgeMask neighborCoarserMask(const int* lods, int chunksX, int chunksZ, int cx, int cz)
    {
        EdgeMask mask;
        if (!lods || cx < 0 || cz < 0 || cx >= chunksX || cz >= chunksZ)
            return mask;

        const int lod = lods[cz * chunksX + cx];
        auto flag = [&](int nx, int nz, uint8_t bit)
        {
            if (nx < 0 || nz < 0 || nx >= chunksX || nz >= chunksZ)
                return;
            if (lods[nz * chunksX + nx] > lod)
                mask.bits = static_cast<uint8_t>(mask.bits | bit);
        };
        flag(cx, cz + 1, 1u); // north +Z
        flag(cx + 1, cz, 2u); // east  +X
        flag(cx, cz - 1, 4u); // south -Z
        flag(cx - 1, cz, 8u); // west  -X
        return mask;
    }

    bool isSkippedEdgeVertex(int x, int z, int cells, EdgeMask mask)
    {
        if (x == 0 && mask.west() && (z & 1) != 0)
            return true;
        if (x == cells && mask.east() && (z & 1) != 0)
            return true;
        if (z == 0 && mask.south() && (x & 1) != 0)
            return true;
        if (z == cells && mask.north() && (x & 1) != 0)
            return true;
        return false;
    }

    namespace
    {

    float WeldedHeight(const HeightMap& hm, int sampleX, int sampleZ, int originX, int originZ, int step, int chunkCells, EdgeMask edges)
    {
        const int localX = sampleX - originX;
        const int localZ = sampleZ - originZ;

        auto lerpH = [&](int ax, int az, int bx, int bz, int along, int aAlong, int span) -> float
        {
            const float t = (span > 0) ? (static_cast<float>(along - aAlong) / static_cast<float>(span)) : 0.0f;
            const float ha = hm.height(ax, az);
            const float hb = hm.height(bx, bz);
            return Lerp(ha, hb, t);
        };

        // Neighbor is restricted to +1 LOD, so the coarse step is 2 * this step.
        const int coarse = step * 2;

        if (localX == 0 && edges.west() && (localZ % coarse) != 0)
        {
            const int lower = (localZ / coarse) * coarse;
            return lerpH(originX, originZ + lower, originX, originZ + lower + coarse, localZ, lower, coarse);
        }
        if (localX == chunkCells && edges.east() && (localZ % coarse) != 0)
        {
            const int lower = (localZ / coarse) * coarse;
            return lerpH(originX + chunkCells, originZ + lower, originX + chunkCells, originZ + lower + coarse, localZ, lower, coarse);
        }
        if (localZ == 0 && edges.south() && (localX % coarse) != 0)
        {
            const int lower = (localX / coarse) * coarse;
            return lerpH(originX + lower, originZ, originX + lower + coarse, originZ, localX, lower, coarse);
        }
        if (localZ == chunkCells && edges.north() && (localX % coarse) != 0)
        {
            const int lower = (localX / coarse) * coarse;
            return lerpH(originX + lower, originZ + chunkCells, originX + lower + coarse, originZ + chunkCells, localX, lower, coarse);
        }

        return hm.height(sampleX, sampleZ);
    }

    void EmitTri(MeshData& mesh, int verts, int x0, int z0, int x1, int z1, int x2, int z2)
    {
        auto idx = [verts](int x, int z) -> uint32_t
        {
            return static_cast<uint32_t>(z * verts + x);
        };

        // D3D tests winding after the viewport (Y down). A +Y surface viewed from
        // above is front-facing under FrontCounterClockwise only if the math-space
        // winding is clockwise, i.e. geometric cross Y is negative.
        const int crossY = (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0);
        if (crossY <= 0)
        {
            mesh.indices.push_back(idx(x0, z0));
            mesh.indices.push_back(idx(x1, z1));
            mesh.indices.push_back(idx(x2, z2));
        }
        else
        {
            mesh.indices.push_back(idx(x0, z0));
            mesh.indices.push_back(idx(x2, z2));
            mesh.indices.push_back(idx(x1, z1));
        }
    }

    void EmitRegularQuad(MeshData& mesh, int verts, int x, int z)
    {
        // bl=(x,z), br=(x+1,z), tr=(x+1,z+1), tl=(x,z+1)
        // (bl, br, tr) / (bl, tr, tl) is CW in Y-up XZ, CCW in D3D Y-down screen space.
        auto idx = [verts](int vx, int vz) -> uint32_t
        {
            return static_cast<uint32_t>(vz * verts + vx);
        };
        const uint32_t bl = idx(x, z);
        const uint32_t br = idx(x + 1, z);
        const uint32_t tr = idx(x + 1, z + 1);
        const uint32_t tl = idx(x, z + 1);
        mesh.indices.push_back(bl);
        mesh.indices.push_back(br);
        mesh.indices.push_back(tr);
        mesh.indices.push_back(bl);
        mesh.indices.push_back(tr);
        mesh.indices.push_back(tl);
    }

    void BuildIndices(MeshData& mesh, int cells, EdgeMask edges)
    {
        const int verts = cells + 1;

        if (cells == 1)
        {
            EmitRegularQuad(mesh, verts, 0, 0);
            return;
        }

        // 2x2 cells: fan from the center so any mix of coarse edges stays a disk.
        if (cells == 2)
        {
            const int ring[8][2] = {
                { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 },
                { 2, 2 }, { 1, 2 }, { 0, 2 }, { 0, 1 }
            };
            int active[8][2]{};
            int n = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (!isSkippedEdgeVertex(ring[i][0], ring[i][1], cells, edges))
                {
                    active[n][0] = ring[i][0];
                    active[n][1] = ring[i][1];
                    ++n;
                }
            }
            for (int i = 0; i < n; ++i)
            {
                const int j = (i + 1) % n;
                EmitTri(mesh, verts, 1, 1, active[i][0], active[i][1], active[j][0], active[j][1]);
            }
            return;
        }

        const int x0 = edges.west() ? 1 : 0;
        const int x1 = edges.east() ? (cells - 1) : cells;
        const int z0 = edges.south() ? 1 : 0;
        const int z1 = edges.north() ? (cells - 1) : cells;
        for (int z = z0; z < z1; ++z)
        {
            for (int x = x0; x < x1; ++x)
                EmitRegularQuad(mesh, verts, x, z);
        }

        auto westPent = [&](int z)
        {
            EmitTri(mesh, verts, 0, z, 0, z + 2, 1, z + 2);
            EmitTri(mesh, verts, 0, z, 1, z + 2, 1, z + 1);
            EmitTri(mesh, verts, 0, z, 1, z + 1, 1, z);
        };
        auto eastPent = [&](int z)
        {
            EmitTri(mesh, verts, cells, z, cells - 1, z, cells - 1, z + 1);
            EmitTri(mesh, verts, cells, z, cells - 1, z + 1, cells - 1, z + 2);
            EmitTri(mesh, verts, cells, z, cells - 1, z + 2, cells, z + 2);
        };
        auto southPent = [&](int x)
        {
            EmitTri(mesh, verts, x, 0, x + 2, 0, x + 2, 1);
            EmitTri(mesh, verts, x, 0, x + 2, 1, x + 1, 1);
            EmitTri(mesh, verts, x, 0, x + 1, 1, x, 1);
        };
        auto northPent = [&](int x)
        {
            EmitTri(mesh, verts, x, cells, x, cells - 1, x + 1, cells - 1);
            EmitTri(mesh, verts, x, cells, x + 1, cells - 1, x + 2, cells - 1);
            EmitTri(mesh, verts, x, cells, x + 2, cells - 1, x + 2, cells);
        };

        auto swHex = [&]()
        {
            EmitTri(mesh, verts, 0, 0, 0, 2, 1, 2);
            EmitTri(mesh, verts, 0, 0, 1, 2, 1, 1);
            EmitTri(mesh, verts, 0, 0, 1, 1, 2, 1);
            EmitTri(mesh, verts, 0, 0, 2, 1, 2, 0);
        };
        auto seHex = [&]()
        {
            EmitTri(mesh, verts, cells, 0, cells - 2, 0, cells - 2, 1);
            EmitTri(mesh, verts, cells, 0, cells - 2, 1, cells - 1, 1);
            EmitTri(mesh, verts, cells, 0, cells - 1, 1, cells - 1, 2);
            EmitTri(mesh, verts, cells, 0, cells - 1, 2, cells, 2);
        };
        auto nwHex = [&]()
        {
            EmitTri(mesh, verts, 0, cells, 2, cells, 2, cells - 1);
            EmitTri(mesh, verts, 0, cells, 2, cells - 1, 1, cells - 1);
            EmitTri(mesh, verts, 0, cells, 1, cells - 1, 1, cells - 2);
            EmitTri(mesh, verts, 0, cells, 1, cells - 2, 0, cells - 2);
        };
        auto neHex = [&]()
        {
            EmitTri(mesh, verts, cells, cells, cells, cells - 2, cells - 1, cells - 2);
            EmitTri(mesh, verts, cells, cells, cells - 1, cells - 2, cells - 1, cells - 1);
            EmitTri(mesh, verts, cells, cells, cells - 1, cells - 1, cells - 2, cells - 1);
            EmitTri(mesh, verts, cells, cells, cells - 2, cells - 1, cells - 2, cells);
        };

        if (edges.west())
        {
            for (int z = 0; z < cells; z += 2)
            {
                if (z == 0 && edges.south())
                    swHex();
                else if (z == cells - 2 && edges.north())
                    nwHex();
                else
                    westPent(z);
            }
        }
        if (edges.east())
        {
            for (int z = 0; z < cells; z += 2)
            {
                if (z == 0 && edges.south())
                    seHex();
                else if (z == cells - 2 && edges.north())
                    neHex();
                else
                    eastPent(z);
            }
        }
        if (edges.south())
        {
            for (int x = 0; x < cells; x += 2)
            {
                if (x == 0 && edges.west())
                    continue;
                if (x == cells - 2 && edges.east())
                    continue;
                southPent(x);
            }
        }
        if (edges.north())
        {
            for (int x = 0; x < cells; x += 2)
            {
                if (x == 0 && edges.west())
                    continue;
                if (x == cells - 2 && edges.east())
                    continue;
                northPent(x);
            }
        }
    }

    } // namespace

    bool buildPatchMesh(const PatchBuildDesc& desc, MeshData& out)
    {
        out = MeshData{};

        if (!desc.heightMap || !desc.heightMap->valid())
        {
            DE_LOG_ERROR("buildPatchMesh: invalid height map");
            return false;
        }
        if (!isPowerOfTwo(desc.chunkCells))
        {
            DE_LOG_ERROR("buildPatchMesh: chunkCells must be a power of two");
            return false;
        }

        const int maxLod = maxLodForChunkCells(desc.chunkCells);
        const int lod    = Clamp(desc.lod, 0, maxLod);
        const int step   = lodStep(lod);
        const int cells  = desc.chunkCells / step;
        const int verts  = cells + 1;

        const HeightMap& hm = *desc.heightMap;
        const float invU    = (hm.width() > 1) ? (1.0f / static_cast<float>(hm.width() - 1)) : 1.0f;
        const float invV    = (hm.height() > 1) ? (1.0f / static_cast<float>(hm.height() - 1)) : 1.0f;

        out.positions.reserve(static_cast<size_t>(verts) * verts);
        out.normals.reserve(static_cast<size_t>(verts) * verts);
        out.uvs.reserve(static_cast<size_t>(verts) * verts);

        for (int z = 0; z < verts; ++z)
        {
            for (int x = 0; x < verts; ++x)
            {
                const int sx = desc.originSampleX + x * step;
                const int sz = desc.originSampleZ + z * step;
                const float rawH = WeldedHeight(hm, sx, sz, desc.originSampleX, desc.originSampleZ, step, desc.chunkCells, desc.edges);
                const float wx = hm.worldX(sx);
                const float wz = hm.worldZ(sz);
                const float wy = hm.worldY(rawH);

                out.positions.push_back(Vector3f(wx, wy, wz));
                out.normals.push_back(hm.normalAtWorld(wx, wz));
                out.uvs.push_back(Vector2f(static_cast<float>(sx) * invU, static_cast<float>(sz) * invV));
            }
        }

        BuildIndices(out, cells, desc.edges);
        return !out.indices.empty();
    }

    bool buildGridIndices(int cells, EdgeMask edges, MeshData& out)
    {
        if (cells < 1)
            return false;
        BuildIndices(out, cells, edges);
        return !out.indices.empty();
    }

} // namespace Dark::Terrain
