#pragma once

#include "Assets/AssetHandle.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Dark
{

    class Image;

    // CPU decode intern helpers. Does not own Image (AssetManager::m_assets does).
    class ImageCache
    {
    public:
        ImageCache() = default;

        ImageCache(const ImageCache&)            = delete;
        ImageCache& operator=(const ImageCache&) = delete;

        static std::string normalizePath(const std::filesystem::path& path);
        static std::string fileKey(const std::filesystem::path& path);
        static std::string solidKey(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
        static std::string softCircleKey(uint32_t size);
        static std::string gltfKey(const std::string& normalizedGltfPath, int imageIndex);

        // Single-flight factory. Result is not retained here.
        template <typename Factory>
        AssetRef<Image> decodeOnce(const std::string& key, Factory&& factory);

    private:
        struct InFlight
        {
            std::mutex              mutex;
            std::condition_variable cv;
            AssetRef<Image>         result;
            bool                    claimed = false;
            bool                    done    = false;
        };

        AssetRef<Image> finishDecode(const std::string& key, const std::shared_ptr<InFlight>& flight, AssetRef<Image> img);

        mutable std::mutex                                         m_mutex;
        std::unordered_map<std::string, std::shared_ptr<InFlight>> m_inflight;
    };

    template <typename Factory>
    AssetRef<Image> ImageCache::decodeOnce(const std::string& key, Factory&& factory)
    {
        if (key.empty())
            return {};

        std::shared_ptr<InFlight> flight;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
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

        AssetRef<Image> img = factory();
        return finishDecode(key, flight, std::move(img));
    }

} // namespace Dark
