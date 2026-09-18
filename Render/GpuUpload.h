#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/MeshData.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/Texture2D.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Dark
{

    class Renderer;
    class AssetManager;
    class Model;
    class Material;
    class GpuResourceCache;

    // loadImage + ensureTexture. SpriteSheet / 2D hosts.
    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);
    AssetRef<Model>            loadAndUploadModel(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);
    AssetRef<Model>            loadAndUploadModelFile(Renderer& renderer, AssetManager& assets, const std::filesystem::path& absPath);

    AssetRef<Model> internAndUploadProceduralModel(Renderer& renderer, AssetManager& assets, MeshData mesh, AssetRef<Material> material, const std::string& cacheKey = {});
    AssetRef<Model> registerAndUploadModel(Renderer& renderer, AssetManager& assets, AssetRef<Model> model, const std::string& cacheKey = {});

    // Load authored HDRI, bake split-sum cubes, bind lighting slots 6-8. Empty path → IBL off.
    bool loadAndBakeIbl(Renderer& renderer, AssetManager& assets, IblSettings& ibl, AssetID& imageId);
    bool iblGpuReady(const GpuResourceCache& gpu, AssetID imageId);

} // namespace Dark
