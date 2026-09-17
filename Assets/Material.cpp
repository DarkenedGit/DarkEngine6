#include "Assets/Material.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Math/Color.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace Dark
{

    namespace
    {
        AssetID recipeImageId(const AssetRef<Image>& img)
        {
            return (img && img->id != NULL_ASSET) ? img->id : NULL_ASSET;
        }

        bool packOrmSourceOk(const Image* img)
        {
            if (!img)
                return true;
            if (img->format() != ImageFormat::RGBA8 || img->width() == 0 || img->height() == 0 || !img->pixels() || img->rowPitchBytes() < img->width() * 4u)
            {
                DE_LOG_ERROR("packOrmImage: expected RGBA8 image with width>0, height>0, pixels, and rowPitchBytes >= width*4");
                return false;
            }
            return true;
        }

        const uint8_t* packOrmPixel(const Image& img, uint32_t x, uint32_t y)
        {
            return img.pixels() + static_cast<size_t>(y) * img.rowPitchBytes() + static_cast<size_t>(x) * 4u;
        }
    } // namespace

    Material::Material()
    {
        type = AssetType::Material;
    }

    bool Material::createFromAlbedoImage(AssetRef<Image> albedo, float r, float g, float b, float a)
    {
        type = AssetType::Material;
        if (!albedo || !albedo->valid())
            return false;
        m_albedo = std::move(albedo);
        m_normal.reset();
        m_orm.reset();
        m_emissiveImage.reset();
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
        AssetRef<Image> img = assets.loadSolidImage(r, g, b, a);
        if (!img || !img->valid())
            return false;
        img->setColorSpace(Color::ColorSpace::sRGB);
        return createFromAlbedoImage(std::move(img), 1.0f, 1.0f, 1.0f, 1.0f);
    }

    bool Material::copyFrom(const Material& src)
    {
        if (!src.albedo() || !src.albedo()->valid())
            return false;
        type              = AssetType::Material;
        m_albedo          = src.albedo();
        m_normal          = src.normalImage();
        m_orm             = src.ormImage();
        m_emissiveImage   = src.emissiveImage();
        m_baseColor[0]    = src.m_baseColor[0];
        m_baseColor[1]    = src.m_baseColor[1];
        m_baseColor[2]    = src.m_baseColor[2];
        m_baseColor[3]    = src.m_baseColor[3];
        m_emissiveColor[0] = src.m_emissiveColor[0];
        m_emissiveColor[1] = src.m_emissiveColor[1];
        m_emissiveColor[2] = src.m_emissiveColor[2];
        m_metallic        = src.m_metallic;
        m_roughness       = src.m_roughness;
        m_emissive        = src.m_emissive;
        m_ao              = src.m_ao;
        m_normalScale     = src.m_normalScale;
        m_alphaCutoff     = src.m_alphaCutoff;
        m_alphaMode       = src.m_alphaMode;
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

    void Material::setBaseColorFromSrgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        float rgb[3]{};
        Color::srgb8ToLinear3(r, g, b, rgb);
        setBaseColor(rgb[0], rgb[1], rgb[2], static_cast<float>(a) / 255.0f);
    }

    void Material::setEmissive(float emissive)
    {
        m_emissive = emissive < 0.0f ? 0.0f : emissive;
    }

    void Material::setNormalImage(AssetRef<Image> image)
    {
        m_normal = std::move(image);
    }

    void Material::setOrmImage(AssetRef<Image> image)
    {
        m_orm = std::move(image);
    }

    void Material::setEmissiveImage(AssetRef<Image> image)
    {
        m_emissiveImage = std::move(image);
    }

    void Material::setAo(float ao)
    {
        m_ao = ao < 0.0f ? 0.0f : (ao > 1.0f ? 1.0f : ao);
    }

    void Material::setNormalScale(float s)
    {
        m_normalScale = s;
    }

    void Material::setAlphaCutoff(float c)
    {
        m_alphaCutoff = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    }

    void Material::setEmissiveColor(float r, float g, float b)
    {
        m_emissiveColor[0] = r;
        m_emissiveColor[1] = g;
        m_emissiveColor[2] = b;
    }

    uint64_t Material::sortKey() const
    {
        return id;
    }

    std::string materialRecipeKey(const Material& m)
    {
        char buf[512];
        const float* c  = m.baseColor();
        const float* e  = m.emissiveColor();
        std::snprintf(buf, sizeof(buf), "m:%llu:%llu:%llu:%llu:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%u:%.9g:%.9g:%.9g",
                      static_cast<unsigned long long>(recipeImageId(m.albedo())),
                      static_cast<unsigned long long>(recipeImageId(m.normalImage())),
                      static_cast<unsigned long long>(recipeImageId(m.ormImage())),
                      static_cast<unsigned long long>(recipeImageId(m.emissiveImage())),
                      static_cast<double>(c[0]),
                      static_cast<double>(c[1]),
                      static_cast<double>(c[2]),
                      static_cast<double>(c[3]),
                      static_cast<double>(m.metallic()),
                      static_cast<double>(m.roughness()),
                      static_cast<double>(m.emissive()),
                      static_cast<double>(m.ao()),
                      static_cast<double>(m.normalScale()),
                      static_cast<double>(m.alphaCutoff()),
                      static_cast<unsigned>(m.alphaMode()),
                      static_cast<double>(e[0]),
                      static_cast<double>(e[1]),
                      static_cast<double>(e[2]));
        return buf;
    }

    bool packOrmImage(const Image* occlusion, const Image* metallicRoughness, Image& out)
    {
        if (!occlusion && !metallicRoughness)
            return false;
        if (!packOrmSourceOk(occlusion) || !packOrmSourceOk(metallicRoughness))
            return false;

        if (occlusion && metallicRoughness && occlusion == metallicRoughness)
        {
            if (!out.createFromRGBA(occlusion->pixels(), occlusion->width(), occlusion->height(), occlusion->rowPitchBytes()))
                return false;
            out.setColorSpace(Color::ColorSpace::Linear);
            return true;
        }

        const Image*   sizeSrc = metallicRoughness ? metallicRoughness : occlusion;
        const uint32_t w       = sizeSrc->width();
        const uint32_t h       = sizeSrc->height();
        if (occlusion && metallicRoughness && (occlusion->width() != w || occlusion->height() != h))
        {
            static bool s_sizeMismatchWarned = false;
            if (!s_sizeMismatchWarned)
            {
                DE_LOG_WARN("packOrmImage: occlusion {}x{} resampled nearest to metallicRoughness {}x{}", occlusion->width(), occlusion->height(), w, h);
                s_sizeMismatchWarned = true;
            }
        }

        std::vector<uint8_t> packed(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                uint8_t ao    = 255;
                uint8_t rough = 255;
                uint8_t metal = 255;
                if (occlusion)
                {
                    uint32_t ox = x;
                    uint32_t oy = y;
                    if (occlusion->width() != w || occlusion->height() != h)
                    {
                        ox = static_cast<uint32_t>((static_cast<uint64_t>(x) * occlusion->width()) / w);
                        oy = static_cast<uint32_t>((static_cast<uint64_t>(y) * occlusion->height()) / h);
                        if (ox >= occlusion->width())
                            ox = occlusion->width() - 1;
                        if (oy >= occlusion->height())
                            oy = occlusion->height() - 1;
                    }
                    ao = packOrmPixel(*occlusion, ox, oy)[0];
                }
                if (metallicRoughness)
                {
                    const uint8_t* mr = packOrmPixel(*metallicRoughness, x, y);
                    rough             = mr[1];
                    metal             = mr[2];
                }
                const size_t i = (static_cast<size_t>(y) * w + x) * 4u;
                packed[i + 0]  = ao;
                packed[i + 1]  = rough;
                packed[i + 2]  = metal;
                packed[i + 3]  = 255;
            }
        }

        if (!out.createFromRGBA(packed.data(), w, h, w * 4u))
            return false;
        out.setColorSpace(Color::ColorSpace::Linear);
        return true;
    }

    AssetRef<Material> internSolidMaterial(AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::string& cacheKey)
    {
        auto mat = std::make_shared<Material>();
        if (!mat->createSolid(assets, r, g, b, a))
            return {};
        return assets.internMaterial(mat, cacheKey);
    }

} // namespace Dark
