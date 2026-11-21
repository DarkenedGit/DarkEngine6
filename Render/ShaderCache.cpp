#include "Render/ShaderCache.h"
#include "Render/ShaderKey.h"

#include <format>
#include <fstream>
#include <utility>

namespace Dark
{

    namespace
    {
        constexpr uint32_t kCacheMagic   = 0x43534544u; // 'DESC'
        constexpr uint32_t kCacheVersion = 1;

        void put32(std::vector<uint8_t>& out, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }

        void put64(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }

        struct Reader
        {
            std::span<const uint8_t> bytes;
            size_t                   pos = 0;

            bool get32(uint32_t& v)
            {
                if (bytes.size() - pos < 4)
                    return false;
                v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<uint32_t>(bytes[pos + static_cast<size_t>(i)]) << (8 * i);
                pos += 4;
                return true;
            }

            bool get64(uint64_t& v)
            {
                if (bytes.size() - pos < 8)
                    return false;
                v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= static_cast<uint64_t>(bytes[pos + static_cast<size_t>(i)]) << (8 * i);
                pos += 8;
                return true;
            }

            bool getBytes(size_t n, const uint8_t*& out)
            {
                if (bytes.size() - pos < n)
                    return false;
                out = bytes.data() + pos;
                pos += n;
                return true;
            }
        };

        bool readWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out)
        {
            out.clear();
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return false;
            const std::streamoff size = file.tellg();
            if (size < 0)
                return false;
            file.seekg(0, std::ios::beg);
            out.resize(static_cast<size_t>(size));
            if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size))
                return false;
            return true;
        }
    } // namespace

    uint64_t shaderCacheKey(std::string_view source, std::string_view sourcePath, std::string_view entry, std::string_view profile, std::string_view defines, std::string_view arguments,
                            std::string_view compilerVersion)
    {
        // Field lengths go into the hash so adjacent fields cannot run together.
        uint64_t h = hashString64("DarkEngine6.ShaderCache.v1");
        for (std::string_view part : { source, sourcePath, entry, profile, defines, arguments, compilerVersion })
        {
            const uint64_t len = part.size();
            h                  = hashBytes64(&len, sizeof(len), h);
            h                  = hashString64(part, h);
        }
        return h;
    }

    std::string shaderCacheFileName(uint64_t key)
    {
        return std::format("{:016x}.bin", key);
    }

    std::vector<uint8_t> serializeShaderCacheEntry(const ShaderCacheEntry& entry)
    {
        std::vector<uint8_t> out;
        out.reserve(32 + entry.bytecode.size());
        put32(out, kCacheMagic);
        put32(out, kCacheVersion);
        put64(out, entry.key);
        put32(out, static_cast<uint32_t>(entry.dependencies.size()));
        for (const ShaderCacheDependency& dep : entry.dependencies)
        {
            put32(out, static_cast<uint32_t>(dep.path.size()));
            out.insert(out.end(), dep.path.begin(), dep.path.end());
            put64(out, dep.hash);
        }
        put64(out, entry.bytecode.size());
        out.insert(out.end(), entry.bytecode.begin(), entry.bytecode.end());
        put64(out, hashBytes64(out.data(), out.size()));
        return out;
    }

    bool parseShaderCacheEntry(std::span<const uint8_t> bytes, ShaderCacheEntry& out)
    {
        out = {};
        if (bytes.size() < 8)
            return false;
        const std::span<const uint8_t> body = bytes.first(bytes.size() - 8);
        Reader                         tail{ bytes, bytes.size() - 8 };
        uint64_t                       checksum = 0;
        if (!tail.get64(checksum) || checksum != hashBytes64(body.data(), body.size()))
            return false;

        Reader   r{ body };
        uint32_t magic = 0, version = 0, depCount = 0;
        if (!r.get32(magic) || magic != kCacheMagic || !r.get32(version) || version != kCacheVersion)
            return false;
        if (!r.get64(out.key) || !r.get32(depCount))
            return false;
        for (uint32_t i = 0; i < depCount; ++i)
        {
            uint32_t              len = 0;
            const uint8_t*        p   = nullptr;
            ShaderCacheDependency dep;
            if (!r.get32(len) || !r.getBytes(len, p))
                return false;
            dep.path.assign(reinterpret_cast<const char*>(p), len);
            if (!r.get64(dep.hash))
                return false;
            out.dependencies.push_back(std::move(dep));
        }
        uint64_t       size = 0;
        const uint8_t* code = nullptr;
        if (!r.get64(size) || size != body.size() - r.pos || !r.getBytes(static_cast<size_t>(size), code))
            return false;
        out.bytecode.assign(code, code + size);
        return true;
    }

    bool hashShaderCacheFile(const std::filesystem::path& path, uint64_t& outHash)
    {
        std::vector<uint8_t> bytes;
        if (!readWholeFile(path, bytes))
            return false;
        outHash = hashBytes64(bytes.data(), bytes.size());
        return true;
    }

    bool shaderCacheDependenciesCurrent(const ShaderCacheEntry& entry)
    {
        for (const ShaderCacheDependency& dep : entry.dependencies)
        {
            uint64_t            hash = 0;
            const std::u8string utf8(reinterpret_cast<const char8_t*>(dep.path.data()), dep.path.size());
            if (!hashShaderCacheFile(std::filesystem::path(utf8), hash) || hash != dep.hash)
                return false;
        }
        return true;
    }

    bool loadShaderCacheEntry(const std::filesystem::path& dir, uint64_t key, ShaderCacheEntry& out)
    {
        std::vector<uint8_t> bytes;
        if (dir.empty() || !readWholeFile(dir / shaderCacheFileName(key), bytes))
            return false;
        if (!parseShaderCacheEntry(bytes, out) || out.key != key || out.bytecode.empty())
            return false;
        return shaderCacheDependenciesCurrent(out);
    }

    bool storeShaderCacheEntry(const std::filesystem::path& dir, const ShaderCacheEntry& entry)
    {
        if (dir.empty())
            return false;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return false;
        const std::filesystem::path finalPath = dir / shaderCacheFileName(entry.key);
        std::filesystem::path       tmpPath   = finalPath;
        tmpPath += ".tmp";
        {
            const std::vector<uint8_t> bytes = serializeShaderCacheEntry(entry);
            std::ofstream              file(tmpPath, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return false;
        }
        std::filesystem::rename(tmpPath, finalPath, ec);
        if (ec)
        {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
        return true;
    }

} // namespace Dark
