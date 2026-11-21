#include "Scene/SceneId.h"

#include "Content/ContentDb.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include <cctype>
#include <string_view>

namespace Dark
{

    namespace
    {

        std::string lowerSlashes(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char c : text)
            {
                const char d = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (d == '/' && !out.empty() && out.back() == '/')
                    continue;
                out.push_back(d);
            }
            // "./" segments add nothing. weakly_canonical already folds "..".
            for (size_t at = out.find("/./"); at != std::string::npos; at = out.find("/./"))
                out.erase(at, 2);
            if (out.starts_with("./"))
                out.erase(0, 2);
            return out;
        }

        bool packageIdChars(std::string_view text)
        {
            if (text.size() < 2) // one letter is a drive, as in "c:/"
                return false;
            for (const char c : text)
            {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
                    return false;
            }
            return true;
        }

        // Text after "<root>/" when root is a prefix of path on a folder boundary.
        bool stripRoot(std::string_view path, std::string_view root, std::string_view& rest)
        {
            while (!root.empty() && root.back() == '/')
                root.remove_suffix(1);
            if (root.empty() || path.size() <= root.size() + 1 || !path.starts_with(root) || path[root.size()] != '/')
                return false;
            rest = path.substr(root.size() + 1);
            return true;
        }

        std::string sceneTail(std::string_view path)
        {
            if (path.starts_with("scenes/"))
                return std::string(path);
            const size_t at = path.rfind("/scenes/");
            if (at != std::string_view::npos)
                return std::string(path.substr(at + 1));
            const size_t           slash = path.rfind('/');
            const std::string_view name  = slash == std::string_view::npos ? path : path.substr(slash + 1);
            if (name.empty())
                return {};
            return "scenes/" + std::string(name);
        }

        // An unresolved identity matches only the same unresolved path (same file, same install).
        // It never matches a core or package id, so it cannot load as the default level.
        bool gateIds(const std::string& file, const std::string& ours, const std::string& fileIdentity)
        {
            if (file == ours)
                return true;
            if (file.starts_with(kUnresolvedScenePrefix))
                DE_LOG_WARN(LogCategory::Core, "Save: scene identity '{}' has no content root or mods/<package> folder here; not loading it as '{}'", fileIdentity, ours);
            return false;
        }

    } // namespace

    std::string sceneContentId(const std::filesystem::path& scenePath, const std::vector<SceneIdRoot>& roots)
    {
        const std::string path = lowerSlashes(scenePath.generic_string());
        if (path.empty())
            return {};

        // An identity already marked unresolved stays exactly as it is.
        if (path.starts_with(kUnresolvedScenePrefix))
            return path;
        // Already an id: "scenes/x.json" or "<package>:scenes/x.json".
        if (const size_t colon = path.find(':'); colon != std::string::npos && packageIdChars(path.substr(0, colon)) && path.compare(colon + 1, 7, "scenes/") == 0)
            return path.substr(0, colon) + ":" + sceneTail(path.substr(colon + 1));

        size_t           best = 0;
        std::string      package;
        std::string_view rest;
        for (const SceneIdRoot& root : roots)
        {
            const std::string lowered = lowerSlashes(root.root.generic_string());
            std::string_view  tail; // points into path
            if (!stripRoot(path, lowered, tail) || lowered.size() < best)
                continue;
            best    = lowered.size();
            rest    = tail;
            package = lowerSlashes(root.package);
        }
        if (best > 0 && !package.empty() && package != "core")
            return package + ":" + sceneTail(rest);

        // Core content, or no current root at all (a save from another install). Decide from the
        // folders right above "scenes/", never from the file name alone:
        //   <...>/content/scenes/x.json      core (legacy install layout)
        //   <...>/mods/<folder>/scenes/x.json package <folder> (or the current package rooted at that folder)
        //   anything else                    unresolved: kept verbatim, matches only itself
        const std::string_view search = best > 0 ? rest : std::string_view(path);
        const size_t           at     = search.starts_with("scenes/") ? 0 : search.rfind("/scenes/");
        if (at == std::string_view::npos)
        {
            if (best > 0 || search.find('/') != std::string_view::npos)
                return std::string(kUnresolvedScenePrefix) + path;
            return "scenes/" + std::string(search); // bare "level.json"
        }
        const std::string_view tail   = search.substr(at == 0 ? 0 : at + 1);
        const std::string_view prefix = search.substr(0, at);
        const size_t           cut    = prefix.rfind('/');
        const std::string_view parent = cut == std::string_view::npos ? prefix : prefix.substr(cut + 1);
        const std::string_view above  = cut == std::string_view::npos ? std::string_view{} : prefix.substr(0, cut);
        const bool             inMods = above == "mods" || above.ends_with("/mods");
        if (inMods && packageIdChars(parent) && parent != "core")
        {
            // A current package whose root folder has this name keeps its real id after a move.
            for (const SceneIdRoot& root : roots)
            {
                const std::string id = lowerSlashes(root.package);
                if (!id.empty() && id != "core" && lowerSlashes(root.root.filename().generic_string()) == parent)
                    return id + ":" + std::string(tail);
            }
            return std::string(parent) + ":" + std::string(tail);
        }
        if (at == 0 || (best == 0 && parent == "content" && !inMods) || (best > 0 && prefix.empty()))
            return std::string(tail);
        return std::string(kUnresolvedScenePrefix) + path;
    }

    std::string sceneContentId(const std::filesystem::path& scenePath)
    {
        // Roots and the path are compared in the same canonical form, so "..", symlinks and a relative root still match.
        const auto canonicalOf = [](const std::filesystem::path& p)
        {
            std::error_code             ec;
            const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
            return ec ? p : c;
        };
        std::vector<SceneIdRoot> roots;
        for (const ContentPackage& package : contentPackages())
        {
            if (package.enabled && !package.root.empty())
                roots.push_back(SceneIdRoot{ canonicalOf(package.root), package.id });
        }
        for (const std::filesystem::path& root : contentRootCandidates())
            roots.push_back(SceneIdRoot{ canonicalOf(root), std::string{} });
        // "scenes/x.json" and "pkg:scenes/x.json" are ids already. Any other relative path is a file under the cwd.
        const std::string text   = lowerSlashes(scenePath.generic_string());
        const bool        isId   = text.starts_with("scenes/") || text.find(':') != std::string::npos;
        const bool        onDisk = scenePath.is_absolute() || (!isId && !text.empty());
        return sceneContentId(onDisk ? canonicalOf(scenePath) : scenePath, roots);
    }

    bool sameSceneId(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        const std::string ia = sceneContentId(a);
        return !ia.empty() && ia == sceneContentId(b);
    }

    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId, const std::vector<SceneIdRoot>& roots)
    {
        const std::string ours = sceneContentId(ourId, roots);
        if (fileIdentity.empty())
            return ours.empty() || ours == kDefaultSceneId;
        return gateIds(sceneContentId(fileIdentity, roots), ours, fileIdentity);
    }

    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId)
    {
        const std::string ours = sceneContentId(ourId);
        if (fileIdentity.empty())
            return ours.empty() || ours == kDefaultSceneId;
        return gateIds(sceneContentId(fileIdentity), ours, fileIdentity);
    }

} // namespace Dark
