#pragma once

#include "Terrain/GrassTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Dark
{
    class Frustum3f;
}

namespace Dark::Terrain
{
    // Resident 8 m tile. minY/maxY are blade roots. The caller pads height and wind lean.
    struct GrassTileVolume
    {
        int      lod      = 0;
        int      tileX    = 0;
        int      tileZ    = 0;
        uint32_t begin    = 0;
        uint32_t count    = 0;
        uint32_t windSlot = 0;
        float    minY     = 0.0f;
        float    maxY     = 0.0f;
    };

    // Keeps a tile when its padded box meets the frustum, and copies the whole blade span.
    // fade <= 0 is past the draw distance and is dropped. A partial overlap still draws every blade.
    uint32_t selectVisibleGrassTiles(const GrassTileVolume* tiles, uint32_t tileCount, const GrassTileWind* wind, uint32_t windCount, const Frustum3f& frustum,
                                     float horizontalPad, float verticalPad, GrassTileDraw* out, uint32_t cap);

    class TerrainGrid;

    struct GrassPlayerSample
    {
        float x       = 0.0f;
        float z       = 0.0f;
        float velX    = 0.0f;
        float velZ    = 0.0f;
        bool  shoving = false;
    };

    class GrassField
    {
    public:
        struct BladeSpan
        {
            const GrassBlade* data  = nullptr;
            uint32_t          count = 0;
        };

        GrassField()  = default;
        ~GrassField() = default;

        GrassField(const GrassField&)            = delete;
        GrassField& operator=(const GrassField&) = delete;
        GrassField(GrassField&&)                 = delete;
        GrassField& operator=(GrassField&&)      = delete;

        void               setParams(const GrassParams& params);
        const GrassParams& params() const { return m_params; }

        void reset();
        void update(const TerrainGrid& grid, float playerX, float playerZ, double playTimeSec, float waterLevel, const GrassPlayerSample& player = {});

        static void tileIndex(float worldX, float worldZ, int& tileX, int& tileZ);

        BladeSpan blades(int lod) const;
        // Bit 0 is frame slot 0, bit 1 is frame slot 1. removeSpan and append set both.
        uint8_t   lodStaleMask(int lod) const;
        void      clearLodStale(int lod, uint32_t frameSlot);
        // Tiles whose 8 m square meets the frustum. Each draw is that tile's full blade span.
        uint32_t  visibleTiles(const Frustum3f& frustum, GrassTileDraw* out, uint32_t cap) const;
        BladeSpan tileBlades(int tileX, int tileZ) const;
        int       residentLod(int tileX, int tileZ) const;
        uint32_t  residentTileCount() const { return static_cast<uint32_t>(m_tiles.size()); }

        const GrassTileWind* tileWind() const { return m_wind.empty() ? nullptr : m_wind.data(); }
        uint32_t              tileWindCount() const;

        const GrassInteractor* interactors() const { return m_interactor; }
        const GrassInteractor* prevInteractors() const { return m_prevInteractor; }
        const GrassInteractor* footprints() const { return m_footprint; }
        const GrassInteractor* prevFootprints() const { return m_prevFootprint; }
        void                   writeFrameInteraction(GrassFrameConstants& frame) const;

    private:
        struct Tile
        {
            int      tileX    = 0;
            int      tileZ    = 0;
            int      lod      = 0;
            int      keepK    = 0;
            uint32_t windSlot = 0;
            uint32_t begin    = 0;
            uint32_t count    = 0;
            float    minY     = 0.0f;
            float    maxY     = 0.0f;
            bool     dirty    = false;
            uint64_t kept[64]{};
        };

        struct Region
        {
            std::vector<GrassBlade> blades;
        };

        void     ensureWind();
        uint32_t allocWindSlot();
        void     freeWindSlot(uint32_t slot);
        void     sampleWind(float playerX, float playerZ, double playTimeSec);
        void     clearInteraction();
        void     updateInteraction(const GrassPlayerSample& player, double playTimeSec);
        void     decayFootprints(float dt);
        void     placeFootprint(float x, float z);
        void     warnFull(int lod);

        Tile*       findTile(int tileX, int tileZ);
        const Tile* findTile(int tileX, int tileZ) const;

        uint32_t appendBlades(int lod, const GrassBlade* src, uint32_t count);
        void     removeSpan(int lod, uint32_t begin, uint32_t count);
        void     markLodStale(int lod);
        void     eraseTileAt(size_t index);

        bool splatReady(const TerrainGrid& grid, float x, float z) const;
        bool acceptCandidate(const TerrainGrid& grid, int i, uint32_t tileSeed, float originX, float originZ, uint32_t tileSlot, float waterLevel, GrassBlade& out) const;
        void buildSubset(const TerrainGrid& grid, int tileX, int tileZ, int newK, int oldK, const uint64_t* oldBits, const GrassBlade* oldBlades, uint32_t oldCount,
                         uint32_t tileSlot, float waterLevel, bool resampleAll, uint64_t* newBits);

        bool commitBlades(size_t index, int newLod, int newK, const uint64_t* newBits, bool clearDirty);
        bool tryCreate(const TerrainGrid& grid, int tileX, int tileZ, int lod, float waterLevel);
        bool tryPromote(const TerrainGrid& grid, size_t index, int newLod, float waterLevel);
        bool tryRebuild(const TerrainGrid& grid, size_t index, float waterLevel);
        bool tryDemote(const TerrainGrid& grid, size_t index);

        GrassParams                          m_params{};
        std::vector<Tile>                    m_tiles;
        std::unordered_map<uint64_t, uint32_t> m_lookup;
        Region                               m_region[kGrassLodCount];
        uint8_t                              m_lodStale[kGrassLodCount]{};
        std::vector<GrassTileWind>          m_wind;
        std::vector<uint32_t>               m_freeSlots;
        std::vector<GrassBlade>             m_scratch;
        bool                                m_loggedNoSplat = false;
        bool                                m_refused[kGrassLodCount]{};
        GrassInteractor                     m_interactor[4]{};
        GrassInteractor                     m_prevInteractor[4]{};
        GrassInteractor                     m_footprint[8]{};
        GrassInteractor                     m_prevFootprint[8]{};
        float                               m_footAge[8]{};
        float                               m_velX        = 0.0f;
        float                               m_velZ        = 0.0f;
        float                               m_prevVelX    = 0.0f;
        float                               m_prevVelZ    = 0.0f;
        float                               m_dropX       = 0.0f;
        float                               m_dropZ       = 0.0f;
        bool                                m_hasDrop     = false;
        bool                                m_hasClock    = false;
        double                              m_lastPlaySec = 0.0;
    };

} // namespace Dark::Terrain
