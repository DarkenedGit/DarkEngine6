#include "Terrain/TerrainLodFile.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Dark::Terrain
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        TerrainLodSettings g_settings{};
        bool               g_loaded   = false;
        bool               g_fromFile = false;

        bool readFloat(const json& v, float& out)
        {
            if (v.is_number_float())
            {
                out = static_cast<float>(v.get<double>());
                return std::isfinite(out);
            }
            if (v.is_number_integer())
            {
                out = static_cast<float>(v.get<int64_t>());
                return std::isfinite(out);
            }
            if (v.is_number_unsigned())
            {
                out = static_cast<float>(v.get<uint64_t>());
                return std::isfinite(out);
            }
            return false;
        }

        bool readPositiveIncreasing(const json& arr, float* out, int count)
        {
            if (!arr.is_array() || static_cast<int>(arr.size()) != count || !out)
                return false;
            for (int i = 0; i < count; ++i)
            {
                float v = 0.0f;
                if (!readFloat(arr[i], v) || !(v > 0.0f))
                    return false;
                if (i > 0 && !(v > out[i - 1]))
                    return false;
                out[i] = v;
            }
            return true;
        }

        bool readGrass(const json& grass, GrassLodDistances& out)
        {
            float nominal[4]{};
            float promote[3]{};
            float demote[3]{};
            const auto nom = grass.find("nominal");
            const auto pro = grass.find("promote");
            const auto dem = grass.find("demote");
            if (nom == grass.end() || pro == grass.end() || dem == grass.end())
                return false;
            if (!readPositiveIncreasing(*nom, nominal, 4) || !readPositiveIncreasing(*pro, promote, 3) || !readPositiveIncreasing(*dem, demote, 3))
                return false;
            for (int i = 0; i < 3; ++i)
            {
                if (!(promote[i] < nominal[i]) || !(nominal[i] < demote[i]) || !(demote[i] < nominal[i + 1]))
                    return false;
            }
            float createRadius = 0.0f;
            float evictRadius  = 0.0f;
            float fadeStart    = 0.0f;
            float fadeEnd      = 0.0f;
            const auto create = grass.find("createRadius");
            const auto evict  = grass.find("evictRadius");
            const auto fade0  = grass.find("fadeStart");
            const auto fade1  = grass.find("fadeEnd");
            if (create == grass.end() || evict == grass.end() || fade0 == grass.end() || fade1 == grass.end())
                return false;
            if (!readFloat(*create, createRadius) || !readFloat(*evict, evictRadius) || !readFloat(*fade0, fadeStart) || !readFloat(*fade1, fadeEnd))
                return false;
            if (!(createRadius > 0.0f) || createRadius != nominal[3] || !(evictRadius > createRadius))
                return false;
            if (!(fadeStart > 0.0f) || !(fadeEnd > fadeStart) || !(fadeEnd <= evictRadius))
                return false;
            out.nominal0     = nominal[0];
            out.nominal1     = nominal[1];
            out.nominal2     = nominal[2];
            out.nominal3     = nominal[3];
            out.promote0     = promote[0];
            out.promote1     = promote[1];
            out.promote2     = promote[2];
            out.demote0      = demote[0];
            out.demote1      = demote[1];
            out.demote2      = demote[2];
            out.createRadius = createRadius;
            out.evictRadius  = evictRadius;
            out.fadeStart    = fadeStart;
            out.fadeEnd      = fadeEnd;
            return true;
        }

        bool loadFile(TerrainLodSettings& out)
        {
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / "terrain" / "lod.json";
                std::error_code             ec;
                if (!std::filesystem::is_regular_file(path, ec) || ec)
                    continue;
                const auto sz = std::filesystem::file_size(path, ec);
                if (ec || sz == 0 || sz > kMaxJsonBytes)
                {
                    DE_LOG_ERROR(LogCategory::Render, "TerrainLod: unreadable '{}'", path.string());
                    return false;
                }
                std::ifstream in(path, std::ios::binary);
                if (!in)
                    return false;
                std::ostringstream ss;
                ss << in.rdbuf();
                if (!parseTerrainLodSettings(ss.str(), out))
                {
                    DE_LOG_ERROR(LogCategory::Render, "TerrainLod: parse failed '{}'", path.string());
                    return false;
                }
                DE_LOG_INFO(LogCategory::Render, "TerrainLod: loaded '{}'", path.string());
                return true;
            }
            DE_LOG_WARN(LogCategory::Render, "TerrainLod: terrain/lod.json not found; using built-in distances");
            return false;
        }
    }

    TerrainLodSettings builtinTerrainLodSettings()
    {
        return {};
    }

    bool parseTerrainLodSettings(std::string_view jsonText, TerrainLodSettings& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: discarded or non-object json");
            return false;
        }
        const auto ver = root.find("version");
        if (ver != root.end())
        {
            float version = 0.0f;
            if (!readFloat(*ver, version) || static_cast<int>(version) != 1)
            {
                DE_LOG_ERROR(LogCategory::Render, "TerrainLod: unsupported version");
                return false;
            }
        }
        const auto terrain = root.find("terrain");
        const auto grass   = root.find("grass");
        if (terrain == root.end() || !terrain->is_object() || grass == root.end() || !grass->is_object())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: terrain and grass objects are required");
            return false;
        }
        const auto multiples = terrain->find("chunkMultiples");
        if (multiples == terrain->end() || !multiples->is_array())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: chunkMultiples is required");
            return false;
        }
        const int count = static_cast<int>(multiples->size());
        if (count < 1 || count > kMaxLodLevels)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: chunkMultiples count must be 1..{}", kMaxLodLevels);
            return false;
        }
        float parsedMultiples[kMaxLodLevels]{};
        if (!readPositiveIncreasing(*multiples, parsedMultiples, count))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: chunkMultiples must be positive and increasing");
            return false;
        }
        GrassLodDistances parsedGrass;
        if (!readGrass(*grass, parsedGrass))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainLod: grass distances are invalid");
            return false;
        }
        out.terrainCount = count;
        for (int i = 0; i < count; ++i)
            out.terrainMultiples[i] = parsedMultiples[i];
        out.grass = parsedGrass;
        return true;
    }

    const TerrainLodSettings& terrainLodSettings()
    {
        if (!g_loaded)
        {
            g_loaded = true;
            TerrainLodSettings loaded = g_settings;
            g_fromFile = loadFile(loaded);
            if (g_fromFile)
                g_settings = loaded;
        }
        return g_settings;
    }

    bool terrainLodLoadedFromFile()
    {
        terrainLodSettings();
        return g_fromFile;
    }

} // namespace Dark::Terrain
