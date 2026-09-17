#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/MeshData.h"
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

    // loadImage + ensureTexture. SpriteSheet / 2D hosts.
    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);
    AssetRef<Model>            loadAndUploadModel(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);
    AssetRef<Model>            loadAndUploadModelFile(Renderer& renderer, AssetManager& assets, const std::filesystem::path& absPath);

    AssetRef<Model> internAndUploadProceduralModel(Renderer& renderer, AssetManager& assets, MeshData mesh, AssetRef<Material> material, const std::string& cacheKey = {});
    AssetRef<Model> registerAndUploadModel(Renderer& renderer, AssetManager& assets, AssetRef<Model> model, const std::string& cacheKey = {});

} // namespace Dark
