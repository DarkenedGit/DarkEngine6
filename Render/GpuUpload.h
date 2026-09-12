#pragma once

#include "Assets/AssetHandle.h"
#include "Render/Texture2D.h"

#include <memory>
#include <string>

namespace Dark
{

    class Renderer;
    class AssetManager;
    class Model;

    // loadImage + ensureTexture. SpriteSheet / 2D hosts.
    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);
    AssetRef<Model>            loadAndUploadModel(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);

} // namespace Dark
