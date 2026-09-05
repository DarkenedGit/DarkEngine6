#pragma once

#include "Render/Texture2D.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Dark
{

    class Renderer;

    // Thread-safe GPU texture intern. Same key → same Texture2D instance.
    // In-flight loads are single-flight (waiters block; only one decode/upload).
    // Failed loads are not cached, so a later call can retry.
    class TextureCache
    {
    public:
        TextureCache() = default;

        TextureCache(const TextureCache&)            = delete;
        TextureCache& operator=(const TextureCache&) = delete;

        // Canonical cache key for a filesystem path (absolute, weakly canonical, lower-case).
        static std::string normalizePath(const std::filesystem::path& path);

        static std::string fileKey(const std::filesystem::path& path);
        static std::string solidKey(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
        static std::string softCircleKey(uint32_t size);

        // Returns a resident texture or creates it via factory. Factory runs once per key.
        // If factory returns null, nothing is stored (retry later).
        template <typename Factory>
        std::shared_ptr<Texture2D> getOrCreate(const std::string& key, Factory&& factory);

        std::shared_ptr<Texture2D> loadFile(Renderer& renderer, const std::filesystem::path& path);
        std::shared_ptr<Texture2D> loadSolid(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        std::shared_ptr<Texture2D> loadSoftCircle(Renderer& renderer, uint32_t size = 64);

        std::shared_ptr<Texture2D> find(const std::string& key) const;
        bool contains(const std::string& key) const;
        size_t size() const;

        // Drop entries whose only remaining owner is the cache.
        void collectUnused();
        void clear();

    private:
        struct InFlight
        {
            std::mutex                    mutex;
            std::condition_variable       cv;
            std::shared_ptr<Texture2D>    result;
            bool                          claimed = false;
            bool                          done    = false;
        };

        std::shared_ptr<Texture2D> finishCreate(
            const std::string& key,
            const std::shared_ptr<InFlight>& flight,
            std::shared_ptr<Texture2D> tex);

        mutable std::mutex m_mutex;
        std::mutex         m_gpuMutex; // Renderer uploads are not thread-safe
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_entries;
        std::unordered_map<std::string, std::shared_ptr<InFlight>>  m_inflight;
    };

    template <typename Factory>
    std::shared_ptr<Texture2D> TextureCache::getOrCreate(const std::string& key, Factory&& factory)
    {
        if (key.empty())
            return {};

        std::shared_ptr<InFlight> flight;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto existing = m_entries.find(key);
            if (existing != m_entries.end() && existing->second)
                return existing->second;

            auto& slot = m_inflight[key];
            if (!slot)
                slot = std::make_shared<InFlight>();
            flight = slot;
        }

        bool isLoader = false;
        {
            std::lock_guard<std::mutex> lock(flight->mutex);
            if (!flight->claimed)
            {
                flight->claimed = true;
                isLoader        = true;
            }
        }

        if (!isLoader)
        {
            std::unique_lock<std::mutex> lock(flight->mutex);
            flight->cv.wait(lock, [&]() { return flight->done; });
            return flight->result;
        }

        std::shared_ptr<Texture2D> tex = factory();
        return finishCreate(key, flight, std::move(tex));
    }

} // namespace Dark
