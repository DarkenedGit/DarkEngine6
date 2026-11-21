#include "Render/ShaderKey.h"

#include <algorithm>
#include <format>
#include <utility>

namespace Dark
{

    namespace
    {
        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::string_view trim(std::string_view s)
        {
            while (!s.empty() && isSpace(s.front()))
                s.remove_prefix(1);
            while (!s.empty() && isSpace(s.back()))
                s.remove_suffix(1);
            return s;
        }

        void splitWords(std::string_view line, std::vector<std::string_view>& out)
        {
            out.clear();
            size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && isSpace(line[i]))
                    ++i;
                const size_t start = i;
                while (i < line.size() && !isSpace(line[i]))
                    ++i;
                if (i > start)
                    out.push_back(line.substr(start, i - start));
            }
        }

        bool isIdentifier(std::string_view s)
        {
            if (s.empty())
                return false;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const char c     = s[i];
                const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
                const bool digit = c >= '0' && c <= '9';
                if (!(alpha || (digit && i > 0)))
                    return false;
            }
            return true;
        }
    } // namespace

    const char* shaderStageId(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:
            return "vs";
        case ShaderStage::Pixel:
            return "ps";
        case ShaderStage::Compute:
            return "cs";
        }
        return "vs";
    }

    bool shaderStageFromId(std::string_view id, ShaderStage& out)
    {
        if (id == "vs")
            out = ShaderStage::Vertex;
        else if (id == "ps")
            out = ShaderStage::Pixel;
        else if (id == "cs")
            out = ShaderStage::Compute;
        else
            return false;
        return true;
    }

    std::string shaderProfile(ShaderStage stage, bool fxc)
    {
        return std::format("{}_{}", shaderStageId(stage), fxc ? kShaderModelFxc : kShaderModelDxc);
    }

    std::string canonicalShaderDefines(std::span<const ShaderDefine> defines)
    {
        std::vector<const ShaderDefine*> sorted;
        sorted.reserve(defines.size());
        for (const ShaderDefine& d : defines)
            sorted.push_back(&d);
        std::stable_sort(sorted.begin(), sorted.end(), [](const ShaderDefine* a, const ShaderDefine* b) { return a->name < b->name; });
        std::string out;
        for (const ShaderDefine* d : sorted)
        {
            if (!out.empty())
                out += ',';
            out += d->name;
            out += '=';
            out += d->value;
        }
        return out;
    }

    bool splitShaderDefines(std::string_view defines, std::vector<ShaderDefine>& out)
    {
        out.clear();
        if (defines.empty())
            return true;
        size_t start = 0;
        while (start <= defines.size())
        {
            size_t end = defines.find(',', start);
            if (end == std::string_view::npos)
                end = defines.size();
            const std::string_view pair = defines.substr(start, end - start);
            const size_t           eq   = pair.find('=');
            if (eq == std::string_view::npos || !isIdentifier(pair.substr(0, eq)))
                return false;
            out.push_back({ std::string(pair.substr(0, eq)), std::string(pair.substr(eq + 1)) });
            if (end == defines.size())
                break;
            start = end + 1;
        }
        return true;
    }

    bool shaderFileFromContentPath(std::string_view relativeUnderContent, std::string& outFile)
    {
        std::string path(relativeUnderContent);
        std::replace(path.begin(), path.end(), '\\', '/');
        constexpr std::string_view kPrefix = "shaders/";
        if (path.size() <= kPrefix.size() || path.compare(0, kPrefix.size(), kPrefix) != 0)
            return false;
        outFile = path.substr(kPrefix.size());
        return true;
    }

    std::string formatShaderKey(const ShaderKey& key)
    {
        std::string line = std::format("{} {} {}", key.file, key.entry, shaderStageId(key.stage));
        if (!key.defines.empty())
        {
            line += ' ';
            line += key.defines;
        }
        return line;
    }

    std::string bakedShaderFileName(const ShaderKey& key)
    {
        std::string                stem = key.file;
        constexpr std::string_view kExt = ".hlsl";
        if (stem.size() > kExt.size() && stem.compare(stem.size() - kExt.size(), kExt.size(), kExt) == 0)
            stem.resize(stem.size() - kExt.size());
        std::string name = std::format("{}.{}.{}", stem, key.entry, shaderStageId(key.stage));
        if (!key.defines.empty())
        {
            name += '.';
            name += key.defines;
        }
        for (char& c : name)
        {
            if (c == '=')
                c = '-';
            else if (c == ',' || c == '/')
                c = '_';
            else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
                c = '_';
        }
        return name + ".dxil";
    }

    bool ShaderManifest::contains(const ShaderKey& key) const
    {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }

    bool parseShaderManifest(std::string_view text, ShaderManifest& out, std::string& error)
    {
        out.keys.clear();
        error.clear();
        std::vector<std::string_view> words;
        std::vector<ShaderDefine>     defines;
        std::vector<std::string>      names;
        size_t                        lineNo = 0;
        size_t                        start  = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos)
                end = text.size();
            const std::string_view line = trim(text.substr(start, end - start));
            start                       = end + 1;
            ++lineNo;
            if (line.empty() || line.front() == '#')
                continue;

            splitWords(line, words);
            if (words.size() < 3 || words.size() > 4)
            {
                error = std::format("line {}: expected '<file> <entry> <stage> [defines]'", lineNo);
                return false;
            }
            ShaderKey key;
            key.file  = std::string(words[0]);
            key.entry = std::string(words[1]);
            if (key.file.find('\\') != std::string::npos || key.file.size() < 6 || key.file.compare(key.file.size() - 5, 5, ".hlsl") != 0)
            {
                error = std::format("line {}: file '{}' must be a .hlsl path with '/' separators", lineNo, key.file);
                return false;
            }
            if (!isIdentifier(key.entry))
            {
                error = std::format("line {}: bad entry point '{}'", lineNo, key.entry);
                return false;
            }
            if (!shaderStageFromId(words[2], key.stage))
            {
                error = std::format("line {}: stage '{}' is not vs, ps or cs", lineNo, words[2]);
                return false;
            }
            if (words.size() == 4)
            {
                if (!splitShaderDefines(words[3], defines))
                {
                    error = std::format("line {}: defines '{}' must be NAME=VALUE pairs separated by ','", lineNo, words[3]);
                    return false;
                }
                key.defines = canonicalShaderDefines(defines);
                if (key.defines != words[3])
                {
                    error = std::format("line {}: defines must be sorted by name: '{}'", lineNo, key.defines);
                    return false;
                }
            }
            if (out.contains(key))
            {
                error = std::format("line {}: duplicate permutation '{}'", lineNo, formatShaderKey(key));
                return false;
            }
            const std::string name = bakedShaderFileName(key);
            if (std::find(names.begin(), names.end(), name) != names.end())
            {
                error = std::format("line {}: baked name '{}' collides with an earlier line", lineNo, name);
                return false;
            }
            names.push_back(name);
            out.keys.push_back(std::move(key));
        }
        return true;
    }

    bool isDxilContainer(const void* data, size_t size)
    {
        // DxilContainerHeader: 'DXBC', 16-byte digest, u16 major, u16 minor, u32 size, u32 part count,
        // then u32 part offsets. Each part starts with its FourCC and u32 size.
        const unsigned char* p      = static_cast<const unsigned char*>(data);
        auto                 read32 = [p](size_t at)
        { return static_cast<uint32_t>(p[at]) | (static_cast<uint32_t>(p[at + 1]) << 8) | (static_cast<uint32_t>(p[at + 2]) << 16) | (static_cast<uint32_t>(p[at + 3]) << 24); };
        if (!p || size < 32 || p[0] != 'D' || p[1] != 'X' || p[2] != 'B' || p[3] != 'C')
            return false;
        const uint32_t parts = read32(28);
        if (parts > (size - 32) / 4)
            return false;
        for (uint32_t i = 0; i < parts; ++i)
        {
            const uint32_t offset = read32(32 + 4 * static_cast<size_t>(i));
            if (offset > size - 8)
                return false;
            if (p[offset] == 'D' && p[offset + 1] == 'X' && p[offset + 2] == 'I' && p[offset + 3] == 'L')
                return true;
        }
        return false;
    }

    uint64_t hashBytes64(const void* data, size_t size, uint64_t seed)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        uint64_t             h = seed;
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 0x100000001b3ull;
        }
        return h;
    }

    uint64_t hashString64(std::string_view text, uint64_t seed)
    {
        return hashBytes64(text.data(), text.size(), seed);
    }

} // namespace Dark
