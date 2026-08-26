#include "Render/LoadingScreenConfig.h"
#include "Core/Application.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/Version.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <unordered_set>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 1024u * 1024u;

        float clampMinSeconds(float v)
        {
            if (v < 0.0f)
                return 0.0f;
            if (v > 30.0f)
                return 30.0f;
            return v;
        }

        bool jsonToFloat(const json& v, float& out)
        {
            if (!v.is_number())
                return false;
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

        bool readFloat(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            return jsonToFloat(*it, out);
        }

        bool readBool(const json& obj, const char* key, bool& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_boolean())
                return false;
            if (const auto* b = it->get_ptr<const json::boolean_t*>())
            {
                out = *b;
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool readColor(const json& obj, const char* key, float out[4])
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_array())
                return false;
            const json& arr = *it;
            if (arr.size() < 3 || arr.size() > 4)
                return false;
            float tmp[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            size_t i     = 0;
            for (auto el = arr.begin(); el != arr.end(); ++el, ++i)
            {
                if (i >= 4 || !jsonToFloat(*el, tmp[i]))
                    return false;
            }
            out[0] = tmp[0];
            out[1] = tmp[1];
            out[2] = tmp[2];
            out[3] = tmp[3];
            return true;
        }

        void mergeLayer(LoadingScreenConfig::Layer& dst, const json& obj)
        {
            if (!obj.is_object())
                return;
            readString(obj, "image", dst.image);
            float ms = dst.minSeconds;
            if (readFloat(obj, "minSeconds", ms))
                dst.minSeconds = clampMinSeconds(ms);
            readString(obj, "title", dst.title);
        }

        void mergeLegal(LoadingScreenConfig::Legal& dst, const json& obj)
        {
            if (!obj.is_object())
                return;
            readBool(obj, "showCopyright", dst.showCopyright);
            readString(obj, "copyright", dst.copyright);
        }

        void mergeVersionText(LoadingScreenConfig::VersionText& dst, const json& obj)
        {
            if (!obj.is_object())
                return;
            readBool(obj, "showEngine", dst.showEngine);
            readBool(obj, "showHost", dst.showHost);
            readBool(obj, "showGit", dst.showGit);
            readString(obj, "anchor", dst.anchor);
        }

        void warnSkipOnce(const std::string& key, const char* reason)
        {
            static std::unordered_set<std::string> s_warned;
            if (!s_warned.insert(key).second)
                return;
            DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: skip layer '{}': {}", key, reason);
        }

        bool pathHasDotDot(const std::filesystem::path& p)
        {
            for (const auto& part : p)
            {
                if (part == "..")
                    return true;
            }
            return false;
        }

        bool pathIsInsideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
        {
            const std::filesystem::path rel = candidate.lexically_relative(root);
            if (rel.empty())
                return false;
            if (rel.has_root_path())
                return false;
            if (pathHasDotDot(rel))
                return false;
            return true;
        }

        bool readJsonFileCapped(const std::filesystem::path& path, std::string& outText)
        {
            outText.clear();
            std::error_code        ec;
            const std::uintmax_t   sz = std::filesystem::file_size(path, ec);
            if (ec)
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: cannot stat '{}'", path.string());
                return false;
            }
            if (sz > kMaxJsonBytes)
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: '{}' exceeds 1 MB ({} bytes)", path.string(), static_cast<unsigned long long>(sz));
                return false;
            }

            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: cannot open '{}'", path.string());
                return false;
            }

            outText.resize(static_cast<size_t>(sz));
            if (sz > 0 && !file.read(outText.data(), static_cast<std::streamsize>(sz)))
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: read failed for '{}'", path.string());
                outText.clear();
                return false;
            }
            return true;
        }

        void applyJsonFile(LoadingScreenConfig& cfg, const std::string& virtualPath)
        {
            std::filesystem::path resolved;
            if (!resolveSplashAsset(virtualPath, resolved))
            {
                warnSkipOnce(virtualPath, "missing or rejected");
                return;
            }
            std::string text;
            if (!readJsonFileCapped(resolved, text))
            {
                warnSkipOnce(virtualPath, "unreadable");
                return;
            }
            if (!mergeLoadingScreenConfigJson(cfg, text, virtualPath.c_str()))
                warnSkipOnce(virtualPath, "discarded JSON");
        }

        void appendPart(std::string& line, std::string_view part)
        {
            if (part.empty())
                return;
            if (!line.empty())
                line += "  \xC2\xB7  ";
            line.append(part.data(), part.size());
        }

    } // namespace

    bool mergeLoadingScreenConfigJson(LoadingScreenConfig& cfg, std::string_view text, const char* sourceName)
    {
        const std::string owned(text);
        const json        j = json::parse(owned, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            const char* src = (sourceName && sourceName[0]) ? sourceName : "<json>";
            DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: discarded JSON from '{}'", src);
            return false;
        }

        float schema = 0.0f;
        if (readFloat(j, "schemaVersion", schema))
        {
            cfg.schemaVersion = static_cast<int>(schema);
            if (cfg.schemaVersion > 1)
            {
                const char* src = (sourceName && sourceName[0]) ? sourceName : "<json>";
                DE_LOG_WARN(LogCategory::Render, "LoadingScreenConfig: schemaVersion {} > 1 in '{}', parsing known fields", cfg.schemaVersion, src);
            }
        }

        readBool(j, "enabled", cfg.enabled);
        readBool(j, "skipOnKey", cfg.skipOnKey);
        readBool(j, "reducedMotion", cfg.reducedMotion);
        readColor(j, "background", cfg.background);
        readColor(j, "spinnerColor", cfg.spinnerColor);
        readString(j, "animation", cfg.animation);

        const auto engineIt = j.find("engine");
        if (engineIt != j.end())
            mergeLayer(cfg.engine, *engineIt);

        const auto hostIt = j.find("host");
        if (hostIt != j.end())
            mergeLayer(cfg.host, *hostIt);

        const auto legalIt = j.find("legal");
        if (legalIt != j.end())
            mergeLegal(cfg.legal, *legalIt);

        const auto versionIt = j.find("versionText");
        if (versionIt != j.end())
            mergeVersionText(cfg.versionText, *versionIt);

        return true;
    }

    bool resolveSplashAsset(const std::string& virtualPath, std::filesystem::path& out)
    {
        namespace fs = std::filesystem;
        out.clear();

        if (virtualPath.empty())
            return false;

        const fs::path v(virtualPath);
        if (v.is_absolute() || v.has_root_name() || v.has_root_directory())
        {
            DE_LOG_WARN(LogCategory::Render, "resolveSplashAsset: absolute path rejected '{}'", virtualPath);
            return false;
        }
        if (pathHasDotDot(v))
        {
            DE_LOG_WARN(LogCategory::Render, "resolveSplashAsset: path escape rejected '{}'", virtualPath);
            return false;
        }

        std::error_code ec;
        for (const fs::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;

            const fs::path joined = root / v;
            fs::path       canon  = fs::weakly_canonical(joined, ec);
            if (ec)
                canon = joined.lexically_normal();

            fs::path rootCanon = fs::weakly_canonical(root, ec);
            if (ec)
                rootCanon = root.lexically_normal();

            if (!pathIsInsideRoot(canon, rootCanon))
            {
                DE_LOG_WARN(LogCategory::Render, "resolveSplashAsset: '{}' escapes content root", virtualPath);
                continue;
            }

            if (fs::exists(canon, ec) && !ec && fs::is_regular_file(canon, ec) && !ec)
            {
                out = canon;
                return true;
            }
        }
        return false;
    }

    bool loadLoadingScreenConfig(const AppConfig& app, LoadingScreenConfig& out)
    {
        out = LoadingScreenConfig{};

        applyJsonFile(out, "loading/engine.json");

        const char* hostId = (app.hostId && app.hostId[0]) ? app.hostId : "app";
        applyJsonFile(out, std::string("loading/") + hostId + ".json");

        if (app.loadingConfig && app.loadingConfig[0])
            applyJsonFile(out, app.loadingConfig);

        return true;
    }

    std::string makeLoadingVersionLine(const LoadingScreenConfig& cfg, const char* hostName, const char* hostVersion)
    {
        std::string line;

        if (cfg.versionText.showEngine)
        {
            std::string engine = cfg.engine.title.empty() ? "DarkEngine6" : cfg.engine.title;
            engine += ' ';
            engine += kEngineVersion;
            if (cfg.versionText.showGit && kEngineHasGit)
            {
                engine += " (";
                std::string git = kEngineGit;
                if (git.size() > 7)
                    git.resize(7);
                engine += git;
                engine += ')';
            }
            line = std::move(engine);
        }

        if (cfg.versionText.showHost && hostName && hostName[0])
        {
            std::string host = hostName;
            if (hostVersion && hostVersion[0])
            {
                host += ' ';
                host += hostVersion;
            }
            appendPart(line, host);
        }

        if (cfg.legal.showCopyright && !cfg.legal.copyright.empty())
            appendPart(line, cfg.legal.copyright);

        return line;
    }

} // namespace Dark
