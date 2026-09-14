#pragma once
#include "Assets/AssetHandle.h"
#include "Assets/ImageCache.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dark
{

    namespace Audio
    {
        class SoundClip;
    }

    class AssetManager
    {
    public:
        AssetManager();
        ~AssetManager();

        // Register a root content directory
        void mountDirectory(const std::filesystem::path& dir);

        // Take ownership of a runtime-created asset; sets asset->id. Returns id or NULL_ASSET.
        // Optional cacheKey (usually TextureCache::normalizePath) enables path-based reuse like loadModel.
        AssetID registerAsset(AssetRef<Asset> asset, const std::string& cacheKey = {});

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

        size_t assetCount() const;
        size_t pathMappingCount() const;

        ImageCache&       imageCache() { return m_images; }
        const ImageCache& imageCache() const { return m_images; }
        ImageCache&       textureCache() { return m_images; }
        const ImageCache& textureCache() const { return m_images; }

        AssetRef<class Image> loadImage(const std::string& virtualPath);
        AssetRef<class Image> loadImageFile(const std::filesystem::path& absPath);
        AssetRef<class Image> loadSolidImage(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        AssetRef<class Image> loadMemoryImage(const std::string& key, const void* bytes, size_t byteCount);

        // Cached glTF / GLB. Same resolved path returns the same Model instance.
        // Interns Model + AnimationSet only. Does not load *.anim.json.
        AssetRef<class Model> loadModel(const std::string& virtualPath);
        AssetRef<class Model> loadModelFile(const std::filesystem::path& absPath);

        // CPU-only. Looks up path + "#anims", or parses the glTF.
        // Static meshes (no skin, no clips) return an empty ref (Trace, not Error).
        AssetRef<class AnimationSet> loadAnimationSet(const std::string& virtualPath);

        // Assigns id if needed. Idempotent. Empty cacheKey = one-off; non-empty reuses m_pathToID.
        AssetRef<class Material> internMaterial(AssetRef<class Material> mat, const std::string& cacheKey = {});

        // Interns *.anim.json. Resolves the JSON "model" field via loadAnimationSet.
        AssetRef<class AnimGraphDef> loadAnimGraph(const std::string& virtualPath);

        // stem + ".anim.json" if that file exists; empty ref if missing (not an error).
        AssetRef<class AnimGraphDef> tryLoadAnimGraphForModel(const std::string& gltfVirtualPath);

        AssetRef<class Audio::SoundClip> loadAudio(const std::string& virtualPath);
        AssetRef<class Audio::SoundClip> internSoundClip(AssetRef<class Audio::SoundClip> clip, const std::string& cacheKey = {});

    private:
        mutable std::mutex                                      m_mutex;
        std::vector<std::filesystem::path>                      m_mounts;
        std::unordered_map<std::string, AssetID>                m_pathToID;
        std::unordered_map<AssetID, std::shared_ptr<Asset>>     m_assets;
        AssetID                                                 m_nextID = 1;
        ImageCache                                              m_images;

        AssetRef<class Image> internDecodedImage(const std::string& key, AssetRef<class Image> img);

        AssetID allocID()
        {
            return m_nextID++;
        }

        // Caller must hold m_mutex.
        void erasePathEntriesLocked(AssetID id);

        // Caller must hold m_mutex. Interns clips/skeleton names at modelKey + "#anims".
        AssetRef<class AnimationSet> internAnimationSetLocked(const std::string& modelKey, const struct GltfCpuModel& cpu);
    };

} // namespace Dark
