#include "Particles/ParticleMaterials.h"

#include "Assets/AssetManager.h"
#include "Assets/Image.h"
#include "Core/Log.h"

#include <memory>

namespace Dark
{

    AssetRef<Material> internParticleSpriteMaterial(AssetManager& assets, bool ribbon)
    {
        const char* imgKey = ribbon ? "proc:soft_streak:64" : "proc:soft_circle:64";
        const char* matKey = ribbon ? kParticleRibbonMaterialKey : kParticleBillboardMaterialKey;

        auto img = std::make_shared<Image>();
        const bool ok = ribbon ? img->createSoftStreak(64) : img->createSoftCircle(64);
        if (!ok)
        {
            DE_LOG_ERROR("internParticleSpriteMaterial: sprite image failed");
            return {};
        }
        img = assets.internImage(std::move(img), imgKey);
        if (!img)
            return {};

        auto mat = std::make_shared<Material>();
        if (!mat->createFromAlbedoImage(img))
        {
            DE_LOG_ERROR("internParticleSpriteMaterial: material create failed");
            return {};
        }
        mat->setEmissive(1.0f);
        return assets.internMaterial(std::move(mat), matKey);
    }

} // namespace Dark
