#include "Render/GpuUpload.h"
#include "Assets/AssetManager.h"
#include "Assets/Image.h"
#include "Assets/Model.h"
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

    AssetRef<Model> loadAndUploadModel(Renderer& renderer, AssetManager& assets, const std::string& virtualPath)
    {
        AssetRef<Model> model = assets.loadModel(virtualPath);
        if (!model || !model->valid())
            return {};
        if (!renderer.gpuResources().ensureModel(model))
            return {};
        return model;
    }

    AssetRef<Model> loadAndUploadModelFile(Renderer& renderer, AssetManager& assets, const std::filesystem::path& absPath)
    {
        AssetRef<Model> model = assets.loadModelFile(absPath);
        if (!model || !model->valid())
            return {};
        if (!renderer.gpuResources().ensureModel(model))
            return {};
        return model;
    }

} // namespace Dark
