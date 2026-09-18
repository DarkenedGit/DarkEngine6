#include "Render/GpuUpload.h"
#include "Assets/AssetManager.h"
#include "Assets/Image.h"
#include "Assets/Model.h"
#include "Render/GpuIbl.h"
#include "Render/GpuResourceCache.h"
#include "Render/IblBake.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <memory>

namespace Dark
{

    std::shared_ptr<Texture2D> loadAndUploadTexture(Renderer& renderer, AssetManager& assets, const std::string& virtualPath)
    {
        AssetRef<Image> img = assets.loadImage(virtualPath);
        if (!img || !img->valid())
            return {};
        if (!renderer.gpuResources().ensureTexture(img, Color::TextureUsage::Hud))
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

    AssetRef<Model> registerAndUploadModel(Renderer& renderer, AssetManager& assets, AssetRef<Model> model, const std::string& cacheKey)
    {
        if (!model || !model->valid())
            return {};
        if (model->id == NULL_ASSET && assets.registerAsset(model, cacheKey) == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "registerAndUploadModel: registerAsset failed");
            return {};
        }
        if (!renderer.gpuResources().ensureModel(model))
            return {};
        return model;
    }

    AssetRef<Model> internAndUploadProceduralModel(Renderer& renderer, AssetManager& assets, MeshData mesh, AssetRef<Material> material, const std::string& cacheKey)
    {
        AssetRef<Model> model = internProceduralModel(assets, std::move(mesh), std::move(material), cacheKey);
        if (!model)
            return {};
        if (!renderer.gpuResources().ensureModel(model))
            return {};
        return model;
    }

    namespace
    {
        void bindDummyIbl(Renderer& renderer)
        {
            renderer.setIblSrvs(renderer.iblDummyCubeCpu(), renderer.iblDummyCubeCpu(), renderer.iblDummyLutCpu());
        }
    } // namespace

    bool loadAndBakeIbl(Renderer& renderer, AssetManager& assets, IblSettings& ibl, AssetID& imageId)
    {
        imageId     = NULL_ASSET;
        ibl.enabled = false;

        if (ibl.virtualPath.empty())
        {
            bindDummyIbl(renderer);
            return false;
        }
        if (!renderer.hasGBuffer())
            return false;

        AssetRef<Image> img = assets.loadImage(ibl.virtualPath);
        if (!img || !img->valid() || img->format() != ImageFormat::RGBA32F)
        {
            DE_LOG_ERROR(LogCategory::Render, "Ibl: failed to load '{}' — using ambient", ibl.virtualPath);
            if (ibl.virtualPath != kDefaultIblVirtualPath)
            {
                bindDummyIbl(renderer);
                return false;
            }

            static bool loggedFallback = false;
            if (!loggedFallback)
            {
                DE_LOG_INFO(LogCategory::Render, "Ibl: using in-memory studio gradient fallback");
                loggedFallback = true;
            }
            auto generated = std::make_shared<Image>();
            if (!IblBake::fillStudioGradient(*generated, 128, 64))
            {
                bindDummyIbl(renderer);
                return false;
            }
            img = assets.internImage(generated, std::string(kDefaultIblVirtualPath) + "#generated");
            if (!img || !img->valid())
            {
                bindDummyIbl(renderer);
                return false;
            }
        }

        if (!renderer.gpuResources().ensureIbl(img))
        {
            bindDummyIbl(renderer);
            return false;
        }

        GpuIbl* gpuIbl = renderer.gpuResources().ibl(img->id);
        if (!gpuIbl || !gpuIbl->isReady())
        {
            bindDummyIbl(renderer);
            return false;
        }

        renderer.ensureIblBrdfLut();
        renderer.setIblSrvs(gpuIbl->irradianceCpu(), gpuIbl->prefilterCpu(), renderer.iblBrdfLutCpu());
        imageId     = img->id;
        ibl.enabled = true;
        return true;
    }

    bool iblGpuReady(const GpuResourceCache& gpu, AssetID imageId)
    {
        const GpuIbl* ibl = gpu.ibl(imageId);
        return ibl && ibl->isReady();
    }

} // namespace Dark
