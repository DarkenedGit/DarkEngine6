#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Image.h"

#include <cstdint>
#include <string>

namespace Dark
{

    class AssetManager;

    enum class MaterialAlphaMode : uint8_t
    {
        Opaque = 0,
        Mask,
        Blend,
    };

    // CPU PBR recipe. No D3D12. GPU heaps live on GpuResourceCache.
    class Material : public Asset
    {
    public:
        Material();

        Material(Material&&) noexcept            = default;
        Material& operator=(Material&&) noexcept = default;

        Material(const Material&)            = delete;
        Material& operator=(const Material&) = delete;

        bool createFromAlbedoImage(AssetRef<Image> albedo, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
        bool createFromAlbedoPath(AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR = 64, uint8_t fallbackG = 166, uint8_t fallbackB = 242,
                                  uint8_t fallbackA = 255);
        bool createSolid(AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        bool copyFrom(const Material& src);

        void  setMetallicRoughness(float metallic, float roughness);
        float metallic() const { return m_metallic; }
        float roughness() const { return m_roughness; }

        void         setBaseColor(float r, float g, float b, float a = 1.0f);
        const float* baseColor() const { return m_baseColor; }

        void              setAlphaMode(MaterialAlphaMode mode) { m_alphaMode = mode; }
        MaterialAlphaMode alphaMode() const { return m_alphaMode; }

        // Self-illumination. Meshes write this into G-buffer RT0.a; particles scale HDR rgb.
        void  setEmissive(float emissive);
        float emissive() const { return m_emissive; }

        bool isValid() const { return m_albedo && m_albedo->valid(); }
        uint64_t sortKey() const;

        const AssetRef<Image>& albedo() const { return m_albedo; }

    private:
        AssetRef<Image>   m_albedo;
        float             m_baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        float             m_metallic  = 0.0f;
        float             m_roughness = 1.0f;
        float             m_emissive  = 0.0f;
        MaterialAlphaMode m_alphaMode = MaterialAlphaMode::Opaque;
    };

    // "m:{albedoId}:{r}:{g}:{b}:{a}:{metallic}:{roughness}:{emissive}:{alphaMode}"
    std::string materialRecipeKey(const Material& m);

} // namespace Dark
