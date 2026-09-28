#include "Terrain/TerrainGround.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace Dark::Terrain
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        int layerIndex(std::string_view id)
        {
            if (id == "dirt")
                return 0;
            if (id == "grass")
                return 1;
            if (id == "rock")
                return 2;
            if (id == "snow")
                return 3;
            return -1;
        }

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = static_cast<float>(*f);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<float>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<float>(*u);
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool readFloat(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            return jsonToFloat(*it, out);
        }
    }

    TerrainGround::TerrainGround()
    {
        setDefaults();
    }

    void TerrainGround::setDefaults()
    {
        const char* ids[] = { "dirt", "grass", "rock", "snow" };
        const float speeds[] = { 1.0f, 1.0f, 0.75f, 0.75f };
        const float hz[] = { 90.0f, 160.0f, 280.0f, 50.0f };
        for (int i = 0; i < kMaxTerrainLayers; ++i)
        {
            m_id[i]       = ids[i];
            m_cue[i]      = std::string("step_") + ids[i];
            m_footstep[i] = std::string("audio/foot_") + ids[i] + ".wav";
            m_layers[i].moveSpeed = speeds[i];
            m_layers[i].blipHz    = hz[i];
            m_layers[i].layer     = i;
            m_layers[i].id        = m_id[i].c_str();
            m_layers[i].cue       = m_cue[i].c_str();
            m_layers[i].footstep  = m_footstep[i].c_str();
        }
    }

    const GroundContact& TerrainGround::layer(int index) const
    {
        if (index < 0 || index >= kMaxTerrainLayers)
            return m_layers[1];
        return m_layers[index];
    }

    bool TerrainGround::parse(std::string_view jsonText)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "TerrainGround: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Core, "TerrainGround: discarded or non-object json");
            return false;
        }
        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Core, "TerrainGround: unsupported version");
                return false;
            }
        }
        const auto layersIt = root.find("layers");
        if (layersIt == root.end() || !layersIt->is_array())
        {
            DE_LOG_ERROR(LogCategory::Core, "TerrainGround: missing layers");
            return false;
        }

        TerrainGround next;
        next.setDefaults();
        for (const json& item : *layersIt)
        {
            if (!item.is_object())
                continue;
            std::string id;
            if (!readString(item, "id", id))
                continue;
            const int index = layerIndex(id);
            if (index < 0)
            {
                DE_LOG_WARN(LogCategory::Core, "TerrainGround: unknown layer '{}'", id);
                continue;
            }
            float speed = next.m_layers[index].moveSpeed;
            float hz    = next.m_layers[index].blipHz;
            readFloat(item, "moveSpeed", speed);
            readFloat(item, "blipHz", hz);
            if (!(speed >= 0.0f) || speed > 2.0f || !(hz >= 1.0f) || hz > 8000.0f)
            {
                DE_LOG_ERROR(LogCategory::Core, "TerrainGround: bad numbers for '{}'", id);
                return false;
            }
            next.m_layers[index].moveSpeed = speed;
            next.m_layers[index].blipHz    = hz;
            std::string foot;
            if (readString(item, "footstep", foot) && !foot.empty())
                next.m_footstep[index] = std::move(foot);
            next.m_layers[index].id       = next.m_id[index].c_str();
            next.m_layers[index].cue      = next.m_cue[index].c_str();
            next.m_layers[index].footstep = next.m_footstep[index].c_str();
        }
        *this = std::move(next);
        for (int i = 0; i < kMaxTerrainLayers; ++i)
        {
            m_layers[i].id       = m_id[i].c_str();
            m_layers[i].cue      = m_cue[i].c_str();
            m_layers[i].footstep = m_footstep[i].c_str();
        }
        return true;
    }

    bool TerrainGround::loadFromContent()
    {
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / "terrain" / "ground.json";
            std::error_code             ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                continue;
            const auto sz = std::filesystem::file_size(path, ec);
            if (ec || sz > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Core, "TerrainGround: unreadable '{}'", path.string());
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            if (!parse(ss.str()))
            {
                DE_LOG_ERROR(LogCategory::Core, "TerrainGround: parse failed '{}'", path.string());
                return false;
            }
            DE_LOG_INFO(LogCategory::Core, "TerrainGround: loaded '{}'", path.string());
            return true;
        }
        DE_LOG_WARN(LogCategory::Core, "TerrainGround: terrain/ground.json not found; using built-in speeds");
        return false;
    }

    GroundContact TerrainGround::at(const HeightMap* height, const SplatMap* splat, float worldX, float worldZ) const
    {
        int layer = 1;
        if (height && height->valid() && splat && splat->valid())
        {
            float fx = 0.0f;
            float fz = 0.0f;
            height->worldToSample(worldX, worldZ, fx, fz);
            if (splat->width() != height->width() || splat->height() != height->height())
            {
                const float sx = (height->width() > 1) ? fx / static_cast<float>(height->width() - 1) : 0.0f;
                const float sz = (height->height() > 1) ? fz / static_cast<float>(height->height() - 1) : 0.0f;
                fx = sx * static_cast<float>(splat->width() > 0 ? splat->width() - 1 : 0);
                fz = sz * static_cast<float>(splat->height() > 0 ? splat->height() - 1 : 0);
            }
            float w[kMaxTerrainLayers] = {};
            splat->sampleWeights(fx, fz, w);
            float best = -1.0f;
            for (int i = 0; i < kMaxTerrainLayers; ++i)
            {
                if (w[i] > best)
                {
                    best  = w[i];
                    layer = i;
                }
            }
        }
        return m_layers[layer];
    }
}
