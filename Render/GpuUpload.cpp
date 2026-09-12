#include "Render/GpuUpload.h"
#include "Assets/AssetManager.h"
#include "Assets/Image.h"
#include "Render/GpuResourceCache.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

namespace Dark
{

    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath)
    {
        AssetRef<Image> img = assets.loadImage(virtualPath);
        if (!img || !img->valid())
            return {};
        if (!renderer.gpuResources().ensureTexture(img))
            return {};
        return renderer.gpuResources().texture(img->id);
    }

} // namespace Dark
