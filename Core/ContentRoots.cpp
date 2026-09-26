#include "Core/ContentRoots.h"
#include "Core/Paths.h"

namespace Dark
{
    namespace
    {
        void appendUnique(std::vector<std::filesystem::path>& out, const std::filesystem::path& raw)
        {
            if (raw.empty())
                return;

            std::error_code             ec;
            std::filesystem::path       key = std::filesystem::weakly_canonical(raw, ec);
            if (ec)
                key = raw.lexically_normal();

            for (const auto& existing : out)
            {
                if (existing == key)
                    return;
            }
            out.push_back(std::move(key));
        }

        void appendFromBase(std::vector<std::filesystem::path>& out, const std::filesystem::path& base, const std::filesystem::path& suffix)
        {
            if (base.empty())
                return;
            appendUnique(out, base / suffix);
        }

        std::filesystem::path normalizedPath(const std::filesystem::path& path)
        {
            std::error_code       ec;
            std::filesystem::path n = std::filesystem::weakly_canonical(path, ec);
            return ec ? path.lexically_normal() : n;
        }

        bool isInsideDirectory(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            if (path.empty() || root.empty())
                return false;
            const std::filesystem::path rel = normalizedPath(path).lexically_relative(normalizedPath(root));
            if (rel.empty())
                return false;
            for (const std::filesystem::path& part : rel)
            {
                if (part == "..")
                    return false;
            }
            return true;
        }
    } // namespace

    std::vector<std::filesystem::path> contentRootCandidates(const std::filesystem::path& exeDir, const std::filesystem::path& cwd)
    {
        namespace fs = std::filesystem;

        std::vector<fs::path> out;
        out.reserve(6);

        const fs::path suffixes[] = {
            fs::path("content"),
            fs::path("..") / ".." / ".." / "content",
            fs::path("..") / ".." / "content",
        };

        for (const fs::path& suffix : suffixes)
        {
            appendFromBase(out, exeDir, suffix);
            appendFromBase(out, cwd, suffix);
        }
        return out;
    }

    std::vector<std::filesystem::path> contentRootCandidates()
    {
        std::error_code             ec;
        std::filesystem::path       cwd = std::filesystem::current_path(ec);
        if (ec)
            cwd.clear();
        return contentRootCandidates(executableDirectory(), cwd);
    }

    std::filesystem::path authoringContentRoot(const std::filesystem::path& exeDir, const std::filesystem::path& cwd)
    {
        namespace fs = std::filesystem;

        fs::path fallbackExisting;
        fs::path fallbackAny;
        for (const fs::path& root : contentRootCandidates(exeDir, cwd))
        {
            if (fallbackAny.empty())
                fallbackAny = root;

            std::error_code ec;
            const bool      exists = fs::is_directory(root, ec) && !ec;
            if (!exists)
                continue;
            if (fallbackExisting.empty())
                fallbackExisting = root;
            if (!exeDir.empty() && isInsideDirectory(root, exeDir))
                continue;
            return root;
        }
        if (!fallbackExisting.empty())
            return fallbackExisting;
        return fallbackAny;
    }

    std::filesystem::path authoringContentRoot()
    {
        std::error_code       ec;
        std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (ec)
            cwd.clear();
        return authoringContentRoot(executableDirectory(), cwd);
    }

} // namespace Dark
