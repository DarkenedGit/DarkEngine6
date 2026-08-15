#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Render/Renderer.h"

namespace Dark
{

    AssetManager::AssetManager()  = default;
    AssetManager::~AssetManager() = default;

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

    AssetID AssetManager::registerAsset(AssetRef<Asset> asset)
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
    }

    void AssetManager::collectGarbage()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::erase_if(m_assets,
                          [](const auto& kv)
                          {
                              return kv.second.use_count() == 1; // only manager holds it
                          });
            DE_LOG_TRACE("AssetManager: GC pass complete ({} assets remaining)", m_assets.size());
        }
        m_textures.collectUnused();
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

} // namespace Dark
