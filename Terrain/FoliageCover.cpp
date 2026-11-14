#include "Terrain/FoliageCover.h"

#include "Core/Log.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/TerrainGen.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <new>

namespace Dark::Terrain
{
    namespace
    {
        struct CoverHeader
        {
            uint32_t magic    = 0;
            uint32_t version  = 0;
            int32_t  tileX    = 0;
            int32_t  tileZ    = 0;
            uint32_t width    = 0;
            uint32_t height   = 0;
            float    cellSize = 0.0f;
            float    originX  = 0.0f;
            float    originZ  = 0.0f;
            uint32_t reserved = 0;
        };

        static_assert(sizeof(CoverHeader) == 40, "cover header");
        static_assert(offsetof(CoverHeader, cellSize) == 24, "cover cellSize");

        constexpr CoverSample kFull{ 255, 255 };

        uint8_t roundByte(double v)
        {
            if (v <= 0.0)
                return 0;
            if (v >= 255.0)
                return 255;
            return static_cast<uint8_t>(std::lround(v));
        }

        double lerp(double a, double b, double t)
        {
            return a + (b - a) * t;
        }

        bool squareHitsDisk(float x0, float z0, float x1, float z1, float x, float z, float radius)
        {
            const float qx = x < x0 ? x0 : (x > x1 ? x1 : x);
            const float qz = z < z0 ? z0 : (z > z1 ? z1 : z);
            const double dx = static_cast<double>(x) - static_cast<double>(qx);
            const double dz = static_cast<double>(z) - static_cast<double>(qz);
            const double r  = static_cast<double>(radius);
            return dx * dx + dz * dz <= r * r;
        }

        double distSqSegmentSegment(double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz)
        {
            const double ux = bx - ax;
            const double uz = bz - az;
            const double vx = dx - cx;
            const double vz = dz - cz;
            const double wx = ax - cx;
            const double wz = az - cz;
            const double a  = ux * ux + uz * uz;
            const double b  = ux * vx + uz * vz;
            const double c  = vx * vx + vz * vz;
            const double d  = ux * wx + uz * wz;
            const double e  = vx * wx + vz * wz;
            const double eps = 1.0e-12;
            double denom = a * c - b * b;
            double sN = 0.0;
            double sD = denom;
            double tN = 0.0;
            double tD = denom;
            if (denom < eps)
            {
                sN = 0.0;
                sD = 1.0;
                tN = e;
                tD = c;
            }
            else
            {
                sN = b * e - c * d;
                tN = a * e - b * d;
                if (sN < 0.0)
                {
                    sN = 0.0;
                    tN = e;
                    tD = c;
                }
                else if (sN > sD)
                {
                    sN = sD;
                    tN = e + b;
                    tD = c;
                }
            }
            if (tN < 0.0)
            {
                tN = 0.0;
                if (-d < 0.0)
                    sN = 0.0;
                else if (-d > a)
                    sN = sD;
                else
                {
                    sN = -d;
                    sD = a;
                }
            }
            else if (tN > tD)
            {
                tN = tD;
                if ((-d + b) < 0.0)
                    sN = 0.0;
                else if ((-d + b) > a)
                    sN = sD;
                else
                {
                    sN = -d + b;
                    sD = a;
                }
            }
            const double sc = (std::fabs(sN) < eps || std::fabs(sD) < eps) ? 0.0 : sN / sD;
            const double tc = (std::fabs(tN) < eps || std::fabs(tD) < eps) ? 0.0 : tN / tD;
            const double px = wx + ux * sc - vx * tc;
            const double pz = wz + uz * sc - vz * tc;
            return px * px + pz * pz;
        }

        bool pointInRect(double x, double z, double x0, double z0, double x1, double z1)
        {
            return x >= x0 && x <= x1 && z >= z0 && z <= z1;
        }

        double distSqSegmentRect(double ax, double az, double bx, double bz, double x0, double z0, double x1, double z1)
        {
            if (pointInRect(ax, az, x0, z0, x1, z1) || pointInRect(bx, bz, x0, z0, x1, z1))
                return 0.0;
            const double e0 = distSqSegmentSegment(ax, az, bx, bz, x0, z0, x1, z0);
            const double e1 = distSqSegmentSegment(ax, az, bx, bz, x1, z0, x1, z1);
            const double e2 = distSqSegmentSegment(ax, az, bx, bz, x1, z1, x0, z1);
            const double e3 = distSqSegmentSegment(ax, az, bx, bz, x0, z1, x0, z0);
            double best = e0;
            if (e1 < best)
                best = e1;
            if (e2 < best)
                best = e2;
            if (e3 < best)
                best = e3;
            return best;
        }

        double distPointSegment(double px, double pz, double ax, double az, double bx, double bz)
        {
            const double abx = bx - ax;
            const double abz = bz - az;
            const double apx = px - ax;
            const double apz = pz - az;
            const double ab2 = abx * abx + abz * abz;
            double t = 0.0;
            if (ab2 > 1.0e-12)
            {
                t = (apx * abx + apz * abz) / ab2;
                if (t < 0.0)
                    t = 0.0;
                if (t > 1.0)
                    t = 1.0;
            }
            const double dx = px - (ax + abx * t);
            const double dz = pz - (az + abz * t);
            return std::sqrt(dx * dx + dz * dz);
        }
    } // namespace

    FoliageCover::~FoliageCover()
    {
        reset();
    }

    FoliageCover::FoliageCover(FoliageCover&& other) noexcept
        : m_tiles(other.m_tiles)
        , m_tilesX(other.m_tilesX)
        , m_tilesZ(other.m_tilesZ)
        , m_cellsX(other.m_cellsX)
        , m_cellsZ(other.m_cellsZ)
        , m_extentX(other.m_extentX)
        , m_extentZ(other.m_extentZ)
        , m_originX(other.m_originX)
        , m_originZ(other.m_originZ)
        , m_valid(other.m_valid)
    {
        other.m_tiles   = nullptr;
        other.m_tilesX  = 0;
        other.m_tilesZ  = 0;
        other.m_cellsX  = 0;
        other.m_cellsZ  = 0;
        other.m_extentX = 0.0f;
        other.m_extentZ = 0.0f;
        other.m_originX = 0.0f;
        other.m_originZ = 0.0f;
        other.m_valid   = false;
    }

    FoliageCover& FoliageCover::operator=(FoliageCover&& other) noexcept
    {
        if (this == &other)
            return *this;
        reset();
        m_tiles   = other.m_tiles;
        m_tilesX  = other.m_tilesX;
        m_tilesZ  = other.m_tilesZ;
        m_cellsX  = other.m_cellsX;
        m_cellsZ  = other.m_cellsZ;
        m_extentX = other.m_extentX;
        m_extentZ = other.m_extentZ;
        m_originX = other.m_originX;
        m_originZ = other.m_originZ;
        m_valid   = other.m_valid;
        other.m_tiles   = nullptr;
        other.m_tilesX  = 0;
        other.m_tilesZ  = 0;
        other.m_cellsX  = 0;
        other.m_cellsZ  = 0;
        other.m_extentX = 0.0f;
        other.m_extentZ = 0.0f;
        other.m_originX = 0.0f;
        other.m_originZ = 0.0f;
        other.m_valid   = false;
        return *this;
    }

    bool FoliageCover::axisLayout(float extent, int& cells, int& tiles) const
    {
        if (!(extent > 0.0f))
            return false;
        const double n = std::ceil(static_cast<double>(extent) / static_cast<double>(kCoverCellSizeM));
        if (!(n >= 1.0) || n > static_cast<double>(INT32_MAX))
            return false;
        cells = static_cast<int>(n);
        tiles = (cells + kCoverTileCells - 1) / kCoverTileCells;
        return tiles >= 1;
    }

    int FoliageCover::tileCellsOnAxis(int tile, int cells) const
    {
        const int begin = tile * kCoverTileCells;
        int       end   = begin + kCoverTileCells;
        if (end > cells)
            end = cells;
        if (end < begin)
            return 0;
        return end - begin;
    }

    FoliageCover::Tile* FoliageCover::tileAt(int tx, int tz)
    {
        if (!m_tiles || tx < 0 || tz < 0 || tx >= m_tilesX || tz >= m_tilesZ)
            return nullptr;
        return &m_tiles[static_cast<size_t>(tz) * static_cast<size_t>(m_tilesX) + static_cast<size_t>(tx)];
    }

    const FoliageCover::Tile* FoliageCover::tileAt(int tx, int tz) const
    {
        if (!m_tiles || tx < 0 || tz < 0 || tx >= m_tilesX || tz >= m_tilesZ)
            return nullptr;
        return &m_tiles[static_cast<size_t>(tz) * static_cast<size_t>(m_tilesX) + static_cast<size_t>(tx)];
    }

    bool FoliageCover::allocateTile(int tx, int tz)
    {
        Tile* tile = tileAt(tx, tz);
        if (!tile || tile->cellsX <= 0 || tile->cellsZ <= 0)
            return false;
        if (tile->texels)
            return true;
        const size_t bytes = static_cast<size_t>(tile->cellsX) * static_cast<size_t>(tile->cellsZ) * 2u;
        uint8_t* texels = new (std::nothrow) uint8_t[bytes];
        if (!texels)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: allocation failed");
            return false;
        }
        std::memset(texels, 255, bytes);
        tile->texels = texels;
        return true;
    }

    void FoliageCover::clearTile(int tx, int tz)
    {
        Tile* tile = tileAt(tx, tz);
        if (!tile || !tile->texels)
            return;
        const size_t bytes = static_cast<size_t>(tile->cellsX) * static_cast<size_t>(tile->cellsZ) * 2u;
        std::memset(tile->texels, 255, bytes);
    }

    void FoliageCover::reset()
    {
        if (m_tiles)
        {
            const size_t n = static_cast<size_t>(m_tilesX) * static_cast<size_t>(m_tilesZ);
            for (size_t i = 0; i < n; ++i)
            {
                delete[] m_tiles[i].texels;
                m_tiles[i].texels = nullptr;
            }
            delete[] m_tiles;
        }
        m_tiles   = nullptr;
        m_tilesX  = 0;
        m_tilesZ  = 0;
        m_cellsX  = 0;
        m_cellsZ  = 0;
        m_extentX = 0.0f;
        m_extentZ = 0.0f;
        m_originX = 0.0f;
        m_originZ = 0.0f;
        m_valid   = false;
    }

    bool FoliageCover::createIndex(float worldExtentX, float worldExtentZ, float originX, float originZ)
    {
        reset();
        if (!std::isfinite(worldExtentX) || !std::isfinite(worldExtentZ) || !std::isfinite(originX) || !std::isfinite(originZ))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: extent rejected");
            return false;
        }
        int cellsX = 0;
        int cellsZ = 0;
        int tilesX = 0;
        int tilesZ = 0;
        if (!axisLayout(worldExtentX, cellsX, tilesX) || !axisLayout(worldExtentZ, cellsZ, tilesZ))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: extent rejected");
            return false;
        }
        if (static_cast<size_t>(tilesZ) > SIZE_MAX / static_cast<size_t>(tilesX))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: extent rejected");
            return false;
        }
        const size_t count = static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZ);
        Tile* tiles = new (std::nothrow) Tile[count];
        if (!tiles)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: allocation failed");
            return false;
        }
        m_tiles   = tiles;
        m_tilesX  = tilesX;
        m_tilesZ  = tilesZ;
        m_cellsX  = cellsX;
        m_cellsZ  = cellsZ;
        m_extentX = worldExtentX;
        m_extentZ = worldExtentZ;
        m_originX = originX;
        m_originZ = originZ;
        for (int tz = 0; tz < tilesZ; ++tz)
        {
            for (int tx = 0; tx < tilesX; ++tx)
            {
                Tile& tile  = m_tiles[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)];
                tile.texels = nullptr;
                tile.cellsX = tileCellsOnAxis(tx, cellsX);
                tile.cellsZ = tileCellsOnAxis(tz, cellsZ);
            }
        }
        m_valid = true;
        return true;
    }

    bool FoliageCover::create(float worldExtentX, float worldExtentZ, float originX, float originZ)
    {
        if (!createIndex(worldExtentX, worldExtentZ, originX, originZ))
            return false;
        const size_t n = static_cast<size_t>(m_tilesX) * static_cast<size_t>(m_tilesZ);
        for (size_t i = 0; i < n; ++i)
        {
            const int tx = static_cast<int>(i % static_cast<size_t>(m_tilesX));
            const int tz = static_cast<int>(i / static_cast<size_t>(m_tilesX));
            if (!allocateTile(tx, tz))
            {
                reset();
                return false;
            }
        }
        return true;
    }

    void FoliageCover::clear(uint8_t coverage, uint8_t heightScale)
    {
        if (!m_tiles)
            return;
        const size_t n = static_cast<size_t>(m_tilesX) * static_cast<size_t>(m_tilesZ);
        for (size_t i = 0; i < n; ++i)
        {
            Tile& tile = m_tiles[i];
            if (!tile.texels || tile.cellsX <= 0 || tile.cellsZ <= 0)
                continue;
            const size_t bytes = static_cast<size_t>(tile.cellsX) * static_cast<size_t>(tile.cellsZ) * 2u;
            if (coverage == heightScale)
                std::memset(tile.texels, coverage, bytes);
            else
            {
                for (size_t b = 0; b < bytes; b += 2)
                {
                    tile.texels[b]     = coverage;
                    tile.texels[b + 1] = heightScale;
                }
            }
        }
    }

    CoverSample FoliageCover::atCell(int cx, int cz) const
    {
        if (!m_valid || cx < 0 || cz < 0 || cx >= m_cellsX || cz >= m_cellsZ)
            return kFull;
        const int tx = cx / kCoverTileCells;
        const int tz = cz / kCoverTileCells;
        const Tile* tile = tileAt(tx, tz);
        if (!tile || !tile->texels)
            return kFull;
        const int localX = cx - tx * kCoverTileCells;
        const int localZ = cz - tz * kCoverTileCells;
        if (localX < 0 || localZ < 0 || localX >= tile->cellsX || localZ >= tile->cellsZ)
            return kFull;
        const uint8_t* p = tile->texels + (static_cast<size_t>(localZ) * static_cast<size_t>(tile->cellsX) + static_cast<size_t>(localX)) * 2u;
        return CoverSample{ p[0], p[1] };
    }

    CoverSample FoliageCover::sampleNearest(float worldX, float worldZ) const
    {
        if (!m_valid)
            return kFull;
        const double lx = static_cast<double>(worldX) - static_cast<double>(m_originX);
        const double lz = static_cast<double>(worldZ) - static_cast<double>(m_originZ);
        if (!(lx >= 0.0) || !(lz >= 0.0) || lx >= static_cast<double>(m_extentX) || lz >= static_cast<double>(m_extentZ))
            return kFull;
        int cx = static_cast<int>(std::floor(lx / static_cast<double>(kCoverCellSizeM)));
        int cz = static_cast<int>(std::floor(lz / static_cast<double>(kCoverCellSizeM)));
        if (cx >= m_cellsX)
            cx = m_cellsX - 1;
        if (cz >= m_cellsZ)
            cz = m_cellsZ - 1;
        return atCell(cx, cz);
    }

    CoverSample FoliageCover::sampleBilinear(float worldX, float worldZ) const
    {
        if (!m_valid)
            return kFull;
        const double lx = static_cast<double>(worldX) - static_cast<double>(m_originX);
        const double lz = static_cast<double>(worldZ) - static_cast<double>(m_originZ);
        if (!(lx >= 0.0) || !(lz >= 0.0) || lx >= static_cast<double>(m_extentX) || lz >= static_cast<double>(m_extentZ))
            return kFull;
        const double sx = lx / static_cast<double>(kCoverCellSizeM) - 0.5;
        const double sz = lz / static_cast<double>(kCoverCellSizeM) - 0.5;
        const int    x0 = static_cast<int>(std::floor(sx));
        const int    z0 = static_cast<int>(std::floor(sz));
        const double fx = sx - static_cast<double>(x0);
        const double fz = sz - static_cast<double>(z0);
        const CoverSample s00 = atCell(x0, z0);
        const CoverSample s10 = atCell(x0 + 1, z0);
        const CoverSample s01 = atCell(x0, z0 + 1);
        const CoverSample s11 = atCell(x0 + 1, z0 + 1);
        const double c = lerp(lerp(s00.coverage, s10.coverage, fx), lerp(s01.coverage, s11.coverage, fx), fz);
        const double h = lerp(lerp(s00.heightScale, s10.heightScale, fx), lerp(s01.heightScale, s11.heightScale, fx), fz);
        return CoverSample{ roundByte(c), roundByte(h) };
    }

    void FoliageCover::writeCell(int cx, int cz, uint8_t coverage, uint8_t heightScale, bool minBlend)
    {
        if (!m_valid || cx < 0 || cz < 0 || cx >= m_cellsX || cz >= m_cellsZ)
            return;
        const int tx = cx / kCoverTileCells;
        const int tz = cz / kCoverTileCells;
        Tile* tile = tileAt(tx, tz);
        if (!tile || !tile->texels)
            return;
        const int localX = cx - tx * kCoverTileCells;
        const int localZ = cz - tz * kCoverTileCells;
        if (localX < 0 || localZ < 0 || localX >= tile->cellsX || localZ >= tile->cellsZ)
            return;
        uint8_t* p = tile->texels + (static_cast<size_t>(localZ) * static_cast<size_t>(tile->cellsX) + static_cast<size_t>(localX)) * 2u;
        if (minBlend)
        {
            if (coverage < p[0])
                p[0] = coverage;
            if (heightScale < p[1])
                p[1] = heightScale;
        }
        else
        {
            p[0] = coverage;
            p[1] = heightScale;
        }
    }

    void FoliageCover::stampDisk(float x, float z, float radiusM, uint8_t coverage, uint8_t heightScale, bool minBlend)
    {
        if (!m_valid || !(radiusM >= 0.0f) || !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radiusM))
            return;
        const double lx = static_cast<double>(x) - static_cast<double>(m_originX);
        const double lz = static_cast<double>(z) - static_cast<double>(m_originZ);
        const double r  = static_cast<double>(radiusM);
        int c0x = static_cast<int>(std::floor((lx - r) / static_cast<double>(kCoverCellSizeM))) - 1;
        int c1x = static_cast<int>(std::floor((lx + r) / static_cast<double>(kCoverCellSizeM))) + 1;
        int c0z = static_cast<int>(std::floor((lz - r) / static_cast<double>(kCoverCellSizeM))) - 1;
        int c1z = static_cast<int>(std::floor((lz + r) / static_cast<double>(kCoverCellSizeM))) + 1;
        if (c0x < 0)
            c0x = 0;
        if (c0z < 0)
            c0z = 0;
        if (c1x >= m_cellsX)
            c1x = m_cellsX - 1;
        if (c1z >= m_cellsZ)
            c1z = m_cellsZ - 1;
        for (int cz = c0z; cz <= c1z; ++cz)
        {
            const float z0 = m_originZ + static_cast<float>(cz) * kCoverCellSizeM;
            const float z1 = z0 + kCoverCellSizeM;
            for (int cx = c0x; cx <= c1x; ++cx)
            {
                const float x0 = m_originX + static_cast<float>(cx) * kCoverCellSizeM;
                const float x1 = x0 + kCoverCellSizeM;
                if (!squareHitsDisk(x0, z0, x1, z1, x, z, radiusM))
                    continue;
                writeCell(cx, cz, coverage, heightScale, minBlend);
            }
        }
    }

    void FoliageCover::stampSoftAnnulus(float x, float z, float innerM, float outerM, uint8_t innerCoverage, uint8_t outerCoverage, uint8_t innerHeight, uint8_t outerHeight, bool minBlend)
    {
        if (!m_valid || !std::isfinite(x) || !std::isfinite(z))
            return;
        if (!(innerM >= 0.0f))
            innerM = 0.0f;
        if (!(outerM > innerM))
        {
            stampDisk(x, z, innerM, innerCoverage, innerHeight, minBlend);
            return;
        }

        const double lx = static_cast<double>(x) - static_cast<double>(m_originX);
        const double lz = static_cast<double>(z) - static_cast<double>(m_originZ);
        const double r  = static_cast<double>(outerM);
        int c0x = static_cast<int>(std::floor((lx - r) / static_cast<double>(kCoverCellSizeM))) - 1;
        int c1x = static_cast<int>(std::floor((lx + r) / static_cast<double>(kCoverCellSizeM))) + 1;
        int c0z = static_cast<int>(std::floor((lz - r) / static_cast<double>(kCoverCellSizeM))) - 1;
        int c1z = static_cast<int>(std::floor((lz + r) / static_cast<double>(kCoverCellSizeM))) + 1;
        if (c0x < 0)
            c0x = 0;
        if (c0z < 0)
            c0z = 0;
        if (c1x >= m_cellsX)
            c1x = m_cellsX - 1;
        if (c1z >= m_cellsZ)
            c1z = m_cellsZ - 1;

        const double span = static_cast<double>(outerM) - static_cast<double>(innerM);
        for (int cz = c0z; cz <= c1z; ++cz)
        {
            const float z0 = m_originZ + static_cast<float>(cz) * kCoverCellSizeM;
            const float z1 = z0 + kCoverCellSizeM;
            for (int cx = c0x; cx <= c1x; ++cx)
            {
                const float x0 = m_originX + static_cast<float>(cx) * kCoverCellSizeM;
                const float x1 = x0 + kCoverCellSizeM;
                if (squareHitsDisk(x0, z0, x1, z1, x, z, innerM))
                {
                    writeCell(cx, cz, innerCoverage, innerHeight, minBlend);
                    continue;
                }
                if (!squareHitsDisk(x0, z0, x1, z1, x, z, outerM))
                    continue;
                const double tcx  = static_cast<double>(m_originX) + (static_cast<double>(cx) + 0.5) * static_cast<double>(kCoverCellSizeM);
                const double tcz  = static_cast<double>(m_originZ) + (static_cast<double>(cz) + 0.5) * static_cast<double>(kCoverCellSizeM);
                const double dx   = tcx - static_cast<double>(x);
                const double dz   = tcz - static_cast<double>(z);
                const double dist = std::sqrt(dx * dx + dz * dz);
                double t = (dist - static_cast<double>(innerM)) / span;
                if (t < 0.0)
                    t = 0.0;
                if (t > 1.0)
                    t = 1.0;
                const uint8_t coverage = roundByte(lerp(innerCoverage, outerCoverage, t));
                const uint8_t height   = roundByte(lerp(innerHeight, outerHeight, t));
                writeCell(cx, cz, coverage, height, minBlend);
            }
        }
    }

    void FoliageCover::stampTrail(const std::vector<Math::Vector2f>& samples, float coreRadiusM, float shoulderM, float edgeNoiseM, uint32_t edgeSeed, bool minBlend)
    {
        if (!m_valid || samples.size() < 2)
            return;
        if (!std::isfinite(coreRadiusM) || !std::isfinite(shoulderM) || !std::isfinite(edgeNoiseM))
            return;
        if (!(coreRadiusM >= 0.0f))
            return;
        if (!(shoulderM >= 0.0f))
            shoulderM = 0.0f;
        if (!(edgeNoiseM >= 0.0f))
            edgeNoiseM = 0.0f;

        constexpr uint8_t kShoulderHeightNear = 64;
        constexpr uint8_t kShoulderHeightFar  = 140;
        const float reach = coreRadiusM + shoulderM + edgeNoiseM;
        const double coreR = static_cast<double>(coreRadiusM);

        for (size_t i = 0; i + 1 < samples.size(); ++i)
        {
            const float ax = samples[i].x;
            const float az = samples[i].y;
            const float bx = samples[i + 1].x;
            const float bz = samples[i + 1].y;
            if (!std::isfinite(ax) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(bz))
                continue;
            const float sdx = bx - ax;
            const float sdz = bz - az;
            if (sdx * sdx + sdz * sdz < 1.0e-8f)
                continue;

            const float minX = ax < bx ? ax : bx;
            const float maxX = ax > bx ? ax : bx;
            const float minZ = az < bz ? az : bz;
            const float maxZ = az > bz ? az : bz;
            int c0x = static_cast<int>(std::floor((static_cast<double>(minX) - static_cast<double>(m_originX) - static_cast<double>(reach)) / static_cast<double>(kCoverCellSizeM))) - 1;
            int c1x = static_cast<int>(std::floor((static_cast<double>(maxX) - static_cast<double>(m_originX) + static_cast<double>(reach)) / static_cast<double>(kCoverCellSizeM))) + 1;
            int c0z = static_cast<int>(std::floor((static_cast<double>(minZ) - static_cast<double>(m_originZ) - static_cast<double>(reach)) / static_cast<double>(kCoverCellSizeM))) - 1;
            int c1z = static_cast<int>(std::floor((static_cast<double>(maxZ) - static_cast<double>(m_originZ) + static_cast<double>(reach)) / static_cast<double>(kCoverCellSizeM))) + 1;
            if (c0x < 0)
                c0x = 0;
            if (c0z < 0)
                c0z = 0;
            if (c1x >= m_cellsX)
                c1x = m_cellsX - 1;
            if (c1z >= m_cellsZ)
                c1z = m_cellsZ - 1;

            const double adx = static_cast<double>(ax);
            const double adz = static_cast<double>(az);
            const double bdx = static_cast<double>(bx);
            const double bdz = static_cast<double>(bz);
            for (int cz = c0z; cz <= c1z; ++cz)
            {
                const float z0 = m_originZ + static_cast<float>(cz) * kCoverCellSizeM;
                const float z1 = z0 + kCoverCellSizeM;
                for (int cx = c0x; cx <= c1x; ++cx)
                {
                    const float x0 = m_originX + static_cast<float>(cx) * kCoverCellSizeM;
                    const float x1 = x0 + kCoverCellSizeM;
                    const double hit = distSqSegmentRect(adx, adz, bdx, bdz, static_cast<double>(x0), static_cast<double>(z0), static_cast<double>(x1), static_cast<double>(z1));
                    if (hit <= coreR * coreR)
                    {
                        writeCell(cx, cz, 0, 0, minBlend);
                        continue;
                    }

                    const int qx = static_cast<int>(std::floor(static_cast<double>(x0)));
                    const int qz = static_cast<int>(std::floor(static_cast<double>(z0)));
                    const float n = hash21(qx, qz, edgeSeed);
                    float outer = coreRadiusM + shoulderM + (n * 2.0f - 1.0f) * edgeNoiseM;
                    if (outer < coreRadiusM)
                        outer = coreRadiusM;
                    if (!(hit <= static_cast<double>(outer) * static_cast<double>(outer)))
                        continue;
                    const double span = static_cast<double>(outer) - coreR;
                    if (!(span > 1.0e-4))
                        continue;
                    const double tcx = static_cast<double>(m_originX) + (static_cast<double>(cx) + 0.5) * static_cast<double>(kCoverCellSizeM);
                    const double tcz = static_cast<double>(m_originZ) + (static_cast<double>(cz) + 0.5) * static_cast<double>(kCoverCellSizeM);
                    double t = (distPointSegment(tcx, tcz, adx, adz, bdx, bdz) - coreR) / span;
                    if (t < 0.0)
                        t = 0.0;
                    if (t > 1.0)
                        t = 1.0;
                    const uint8_t coverage = roundByte(lerp(0.0, 255.0, t));
                    const uint8_t height   = roundByte(lerp(kShoulderHeightNear, kShoulderHeightFar, t));
                    writeCell(cx, cz, coverage, height, minBlend);
                }
            }
        }
    }

    void FoliageCover::overlappingTiles(float x0, float z0, float x1, float z1, int& tx0, int& tz0, int& tx1, int& tz1) const
    {
        auto indexOf = [](float world, float origin, int tiles) {
            if (tiles <= 0)
                return 0;
            int i = static_cast<int>(std::floor((static_cast<double>(world) - static_cast<double>(origin)) / static_cast<double>(kCoverTileMetres)));
            if (i < 0)
                i = 0;
            if (i >= tiles)
                i = tiles - 1;
            return i;
        };
        tx0 = indexOf(x0, m_originX, m_tilesX);
        tz0 = indexOf(z0, m_originZ, m_tilesZ);
        const float xEnd = x1 - 1.0e-3f;
        const float zEnd = z1 - 1.0e-3f;
        tx1 = indexOf(xEnd > x0 ? xEnd : x0, m_originX, m_tilesX);
        tz1 = indexOf(zEnd > z0 ? zEnd : z0, m_originZ, m_tilesZ);
        if (tx1 < tx0)
            tx1 = tx0;
        if (tz1 < tz0)
            tz1 = tz0;
    }

    CoverSaveResult FoliageCover::save(const std::filesystem::path& path, int tileX, int tileZ) const
    {
        const Tile* tile = tileAt(tileX, tileZ);
        if (!tile || !tile->texels || tile->cellsX <= 0 || tile->cellsZ <= 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: '{}' tile {},{} missing", path.string(), tileX, tileZ);
            return CoverSaveResult::Failed;
        }
        const uint64_t payload = static_cast<uint64_t>(tile->cellsX) * static_cast<uint64_t>(tile->cellsZ) * 2ull;
        const uint64_t bytes   = static_cast<uint64_t>(sizeof(CoverHeader)) + payload;
        if (bytes > kMaxFoliageSidecarBytes)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: '{}' exceeds kMaxFoliageSidecarBytes; wrote nothing", path.string());
            return CoverSaveResult::Refused;
        }

        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                DE_LOG_ERROR(LogCategory::Render, "FoliageCover: failed to create directory for '{}' ({})", path.string(), ec.message());
                return CoverSaveResult::Failed;
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: failed to open '{}' for write", path.string());
            return CoverSaveResult::Failed;
        }

        CoverHeader header;
        header.magic    = kCoverMagic;
        header.version  = kCoverVersion;
        header.tileX    = static_cast<int32_t>(tileX);
        header.tileZ    = static_cast<int32_t>(tileZ);
        header.width    = static_cast<uint32_t>(tile->cellsX);
        header.height   = static_cast<uint32_t>(tile->cellsZ);
        header.cellSize = kCoverCellSizeM;
        header.originX  = m_originX;
        header.originZ  = m_originZ;
        header.reserved = 0;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(tile->texels), static_cast<std::streamsize>(payload));
        out.flush();
        if (!out)
        {
            out.close();
            std::error_code removeEc;
            std::filesystem::remove(path, removeEc);
            DE_LOG_ERROR(LogCategory::Render, "FoliageCover: failed while writing '{}'", path.string());
            return CoverSaveResult::Failed;
        }
        return CoverSaveResult::Ok;
    }

    bool FoliageCover::rejectLoad(const std::filesystem::path& path, int expectTileX, int expectTileZ)
    {
        DE_LOG_ERROR(LogCategory::Render, "FoliageCover: '{}' magic/version/cellSize/size rejected", path.string());
        if (!m_valid || expectTileX < 0 || expectTileZ < 0 || expectTileX >= m_tilesX || expectTileZ >= m_tilesZ)
            reset();
        else
            clearTile(expectTileX, expectTileZ);
        return false;
    }

    bool FoliageCover::load(const std::filesystem::path& path, int expectTileX, int expectTileZ)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
            return rejectLoad(path, expectTileX, expectTileZ);
        const std::uintmax_t fileBytes = std::filesystem::file_size(path, ec);
        if (ec)
            return rejectLoad(path, expectTileX, expectTileZ);
        if (static_cast<uint64_t>(fileBytes) > kMaxFoliageSidecarBytes || static_cast<uint64_t>(fileBytes) < sizeof(CoverHeader))
            return rejectLoad(path, expectTileX, expectTileZ);

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return rejectLoad(path, expectTileX, expectTileZ);

        CoverHeader header;
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!in)
            return rejectLoad(path, expectTileX, expectTileZ);
        (void)header.reserved;

        if (header.magic != kCoverMagic || header.version != kCoverVersion || header.cellSize != kCoverCellSizeM)
            return rejectLoad(path, expectTileX, expectTileZ);
        if (header.tileX != expectTileX || header.tileZ != expectTileZ)
            return rejectLoad(path, expectTileX, expectTileZ);

        const Tile* tile = tileAt(expectTileX, expectTileZ);
        if (!tile || tile->cellsX <= 0 || tile->cellsZ <= 0)
            return rejectLoad(path, expectTileX, expectTileZ);
        if (header.width != static_cast<uint32_t>(tile->cellsX) || header.height != static_cast<uint32_t>(tile->cellsZ))
            return rejectLoad(path, expectTileX, expectTileZ);

        const uint64_t payload = static_cast<uint64_t>(tile->cellsX) * static_cast<uint64_t>(tile->cellsZ) * 2ull;
        const uint64_t need    = static_cast<uint64_t>(sizeof(CoverHeader)) + payload;
        if (need > kMaxFoliageSidecarBytes || static_cast<uint64_t>(fileBytes) < need)
            return rejectLoad(path, expectTileX, expectTileZ);

        if (!allocateTile(expectTileX, expectTileZ))
            return false;
        Tile* dest = tileAt(expectTileX, expectTileZ);
        if (!dest || !dest->texels)
            return false;
        in.read(reinterpret_cast<char*>(dest->texels), static_cast<std::streamsize>(payload));
        if (!in)
        {
            clearTile(expectTileX, expectTileZ);
            return rejectLoad(path, expectTileX, expectTileZ);
        }
        return true;
    }

} // namespace Dark::Terrain
