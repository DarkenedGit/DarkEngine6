#pragma once
#include "Assets/AssetHandle.h"
#include "Assets/TextureCache.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dark
{

    class Renderer;

    class AssetManager
    {
    public:
        AssetManager();
        ~AssetManager();

        // Register a root content directory
        void mountDirectory(const std::filesystem::path& dir);

        // Take ownership of a runtime-created asset; sets asset->id. Returns id or NULL_ASSET.
        AssetID registerAsset(AssetRef<Asset> asset);

        // Lookup by id (shared ownership with the manager).
        AssetRef<Asset> get(AssetID id) const;

        template <typename T> AssetRef<T> getAs(AssetID id) const
        {
            return std::dynamic_pointer_cast<T>(get(id));
        }

        // Unload a specific asset
        void unload(AssetID id);

        // Release all unreferenced assets (only manager holds a ref)
        void collectGarbage();

        // Resolve virtual path → absolute path
        std::filesystem::path resolve(const std::string& virtualPath) const;

        TextureCache&       textureCache() { return m_textures; }
        const TextureCache& textureCache() const { return m_textures; }

        // Interned GPU textures. Same resolved file / solid color is loaded once.
        std::shared_ptr<Texture2D> loadTexture(Renderer& renderer, const std::string& virtualPath);
        std::shared_ptr<Texture2D> loadSolidTexture(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    private:
        mutable std::mutex                                      m_mutex;
        std::vector<std::filesystem::path>                      m_mounts;
        std::unordered_map<std::string, AssetID>                m_pathToID;
        std::unordered_map<AssetID, std::shared_ptr<Asset>>     m_assets;
        AssetID                                                 m_nextID = 1;
        TextureCache                                            m_textures;

        AssetID allocID()
        {
            return m_nextID++;
        }
    };

} // namespace Dark
