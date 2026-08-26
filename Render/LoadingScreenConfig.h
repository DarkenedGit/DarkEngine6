#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{

    struct AppConfig;

    struct LoadingScreenConfig
    {
        struct Layer
        {
            std::string image;
            float       minSeconds = 0.0f;
            std::string title;
        };

        struct Legal
        {
            bool        showCopyright = true;
            std::string copyright{ "\xC2\xA9 2026 Travis Johnston" };
        };

        struct VersionText
        {
            bool        showEngine = true;
            bool        showHost   = true;
            bool        showGit    = true;
            std::string anchor{ "bottom-left" };
        };

        int         schemaVersion = 1;
        bool        enabled       = true;
        bool        skipOnKey     = true;
        bool        reducedMotion = false;
        float       background[4]{ 0.05f, 0.05f, 0.07f, 1.0f };
        float       spinnerColor[4]{ 0.25f, 0.65f, 0.95f, 1.0f };
        std::string animation{ "ring" };
        Layer       engine{ "textures/loading/engine_logo.png", 2.0f, "DarkEngine6" };
        Layer       host{ "", 1.5f, "" };
        Legal       legal{};
        VersionText versionText{};
    };

    // Merge one JSON object onto cfg. Discarded / non-object JSON leaves cfg unchanged and returns false.
    bool mergeLoadingScreenConfigJson(LoadingScreenConfig& cfg, std::string_view text, const char* sourceName = nullptr);

    // Overlay: C++ defaults, content/loading/engine.json, content/loading/<hostId>.json, then AppConfig.loadingConfig.
    bool loadLoadingScreenConfig(const AppConfig& app, LoadingScreenConfig& out);

    // Restricted resolver: relative virtual path only, no '..', must exist inside a content root.
    bool resolveSplashAsset(const std::string& virtualPath, std::filesystem::path& out);

    std::string makeLoadingVersionLine(const LoadingScreenConfig& cfg, const char* hostName, const char* hostVersion);

} // namespace Dark
