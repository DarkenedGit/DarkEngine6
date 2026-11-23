#pragma once

#include "Math/Vector3f.h"

#include <cstdint>

// The terrain height field that render passes sample (fog's terrain-height lookup). The Terrain
// module implements it (Terrain::HeightMap), so Render code never includes Terrain headers. CPU only.

namespace Dark
{

    // A regular grid of width x height samples; sample (0,0) sits at `origin` in world XZ and +X / +Z
    // walk the grid by `cellSize`.
    struct HeightGrid
    {
        Math::Vector3f origin{};
        float          cellSize = 0.0f;
        uint32_t       width    = 0;
        uint32_t       height   = 0;
    };

    class IHeightProvider
    {
    public:
        // False (and `out` untouched) when there is no valid height field.
        virtual bool heightGrid(HeightGrid& out) const = 0;

    protected:
        IHeightProvider()                                  = default;
        IHeightProvider(const IHeightProvider&)            = default;
        IHeightProvider& operator=(const IHeightProvider&) = default;
        ~IHeightProvider()                                 = default;
    };

} // namespace Dark
