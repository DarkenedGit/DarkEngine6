#include "Scene/SceneCatalog.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Dark
{
namespace
{

using json = nlohmann::json;

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

} // namespace

std::filesystem::path findScenesDirectory()
{
    namespace fs = std::filesystem;
    for (const fs::path& root : contentRootCandidates())
    {
        const fs::path  dir = root / "scenes";
        std::error_code ec;
        if (!dir.empty() && fs::is_directory(dir, ec) && !ec)
        {
            const fs::path canonical = fs::weakly_canonical(dir, ec);
            return ec ? dir : canonical;
        }
    }
    return {};
}

bool peekSceneFile(const std::filesystem::path& path, SceneFileInfo& out, std::string* errorOut)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ec)
    {
        if (errorOut)
            *errorOut = "scene file not found: " + path.string();
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (errorOut)
            *errorOut = "failed to open " + path.string();
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const json root = json::parse(ss.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        if (errorOut)
            *errorOut = "invalid JSON in " + path.string();
        return false;
    }

    SceneFileInfo info{};
    info.path     = path;
    info.fileName = path.filename().string();
    info.version  = 1;
    if (root.contains("version") && root["version"].is_number_integer())
        info.version = root["version"].get<int>();

    if (root.contains("name") && root["name"].is_string())
        info.displayName = root["name"].get<std::string>();
    if (info.displayName.empty())
        info.displayName = path.stem().string();

    info.mode = SceneMode::Scene3D;
    if (root.contains("mode") && root["mode"].is_string())
    {
        SceneMode parsed{};
        if (tryParseSceneMode(root["mode"].get<std::string>(), parsed))
            info.mode = parsed;
    }

    out = std::move(info);
    return true;
}

std::vector<SceneFileInfo> listSceneFiles(SceneMode mode, const std::filesystem::path& scenesDir)
{
    namespace fs = std::filesystem;
    std::vector<SceneFileInfo> out;
    std::error_code            ec;
    if (scenesDir.empty() || !fs::is_directory(scenesDir, ec) || ec)
        return out;

    fs::directory_iterator it(scenesDir, ec);
    if (ec)
        return out;

    const fs::directory_iterator end{};
    for (; it != end; it.increment(ec))
    {
        if (ec)
            break;
        const fs::directory_entry& ent = *it;
        std::error_code            fileEc;
        if (!ent.is_regular_file(fileEc) || fileEc)
            continue;
        const fs::path& p = ent.path();
        if (!iequals(p.extension().string(), ".json"))
            continue;

        SceneFileInfo info{};
        std::string   err;
        if (!peekSceneFile(p, info, &err))
        {
            if (!err.empty())
                DE_LOG_WARN("SceneCatalog: skip {} — {}", p.string(), err);
            continue;
        }
        if (info.mode != mode)
            continue;
        out.push_back(std::move(info));
    }

    std::sort(out.begin(), out.end(), [](const SceneFileInfo& a, const SceneFileInfo& b) {
        if (a.displayName != b.displayName)
            return a.displayName < b.displayName;
        return a.fileName < b.fileName;
    });
    return out;
}

std::vector<SceneFileInfo> listSceneFiles(SceneMode mode)
{
    return listSceneFiles(mode, findScenesDirectory());
}

const SceneFileInfo* findSceneByFileName(const std::vector<SceneFileInfo>& scenes, std::string_view preferredName)
{
    if (preferredName.empty())
        return nullptr;
    for (const SceneFileInfo& s : scenes)
    {
        if (iequals(s.fileName, preferredName))
            return &s;
    }
    return nullptr;
}

} // namespace Dark
