#pragma once

#include "Math/Vector2f.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Dark::Terrain
{
    constexpr float    kCoverCellSizeM  = 1.0f;
    constexpr float    kCoverTileMetres = 512.0f;
    constexpr int      kCoverTileCells  = 512;
    constexpr uint32_t kCoverMagic      = 0x56434544u; // bytes 'D','E','C','V' little-endian
    constexpr uint32_t kCoverVersion    = 1u;

    struct CoverSample
    {
        uint8_t coverage    = 255;
        uint8_t heightScale = 255;
    };

    // Refused leaves any previous file in place. Failed is an open or write error.
    enum class CoverSaveResult : uint8_t
    {
        Ok,
        Refused,
        Failed
    };

    // 1 m cells, one payload per 512 m tile. Move-only; payloads are nothrow allocations.
    class FoliageCover
    {
    public:
        FoliageCover() = default;
        ~FoliageCover();

        FoliageCover(const FoliageCover&)            = delete;
        FoliageCover& operator=(const FoliageCover&) = delete;
        FoliageCover(FoliageCover&& other) noexcept;
        FoliageCover& operator=(FoliageCover&& other) noexcept;

        // Allocates every tile and fills 255. False only when the extent is unusable or an allocation fails.
        bool create(float worldExtentX, float worldExtentZ, float originX, float originZ);
        // Tile table only. load allocates a payload when a file is present.
        bool createIndex(float worldExtentX, float worldExtentZ, float originX, float originZ);
        void clear(uint8_t coverage, uint8_t heightScale);
        void reset();

        bool  valid() const { return m_valid; }
        float cellSize() const { return m_valid ? kCoverCellSizeM : 0.0f; }
        int   tilesX() const { return m_tilesX; }
        int   tilesZ() const { return m_tilesZ; }
        int   cellsX() const { return m_cellsX; }
        int   cellsZ() const { return m_cellsZ; }
        float originX() const { return m_originX; }
        float originZ() const { return m_originZ; }
        float extentX() const { return m_extentX; }
        float extentZ() const { return m_extentZ; }

        CoverSample sampleNearest(float worldX, float worldZ) const;
        CoverSample sampleBilinear(float worldX, float worldZ) const;

        void stampDisk(float x, float z, float radiusM, uint8_t coverage, uint8_t heightScale, bool minBlend);
        void stampSoftAnnulus(float x, float z, float innerM, float outerM, uint8_t innerCoverage, uint8_t outerCoverage, uint8_t innerHeight, uint8_t outerHeight, bool minBlend);
        // samples: Vector2f::y is world z. Core radius is fixed; edgeSeed only moves the shoulder.
        void stampTrail(const std::vector<Math::Vector2f>& samples, float coreRadiusM, float shoulderM, float edgeNoiseM, uint32_t edgeSeed, bool minBlend);

        CoverSaveResult save(const std::filesystem::path& path, int tileX, int tileZ) const;
        bool load(const std::filesystem::path& path, int expectTileX, int expectTileZ);

        // Inclusive tile range overlapped by the world rect [x0, x1) x [z0, z1).
        void overlappingTiles(float x0, float z0, float x1, float z1, int& tx0, int& tz0, int& tx1, int& tz1) const;

    private:
        struct Tile
        {
            uint8_t* texels = nullptr;
            int      cellsX = 0;
            int      cellsZ = 0;
        };

        bool  axisLayout(float extent, int& cells, int& tiles) const;
        int   tileCellsOnAxis(int tile, int cells) const;
        Tile* tileAt(int tx, int tz);
        const Tile* tileAt(int tx, int tz) const;
        bool  allocateTile(int tx, int tz);
        void  clearTile(int tx, int tz);
        CoverSample atCell(int cx, int cz) const;
        void  writeCell(int cx, int cz, uint8_t coverage, uint8_t heightScale, bool minBlend);
        bool  rejectLoad(const std::filesystem::path& path, int expectTileX, int expectTileZ);

        Tile*  m_tiles   = nullptr;
        int    m_tilesX  = 0;
        int    m_tilesZ  = 0;
        int    m_cellsX  = 0;
        int    m_cellsZ  = 0;
        float  m_extentX = 0.0f;
        float  m_extentZ = 0.0f;
        float  m_originX = 0.0f;
        float  m_originZ = 0.0f;
        bool   m_valid   = false;
    };

} // namespace Dark::Terrain
