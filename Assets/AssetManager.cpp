#include "Assets/AssetManager.h"
#include "Audio/SoundClip.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "AI/HsmGraph.h"
#include "AI/HsmGraphJson.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Assets/Image.h"
#include "Assets/ImageCache.h"
#include "Assets/Model.h"
#include "Assets/Material.h"
#include "Core/Log.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace Dark
{

    AssetManager::AssetManager()  = default;
    AssetManager::~AssetManager() = default;

    void AssetManager::erasePathEntriesLocked(AssetID id)
    {
        std::erase_if(m_pathToID, [id](const auto& kv) { return kv.second == id; });
    }

    void AssetManager::mountDirectory(const std::filesystem::path& dir)
    {
        if (dir.empty())
        {
            DE_LOG_WARN("AssetManager: mount path is empty");
            return;
        }

        std::error_code             ec;
        const std::filesystem::path abs = std::filesystem::absolute(dir, ec);
        if (ec)
        {
            DE_LOG_WARN("AssetManager: cannot resolve mount path '{}': {}", dir.string(), ec.message());
            return;
        }

        if (!std::filesystem::exists(abs, ec) || ec)
        {
            DE_LOG_WARN("AssetManager: mount path does not exist: {} (cwd={})", abs.string(), std::filesystem::current_path().string());
            return;
        }

        if (!std::filesystem::is_directory(abs, ec) || ec)
        {
            DE_LOG_WARN("AssetManager: mount path is not a directory: {}", abs.string());
            return;
        }

        const std::filesystem::path  canonical = std::filesystem::weakly_canonical(abs, ec);
        const std::filesystem::path& mounted   = ec ? abs : canonical;

        std::lock_guard<std::mutex> lock(m_mutex);

        // Skip duplicate mounts.
        for (const auto& existing : m_mounts)
        {
            if (existing == mounted)
                return;
        }

        m_mounts.push_back(mounted);
        DE_LOG_INFO("AssetManager: mounted '{}'", m_mounts.back().string());
    }

    std::filesystem::path AssetManager::resolve(const std::string& vpath) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& mount : m_mounts)
        {
            const auto      candidate = mount / vpath;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec)
                return candidate;
        }
        return {};
    }

    std::string AssetManager::virtualPathFromAbsolute(const std::filesystem::path& absPath) const
    {
        const std::string norm = ImageCache::normalizePath(absPath);
        if (norm.empty())
            return {};

        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& mount : m_mounts)
        {
            std::string prefix = ImageCache::normalizePath(mount);
            if (prefix.empty())
                continue;
            if (prefix.back() != '/')
                prefix.push_back('/');
            if (norm.size() > prefix.size() && norm.compare(0, prefix.size(), prefix) == 0)
                return norm.substr(prefix.size());
        }
        return {};
    }

    AssetID AssetManager::registerAsset(AssetRef<Asset> asset, const std::string& cacheKey)
    {
        if (!asset)
        {
            DE_LOG_ERROR("AssetManager::registerAsset: null asset");
            return NULL_ASSET;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const AssetID id = allocID();
        asset->id        = id;
        m_assets[id]     = std::move(asset);
        if (!cacheKey.empty())
            m_pathToID[cacheKey] = id;
        DE_LOG_INFO("AssetManager: registered id={} type={}", id, static_cast<unsigned>(m_assets[id]->type));
        return id;
    }

    AssetRef<Material> AssetManager::internMaterial(AssetRef<Material> mat, const std::string& cacheKey)
    {
        if (!mat)
        {
            DE_LOG_ERROR("AssetManager::internMaterial: null material");
            return {};
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (mat->id != NULL_ASSET && cacheKey.empty())
            return mat;

        if (!cacheKey.empty())
        {
            if (AssetRef<Material> existing =
                    findInternedLocked<Material>(cacheKey, InternLookup::EraseMissingOrWrongType))
                return existing;
            if (mat->id != NULL_ASSET)
            {
                m_pathToID[cacheKey] = mat->id;
                return mat;
            }
        }

        const AssetID id = allocID();
        mat->id          = id;
        m_assets[id]     = mat;
        if (!cacheKey.empty())
            m_pathToID[cacheKey] = id;
        DE_LOG_INFO("AssetManager: interned material id={}", id);
        return mat;
    }

    AssetRef<Audio::SoundClip> AssetManager::internSoundClip(AssetRef<Audio::SoundClip> clip, const std::string& cacheKey)
    {
        if (!clip)
        {
            DE_LOG_ERROR("AssetManager::internSoundClip: null clip");
            return {};
        }
        clip->type = AssetType::Audio;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (clip->id != NULL_ASSET && cacheKey.empty())
            return clip;

        if (!cacheKey.empty())
        {
            if (AssetRef<Audio::SoundClip> existing =
                    findInternedLocked<Audio::SoundClip>(cacheKey, InternLookup::EraseMissingOrWrongType))
                return existing;
            if (clip->id != NULL_ASSET)
            {
                m_pathToID[cacheKey] = clip->id;
                return clip;
            }
        }

        const AssetID id = allocID();
        clip->id         = id;
        m_assets[id]     = clip;
        if (!cacheKey.empty())
            m_pathToID[cacheKey] = id;
        DE_LOG_INFO("AssetManager: interned audio id={}", id);
        return clip;
    }

    AssetRef<Audio::SoundClip> AssetManager::loadAudio(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: audio not found '{}'", virtualPath);
            return {};
        }
        const std::string key = std::string("a:") + ImageCache::normalizePath(path);
        if (AssetRef<Audio::SoundClip> existing = tryGetInterned<Audio::SoundClip>(key, InternLookup::KeepStale))
            return existing;
        auto clip = std::make_shared<Audio::SoundClip>();
        if (!clip->loadWav(path))
            return {};
        clip->setKey(key);
        return internSoundClip(std::move(clip), key);
    }

    AssetRef<Asset> AssetManager::get(AssetID id) const
    {
        if (id == NULL_ASSET)
            return {};
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_assets.find(id);
        if (it == m_assets.end())
            return {};
        return it->second;
    }

    void AssetManager::unload(AssetID id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assets.erase(id);
        erasePathEntriesLocked(id);
    }

    void AssetManager::collectGarbage()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<AssetID> removed;
            for (auto it = m_assets.begin(); it != m_assets.end();)
            {
                if (it->second.use_count() == 1)
                {
                    removed.push_back(it->first);
                    it = m_assets.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (const AssetID id : removed)
                erasePathEntriesLocked(id);
            DE_LOG_TRACE("AssetManager: GC pass complete ({} assets remaining)", m_assets.size());
        }
    }

    size_t AssetManager::assetCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_assets.size();
    }

    size_t AssetManager::pathMappingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pathToID.size();
    }

    AssetRef<Image> AssetManager::internDecodedImage(const std::string& key, AssetRef<Image> img)
    {
        if (!img || !img->valid())
            return {};
        std::lock_guard<std::mutex> lock(m_mutex);
        return internLoadedLocked<Image>(key, img);
    }

    AssetRef<Image> AssetManager::internImage(AssetRef<Image> image, const std::string& cacheKey)
    {
        if (cacheKey.empty())
        {
            DE_LOG_ERROR("AssetManager::internImage: empty cache key");
            return {};
        }
        return internDecodedImage(cacheKey, std::move(image));
    }

    AssetRef<Image> AssetManager::loadImageFile(const std::filesystem::path& absPath)
    {
        const std::string key = ImageCache::fileKey(absPath);
        if (AssetRef<Image> existing = tryGetInterned<Image>(key, InternLookup::KeepStale))
            return existing;
        AssetRef<Image> decoded = m_images.decodeOnce(key,
                                                      [&absPath]() -> AssetRef<Image> {
                                                          auto img = std::make_shared<Image>();
                                                          if (!img->createFromFile(absPath))
                                                              return {};
                                                          return img;
                                                      });
        return internDecodedImage(key, decoded);
    }

    AssetRef<Image> AssetManager::loadImage(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: image not found '{}'", virtualPath);
            return {};
        }
        return loadImageFile(path);
    }

    AssetRef<Image> AssetManager::loadSolidImage(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const std::string key = ImageCache::solidKey(r, g, b, a);
        if (AssetRef<Image> existing = tryGetInterned<Image>(key, InternLookup::KeepStale))
            return existing;
        AssetRef<Image> decoded = m_images.decodeOnce(key,
                                                      [r, g, b, a]() -> AssetRef<Image> {
                                                          auto img = std::make_shared<Image>();
                                                          if (!img->createSolidColor(r, g, b, a))
                                                              return {};
                                                          return img;
                                                      });
        return internDecodedImage(key, decoded);
    }

#include "Assets/AssetManager_More.inl"

} // namespace Dark
