#include "AI/WalkabilityMesh.h"

#include "AI/Walkability.h"
#include "Core/Log.h"
#include "Terrain/HeightMap.h"

#include <cmath>
#include <vector>

namespace Dark::AI
{
    namespace
    {
        constexpr int   kQuadCap = 262144;
        constexpr float kAlpha   = 0.55f;

        void islandColor(int island, float& r, float& g, float& b)
        {
            const float h = std::fmod(static_cast<float>(island) * 0.6180339887f, 1.0f);
            const float s = 0.62f;
            const float v = 0.92f;
            const float hf = h * 6.0f;
            const int   i  = static_cast<int>(std::floor(hf)) % 6;
            const float f  = hf - std::floor(hf);
            const float p  = v * (1.0f - s);
            const float q  = v * (1.0f - s * f);
            const float t  = v * (1.0f - s * (1.0f - f));
            switch (i)
            {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
            }
        }

        void pushQuad(WalkabilityMeshData& out, const Terrain::HeightMap& hm, int x, int z, int w, int h, float r, float g, float b)
        {
            const float x0 = hm.worldX(x);
            const float z0 = hm.worldZ(z);
            const float x1 = hm.worldX(x + w);
            const float z1 = hm.worldZ(z + h);
            const float y00 = hm.heightAtWorld(x0, z0) + kWalkabilityMeshLift;
            const float y10 = hm.heightAtWorld(x1, z0) + kWalkabilityMeshLift;
            const float y11 = hm.heightAtWorld(x1, z1) + kWalkabilityMeshLift;
            const float y01 = hm.heightAtWorld(x0, z1) + kWalkabilityMeshLift;
            const uint32_t base = static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back(WalkabilityMeshVertex{ x0, y00, z0, r, g, b, kAlpha });
            out.vertices.push_back(WalkabilityMeshVertex{ x1, y10, z0, r, g, b, kAlpha });
            out.vertices.push_back(WalkabilityMeshVertex{ x1, y11, z1, r, g, b, kAlpha });
            out.vertices.push_back(WalkabilityMeshVertex{ x0, y01, z1, r, g, b, kAlpha });
            out.indices.push_back(base);
            out.indices.push_back(base + 1);
            out.indices.push_back(base + 2);
            out.indices.push_back(base);
            out.indices.push_back(base + 2);
            out.indices.push_back(base + 3);
        }
    } // namespace

    bool buildWalkabilityMesh(const Walkability& walk, WalkabilityMeshData& out)
    {
        out.vertices.clear();
        out.indices.clear();
        const Terrain::HeightMap* hm = walk.heightMap();
        if (!walk.valid() || !hm)
            return false;

        const int cellsX = walk.cellsX();
        const int cellsZ = walk.cellsZ();
        std::vector<uint8_t> used(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsZ), 0);
        int quads = 0;
        bool truncated = false;

        for (int z = 0; z < cellsZ; ++z)
        {
            for (int x = 0; x < cellsX; ++x)
            {
                const size_t start = static_cast<size_t>(z) * static_cast<size_t>(cellsX) + static_cast<size_t>(x);
                if (used[start] || !walk.walkable(x, z))
                    continue;
                if (quads >= kQuadCap)
                {
                    truncated = true;
                    break;
                }
                const int id = walk.island(x, z);
                int w = 1;
                while (x + w < cellsX && w < kWalkabilityMeshSpan)
                {
                    const size_t at = static_cast<size_t>(z) * static_cast<size_t>(cellsX) + static_cast<size_t>(x + w);
                    if (used[at] || !walk.walkable(x + w, z) || walk.island(x + w, z) != id)
                        break;
                    ++w;
                }
                int h = 1;
                while (z + h < cellsZ && h < kWalkabilityMeshSpan)
                {
                    bool row = true;
                    for (int k = 0; k < w; ++k)
                    {
                        const size_t at = static_cast<size_t>(z + h) * static_cast<size_t>(cellsX) + static_cast<size_t>(x + k);
                        if (used[at] || !walk.walkable(x + k, z + h) || walk.island(x + k, z + h) != id)
                        {
                            row = false;
                            break;
                        }
                    }
                    if (!row)
                        break;
                    ++h;
                }
                for (int dz = 0; dz < h; ++dz)
                {
                    for (int dx = 0; dx < w; ++dx)
                    {
                        const size_t at = static_cast<size_t>(z + dz) * static_cast<size_t>(cellsX) + static_cast<size_t>(x + dx);
                        used[at] = 1;
                    }
                }
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                islandColor(id, r, g, b);
                pushQuad(out, *hm, x, z, w, h, r, g, b);
                ++quads;
            }
            if (truncated)
                break;
        }

        if (truncated)
            DE_LOG_WARN(LogCategory::AI, "Walkability mesh: stopped at {} quads", kQuadCap);
        return true;
    }
} // namespace Dark::AI
