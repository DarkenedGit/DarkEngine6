#include "Render/EchoChirpFile.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Render/EchoPipeline.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

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

        float clampWindow(float v, float lo, float hi)
        {
            if (v < lo)
                return lo;
            if (v > hi)
                return hi;
            return v;
        }
    }

    static_assert(EchoChirpSettings{}.periodSeconds == EchoPipeline::kPeriodSeconds, "builtin chirp period");
    static_assert(EchoChirpSettings{}.speedMetersPerSec == EchoPipeline::kSpeedMetersPerSec, "builtin chirp speed");

    EchoChirpSettings builtinEchoChirpSettings()
    {
        return {};
    }

    bool parseEchoChirpSettings(std::string_view jsonText, EchoChirpSettings& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Render, "EchoChirp: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Render, "EchoChirp: discarded or non-object json");
            return false;
        }
        const auto ver = root.find("version");
        if (ver != root.end())
        {
            float version = 0.0f;
            if (!readFloat(*ver, version) || static_cast<int>(version) != 1)
            {
                DE_LOG_ERROR(LogCategory::Render, "EchoChirp: unsupported version");
                return false;
            }
        }
        const auto period = root.find("periodSeconds");
        const auto speed  = root.find("speed");
        if (period == root.end() || speed == root.end())
        {
            DE_LOG_ERROR(LogCategory::Render, "EchoChirp: periodSeconds and speed are required");
            return false;
        }
        float periodSeconds = 0.0f;
        float speedMps      = 0.0f;
        if (!readFloat(*period, periodSeconds) || !(periodSeconds > 0.0f) || !readFloat(*speed, speedMps) || !(speedMps > 0.0f))
        {
            DE_LOG_ERROR(LogCategory::Render, "EchoChirp: periodSeconds and speed must be finite and positive");
            return false;
        }
        out.periodSeconds     = clampWindow(periodSeconds, EchoChirpSettings::kPeriodMin, EchoChirpSettings::kPeriodMax);
        out.speedMetersPerSec = clampWindow(speedMps, EchoChirpSettings::kSpeedMin, EchoChirpSettings::kSpeedMax);
        return true;
    }

    bool loadEchoChirpSettings(EchoChirpSettings& out)
    {
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / "render" / "echolocation.json";
            std::error_code             ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                continue;
            const auto sz = std::filesystem::file_size(path, ec);
            if (ec || sz == 0 || sz > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Render, "EchoChirp: unreadable '{}'", path.string());
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            if (!parseEchoChirpSettings(ss.str(), out))
            {
                DE_LOG_ERROR(LogCategory::Render, "EchoChirp: parse failed '{}'", path.string());
                return false;
            }
            DE_LOG_INFO(LogCategory::Render, "EchoChirp: loaded '{}' period {:.3f} s speed {:.1f} m/s", path.string(), static_cast<double>(out.periodSeconds), static_cast<double>(out.speedMetersPerSec));
            return true;
        }
        DE_LOG_WARN(LogCategory::Render, "EchoChirp: render/echolocation.json not found; using built-in period and speed");
        return false;
    }

} // namespace Dark
