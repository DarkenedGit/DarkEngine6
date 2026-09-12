#include "Assets/AssetManager.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Assets/Image.h"
#include "Assets/Model.h"
#include "Assets/Material.h"
#include "Core/Log.h"

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
            const auto pit = m_pathToID.find(cacheKey);
            if (pit != m_pathToID.end())
            {
                const auto ait = m_assets.find(pit->second);
                if (ait != m_assets.end())
                {
                    if (AssetRef<Material> existing = std::dynamic_pointer_cast<Material>(ait->second))
                        return existing;
                }
                m_pathToID.erase(pit);
            }
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
        const auto pit = m_pathToID.find(key);
        if (pit != m_pathToID.end())
        {
            const auto ait = m_assets.find(pit->second);
            if (ait != m_assets.end())
            {
                if (AssetRef<Image> existing = std::dynamic_pointer_cast<Image>(ait->second))
                    return existing;
            }
            else
                m_pathToID.erase(pit);
        }
        const AssetID id = allocID();
        img->id          = id;
        m_assets[id]     = img;
        m_pathToID[key]  = id;
        return img;
    }

    AssetRef<Image> AssetManager::loadImageFile(const std::filesystem::path& absPath)
    {
        const std::string key = ImageCache::fileKey(absPath);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto pit = m_pathToID.find(key);
            if (pit != m_pathToID.end())
            {
                const auto ait = m_assets.find(pit->second);
                if (ait != m_assets.end())
                {
                    if (AssetRef<Image> existing = std::dynamic_pointer_cast<Image>(ait->second))
                        return existing;
                }
            }
        }
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
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto pit = m_pathToID.find(key);
            if (pit != m_pathToID.end())
            {
                const auto ait = m_assets.find(pit->second);
                if (ait != m_assets.end())
                {
                    if (AssetRef<Image> existing = std::dynamic_pointer_cast<Image>(ait->second))
                        return existing;
                }
            }
        }
        AssetRef<Image> decoded = m_images.decodeOnce(key,
                                                      [r, g, b, a]() -> AssetRef<Image> {
                                                          auto img = std::make_shared<Image>();
                                                          if (!img->createSolidColor(r, g, b, a))
                                                              return {};
                                                          return img;
                                                      });
        return internDecodedImage(key, decoded);
    }

    AssetRef<Image> AssetManager::loadMemoryImage(const std::string& key, const void* bytes, size_t byteCount)
    {
        if (key.empty() || !bytes || byteCount == 0)
            return {};
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto pit = m_pathToID.find(key);
            if (pit != m_pathToID.end())
            {
                const auto ait = m_assets.find(pit->second);
                if (ait != m_assets.end())
                {
                    if (AssetRef<Image> existing = std::dynamic_pointer_cast<Image>(ait->second))
                        return existing;
                }
            }
        }
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + byteCount);
        AssetRef<Image> decoded = m_images.decodeOnce(key,
                                                      [copy]() -> AssetRef<Image> {
                                                          auto img = std::make_shared<Image>();
                                                          if (!img->createFromMemory(copy.data(), copy.size()))
                                                              return {};
                                                          return img;
                                                      });
        return internDecodedImage(key, decoded);
    }

    AssetRef<AnimationSet> AssetManager::internAnimationSetLocked(const std::string& modelKey, const GltfCpuModel& cpu)
    {
        const std::string animKey = modelKey + "#anims";
        const auto it = m_pathToID.find(animKey);
        if (it != m_pathToID.end())
        {
            const auto asset = m_assets.find(it->second);
            if (asset != m_assets.end())
            {
                if (auto existing = std::dynamic_pointer_cast<AnimationSet>(asset->second))
                    return existing;
            }
            else
            {
                m_pathToID.erase(it);
            }
        }

        if (cpu.skeleton.joints.empty() && cpu.clips.empty())
            return {};

        auto set = std::make_shared<AnimationSet>();
        set->setFromParsed(cpu);
        const AssetID id = allocID();
        set->id          = id;
        m_assets[id]     = set;
        m_pathToID[animKey] = id;
        DE_LOG_INFO("AssetManager: cached AnimationSet '{}' id={} clips={}", animKey, id, set->clipCount());
        return set;
    }

    AssetRef<Model> AssetManager::loadModel(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: model not found '{}'", virtualPath);
            return {};
        }
        const std::string key = ImageCache::normalizePath(path);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_pathToID.find(key);
            if (it != m_pathToID.end())
            {
                const auto asset = m_assets.find(it->second);
                if (asset != m_assets.end())
                {
                    if (auto existing = std::dynamic_pointer_cast<Model>(asset->second))
                        return existing;
                }
                else
                {
                    m_pathToID.erase(it);
                }
            }
        }

        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return {};

        auto model = std::make_shared<Model>();
        if (!model->createFromParsed(*this, cpu, path))
            return {};

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_pathToID.find(key);
        if (it != m_pathToID.end())
        {
            const auto asset = m_assets.find(it->second);
            if (asset != m_assets.end())
            {
                if (auto existing = std::dynamic_pointer_cast<Model>(asset->second))
                    return existing;
            }
            else
            {
                m_pathToID.erase(it);
            }
        }
        const AssetID id = allocID();
        model->id        = id;
        m_assets[id]     = model;
        m_pathToID[key]  = id;
        model->setAnimationSet(internAnimationSetLocked(key, cpu));
        DE_LOG_INFO("AssetManager: cached model '{}' id={}", virtualPath, id);
        return model;
    }

    AssetRef<AnimationSet> AssetManager::loadAnimationSet(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: animation set source not found '{}'", virtualPath);
            return {};
        }
        const std::string key     = ImageCache::normalizePath(path);
        const std::string animKey = key + "#anims";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_pathToID.find(animKey);
            if (it != m_pathToID.end())
            {
                const auto asset = m_assets.find(it->second);
                if (asset != m_assets.end())
                {
                    if (auto existing = std::dynamic_pointer_cast<AnimationSet>(asset->second))
                        return existing;
                }
                else
                {
                    m_pathToID.erase(it);
                }
            }
        }

        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return {};
        if (cpu.skeleton.joints.empty() && cpu.clips.empty())
        {
            DE_LOG_TRACE("AssetManager: no skeleton or clips in '{}'", virtualPath);
            return {};
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        AssetRef<AnimationSet> set = internAnimationSetLocked(key, cpu);
        const auto modelIt = m_pathToID.find(key);
        if (modelIt != m_pathToID.end())
        {
            const auto asset = m_assets.find(modelIt->second);
            if (asset != m_assets.end())
            {
                if (auto model = std::dynamic_pointer_cast<Model>(asset->second))
                {
                    if (!model->animationSet())
                        model->setAnimationSet(set);
                }
            }
        }
        return set;
    }

    AssetRef<AnimGraphDef> AssetManager::loadAnimGraph(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: anim graph not found '{}'", virtualPath);
            return {};
        }
        const std::string key = ImageCache::normalizePath(path);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_pathToID.find(key);
            if (it != m_pathToID.end())
            {
                const auto asset = m_assets.find(it->second);
                if (asset != m_assets.end())
                {
                    if (auto existing = std::dynamic_pointer_cast<AnimGraphDef>(asset->second))
                        return existing;
                }
                else
                {
                    m_pathToID.erase(it);
                }
            }
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("AssetManager: cannot open anim graph '{}'", path.string());
            return {};
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();

        std::string modelPath;
        if (!peekAnimGraphModelPath(text.c_str(), modelPath))
        {
            DE_LOG_ERROR("AssetManager: anim graph '{}' missing model path", virtualPath);
            return {};
        }

        AssetRef<AnimationSet> set = loadAnimationSet(modelPath);
        if (!set)
        {
            DE_LOG_ERROR("AssetManager: anim graph '{}' could not load model '{}'", virtualPath, modelPath);
            return {};
        }

        auto graph = std::make_shared<AnimGraphDef>();
        if (!parseAnimGraphJson(text.c_str(), *set, *graph))
            return {};
        graph->animSet = set;

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_pathToID.find(key);
        if (it != m_pathToID.end())
        {
            const auto asset = m_assets.find(it->second);
            if (asset != m_assets.end())
            {
                if (auto existing = std::dynamic_pointer_cast<AnimGraphDef>(asset->second))
                    return existing;
            }
            else
            {
                m_pathToID.erase(it);
            }
        }
        const AssetID id = allocID();
        graph->id        = id;
        m_assets[id]     = graph;
        m_pathToID[key]  = id;
        DE_LOG_INFO("AssetManager: cached AnimGraph '{}' id={}", virtualPath, id);
        return graph;
    }

    AssetRef<AnimGraphDef> AssetManager::tryLoadAnimGraphForModel(const std::string& gltfVirtualPath)
    {
        std::filesystem::path vp(gltfVirtualPath);
        vp.replace_extension(".anim.json");
        const std::string jsonPath = vp.generic_string();
        if (resolve(jsonPath).empty())
            return {};
        return loadAnimGraph(jsonPath);
    }

} // namespace Dark
