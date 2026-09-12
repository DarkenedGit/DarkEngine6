#include "Assets/ImageCache.h"
#include "Assets/Image.h"

#include <cstdio>

namespace Dark
{
    std::string ImageCache::normalizePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        std::error_code       ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (ec)
            abs = path;

        std::filesystem::path canonical = std::filesystem::weakly_canonical(abs, ec);
        if (ec)
            canonical = abs;

        std::string s = canonical.generic_string();
        for (char& c : s)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    }

    std::string ImageCache::fileKey(const std::filesystem::path& path)
    {
        return std::string("f:") + normalizePath(path);
    }

    std::string ImageCache::solidKey(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "s:%u,%u,%u,%u", r, g, b, a);
        return buf;
    }

    std::string ImageCache::softCircleKey(uint32_t size)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "c:%u", size);
        return buf;
    }

    std::string ImageCache::gltfKey(const std::string& normalizedGltfPath, int imageIndex)
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "gltf:%s:%d", normalizedGltfPath.c_str(), imageIndex);
        return buf;
    }

    AssetRef<Image> ImageCache::finishDecode(const std::string& key, const std::shared_ptr<InFlight>& flight, AssetRef<Image> img)
    {
        std::lock_guard<std::mutex> cacheLock(m_mutex);
        std::lock_guard<std::mutex> flightLock(flight->mutex);
        flight->result = std::move(img);
        flight->done   = true;
        m_inflight.erase(key);
        flight->cv.notify_all();
        return flight->result;
    }

} // namespace Dark
