#include "Assets/Material.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"

#include <cstdio>

namespace Dark
{

    Material::Material()
    {
        type = AssetType::Material;
    }

    bool Material::createFromAlbedoImage(AssetRef<Image> albedo, float r, float g, float b, float a)
    {
        type = AssetType::Material;
        if (!albedo || !albedo->valid())
            return false;
        m_albedo       = std::move(albedo);
        m_baseColor[0] = r;
        m_baseColor[1] = g;
        m_baseColor[2] = b;
        m_baseColor[3] = a;
        return true;
    }

    bool Material::createFromAlbedoPath(AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR, uint8_t fallbackG, uint8_t fallbackB, uint8_t fallbackA)
    {
        type             = AssetType::Material;
        AssetRef<Image> img = assets.loadImage(virtualAlbedoPath);
        if (img && img->valid())
        {
            DE_LOG_INFO("Material: albedo '{}' ({}x{})", virtualAlbedoPath, img->width(), img->height());
            return createFromAlbedoImage(std::move(img), 1.0f, 1.0f, 1.0f, 1.0f);
        }

        DE_LOG_WARN("Material: failed to load albedo '{}' — solid fallback ({},{},{},{})", virtualAlbedoPath, fallbackR, fallbackG, fallbackB, fallbackA);
        img = assets.loadSolidImage(fallbackR, fallbackG, fallbackB, fallbackA);
        if (!img || !img->valid())
        {
            DE_LOG_ERROR("Material: solid fallback create failed");
            return false;
        }
        return createFromAlbedoImage(std::move(img), 1.0f, 1.0f, 1.0f, 1.0f);
    }

    bool Material::createSolid(AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return createFromAlbedoImage(assets.loadSolidImage(r, g, b, a), 1.0f, 1.0f, 1.0f, 1.0f);
    }

    bool Material::copyFrom(const Material& src)
    {
        if (!src.albedo() || !src.albedo()->valid())
            return false;
        type           = AssetType::Material;
        m_albedo       = src.albedo();
        m_baseColor[0] = src.m_baseColor[0];
        m_baseColor[1] = src.m_baseColor[1];
        m_baseColor[2] = src.m_baseColor[2];
        m_baseColor[3] = src.m_baseColor[3];
        m_metallic     = src.m_metallic;
        m_roughness    = src.m_roughness;
        m_emissive     = src.m_emissive;
        m_alphaMode    = src.m_alphaMode;
        return true;
    }

    void Material::setMetallicRoughness(float metallic, float roughness)
    {
        m_metallic  = metallic;
        m_roughness = roughness;
    }

    void Material::setBaseColor(float r, float g, float b, float a)
    {
        m_baseColor[0] = r;
        m_baseColor[1] = g;
        m_baseColor[2] = b;
        m_baseColor[3] = a;
    }

    void Material::setEmissive(float emissive)
    {
        m_emissive = emissive < 0.0f ? 0.0f : emissive;
    }

    uint64_t Material::sortKey() const
    {
        return id;
    }

    std::string materialRecipeKey(const Material& m)
    {
        char buf[256];
        const float* c   = m.baseColor();
        const AssetID aid = (m.albedo() && m.albedo()->id != NULL_ASSET) ? m.albedo()->id : NULL_ASSET;
        std::snprintf(buf, sizeof(buf), "m:%llu:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%u",
                      static_cast<unsigned long long>(aid),
                      static_cast<double>(c[0]),
                      static_cast<double>(c[1]),
                      static_cast<double>(c[2]),
                      static_cast<double>(c[3]),
                      static_cast<double>(m.metallic()),
                      static_cast<double>(m.roughness()),
                      static_cast<double>(m.emissive()),
                      static_cast<unsigned>(m.alphaMode()));
        return buf;
    }

} // namespace Dark
