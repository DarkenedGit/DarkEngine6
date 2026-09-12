#pragma once

#include "Assets/AssetHandle.h"
#include "Render/Texture2D.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Dark
{

    class Renderer;
    class AssetManager;

    // CPU PBR recipe. GPU heaps live on GpuResourceCache / GpuMaterial.
    class Material : public Asset
    {
    public:
        Material();

        Material(Material&&) noexcept            = default;
        Material& operator=(Material&&) noexcept = default;

        Material(const Material&)            = delete;
        Material& operator=(const Material&) = delete;

        // Load albedo from a virtual content path (e.g. "textures/foo.png").
        // On failure, uses a solid fallback color so the material stays drawable.
        bool createFromAlbedoPath(Renderer& renderer, AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR = 64, uint8_t fallbackG = 166, uint8_t fallbackB = 242,
                                  uint8_t fallbackA = 255);

        // Solid-color material (1x1 albedo).
        bool createSolid(Renderer& renderer, AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

        // Already-cached albedo (glTF textures, interned solids).
        bool createFromAlbedoTexture(Renderer& renderer, std::shared_ptr<Texture2D> albedo, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

        void setMetallicRoughness(float metallic, float roughness);
        float metallic() const { return m_metallic; }
        float roughness() const { return m_roughness; }

        void setBaseColor(float r, float g, float b, float a = 1.0f);

        bool isValid() const
        {
            return m_albedo && m_albedo->valid();
        }
        uint64_t   sortKey() const;
        Texture2D& albedo()
        {
            return *m_albedo;
        }
        const Texture2D& albedo() const
        {
            return *m_albedo;
        }

        const std::shared_ptr<Texture2D>& albedoPtr() const
        {
            return m_albedo;
        }

        const float* baseColor() const
        {
            return m_baseColor;
        }

    private:
        std::shared_ptr<Texture2D> m_albedo;
        float                      m_baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        float                      m_metallic  = 0.0f;
        float                      m_roughness = 1.0f;
    };

} // namespace Dark
