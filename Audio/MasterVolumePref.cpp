#include "Audio/MasterVolumePref.h"

#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        std::filesystem::path settingsPath()
        {
            char*  env = nullptr;
            size_t len = 0;
            if (_dupenv_s(&env, &len, "LOCALAPPDATA") != 0 || !env || env[0] == '\0')
            {
                free(env);
                return {};
            }
            std::filesystem::path path(env);
            free(env);
            return path / "DarkEngine6" / "settings.json";
        }

        float clampVolume(float volume)
        {
            if (!(volume >= 0.0f))
                return 0.0f;
            if (volume > 1.0f)
                return 1.0f;
            return volume;
        }

    } // namespace

    float loadMasterVolume(float fallback)
    {
        const std::filesystem::path path = settingsPath();
        if (path.empty())
            return clampVolume(fallback);
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return clampVolume(fallback);
        std::ostringstream text;
        text << in.rdbuf();
        const nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return clampVolume(fallback);
        const auto it = root.find("masterVolume");
        if (it == root.end() || !it->is_number())
            return clampVolume(fallback);
        return clampVolume(it->get<float>());
    }

    bool saveMasterVolume(float volume)
    {
        const std::filesystem::path path = settingsPath();
        if (path.empty())
            return false;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            DE_LOG_WARN(LogCategory::Audio, "Settings: could not create {}", path.parent_path().string());
            return false;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DE_LOG_WARN(LogCategory::Audio, "Settings: could not write {}", path.string());
            return false;
        }
        out << "{\n  \"masterVolume\": " << clampVolume(volume) << "\n}\n";
        return static_cast<bool>(out);
    }

} // namespace Dark
