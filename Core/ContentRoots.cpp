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

} // namespace Dark
