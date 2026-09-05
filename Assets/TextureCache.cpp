#include "Assets/TextureCache.h"
#include "Core/Log.h"
#include "Render/Renderer.h"

#include <cstdio>

namespace Dark
{
    std::string TextureCache::normalizePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        std::error_code             ec;
        std::filesystem::path       abs = std::filesystem::absolute(path, ec);
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

    std::string TextureCache::fileKey(const std::filesystem::path& path)
    {
        return std::string("f:") + normalizePath(path);
    }

    std::string TextureCache::solidKey(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "s:%u,%u,%u,%u", r, g, b, a);
        return buf;
    }

    std::string TextureCache::softCircleKey(uint32_t size)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "c:%u", size);
        return buf;
    }

    std::shared_ptr<Texture2D> TextureCache::finishCreate(
        const std::string& key,
        const std::shared_ptr<InFlight>& flight,
        std::shared_ptr<Texture2D> tex)
    {
        {
            std::lock_guard<std::mutex> cacheLock(m_mutex);
            std::lock_guard<std::mutex> flightLock(flight->mutex);
            flight->result = tex;
            flight->done   = true;
            if (flight->result)
                m_entries[key] = flight->result;
            m_inflight.erase(key);
            flight->cv.notify_all();
        }
        return flight->result;
    }

    std::shared_ptr<Texture2D> TextureCache::loadFile(Renderer& renderer, const std::filesystem::path& path)
    {
        const std::string key = fileKey(path);
        return getOrCreate(key,
                           [this, &renderer, &path]() -> std::shared_ptr<Texture2D>
                           {
                               auto tex = std::make_shared<Texture2D>();
                               std::lock_guard<std::mutex> upload(m_gpuMutex);
                               if (!tex->createFromFile(renderer, path))
                                   return {};
                               return tex;
                           });
    }

    std::shared_ptr<Texture2D> TextureCache::loadSolid(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const std::string key = solidKey(r, g, b, a);
        return getOrCreate(key,
                           [this, &renderer, r, g, b, a]() -> std::shared_ptr<Texture2D>
                           {
                               auto tex = std::make_shared<Texture2D>();
                               std::lock_guard<std::mutex> upload(m_gpuMutex);
                               if (!tex->createSolidColor(renderer, r, g, b, a))
                                   return {};
                               return tex;
                           });
    }

    std::shared_ptr<Texture2D> TextureCache::loadSoftCircle(Renderer& renderer, uint32_t size)
    {
        const std::string key = softCircleKey(size);
        return getOrCreate(key,
                           [this, &renderer, size]() -> std::shared_ptr<Texture2D>
                           {
                               auto tex = std::make_shared<Texture2D>();
                               std::lock_guard<std::mutex> upload(m_gpuMutex);
                               if (!tex->createSoftCircle(renderer, size))
                                   return {};
                               return tex;
                           });
    }

    std::shared_ptr<Texture2D> TextureCache::find(const std::string& key) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_entries.find(key);
        if (it == m_entries.end())
            return {};
        return it->second;
    }

    bool TextureCache::contains(const std::string& key) const
    {
        return static_cast<bool>(find(key));
    }

    size_t TextureCache::size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries.size();
    }

    void TextureCache::collectUnused()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (!it->second || it->second.use_count() == 1)
                it = m_entries.erase(it);
            else
                ++it;
        }
    }

    void TextureCache::clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }
} // namespace Dark
