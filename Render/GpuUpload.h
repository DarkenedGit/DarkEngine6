#pragma once

#include "Assets/AssetHandle.h"
#include "Render/Texture2D.h"

#include <memory>
#include <string>

namespace Dark
{

    class Renderer;
    class AssetManager;

    // loadImage + ensureTexture. SpriteSheet / 2D hosts.
    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath);

} // namespace Dark
