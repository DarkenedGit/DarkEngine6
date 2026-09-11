#include "Assets/AssetManager.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphJson.h"
#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"
#include "Assets/Model.h"
#include "Assets/TextureCache.h"
#include "Core/Log.h"
#include "Render/Renderer.h"

#include <fstream>
#include <sstream>

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
        m_textures.collectUnused();
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

    std::shared_ptr<Texture2D> AssetManager::loadTexture(Renderer& renderer, const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: texture not found '{}'", virtualPath);
            return {};
        }
        return m_textures.loadFile(renderer, path);
    }

    std::shared_ptr<Texture2D> AssetManager::loadSolidTexture(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return m_textures.loadSolid(renderer, r, g, b, a);
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

    AssetRef<Model> AssetManager::loadModel(Renderer& renderer, const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: model not found '{}'", virtualPath);
            return {};
        }
        const std::string key = TextureCache::normalizePath(path);
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
        if (!model->createFromParsed(renderer, *this, cpu, path))
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
        const std::string key     = TextureCache::normalizePath(path);
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
        const std::string key = TextureCache::normalizePath(path);
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
