#include "Render/Material.h"
#include "Assets/AssetManager.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

namespace Dark
{

    Material::Material()
    {
        type = AssetType::Material;
    }

    bool Material::packSrvHeap(ID3D12Device* device)
    {
        m_gpu.reset();
        if (!device || !m_albedo || !m_albedo->valid())
            return false;
        m_gpu = std::make_unique<GpuMaterial>();
        if (!m_gpu->pack(device, *m_albedo))
        {
            m_gpu.reset();
            return false;
        }
        return true;
    }

    void Material::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        if (m_gpu)
            m_gpu->setShadowSrv(device, shadowCpu);
    }

    bool Material::createFromAlbedoPath(Renderer& renderer, AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR, uint8_t fallbackG, uint8_t fallbackB, uint8_t fallbackA)
    {
        type = AssetType::Material;

        m_albedo = assets.loadTexture(renderer, virtualAlbedoPath);
        if (m_albedo && m_albedo->valid())
        {
            m_baseColor[0] = 1.0f;
            m_baseColor[1] = 1.0f;
            m_baseColor[2] = 1.0f;
            m_baseColor[3] = 1.0f;
            if (!packSrvHeap(renderer.device()))
            {
                DE_LOG_ERROR(LogCategory::Render, "Material: failed to pack SRV heap for '{}'", virtualAlbedoPath);
                return false;
            }
            DE_LOG_INFO(LogCategory::Render, "Material: albedo '{}' (cached {}x{})", virtualAlbedoPath, m_albedo->width(), m_albedo->height());
            return true;
        }

        DE_LOG_WARN(LogCategory::Render, "Material: failed to load albedo '{}' — solid fallback ({},{},{},{})", virtualAlbedoPath, fallbackR, fallbackG, fallbackB, fallbackA);

        m_albedo = assets.loadSolidTexture(renderer, fallbackR, fallbackG, fallbackB, fallbackA);
        if (!m_albedo || !m_albedo->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "Material: solid fallback create failed");
            return false;
        }

        m_baseColor[0] = 1.0f;
        m_baseColor[1] = 1.0f;
        m_baseColor[2] = 1.0f;
        m_baseColor[3] = 1.0f;
        if (!packSrvHeap(renderer.device()))
        {
            DE_LOG_ERROR(LogCategory::Render, "Material: failed to pack SRV heap for solid fallback");
            return false;
        }
        return true;
    }

    bool Material::createSolid(Renderer& renderer, AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        type = AssetType::Material;
        m_albedo = assets.textureCache().loadSolid(renderer, r, g, b, a);
        if (!m_albedo || !m_albedo->valid())
            return false;

        m_baseColor[0] = 1.0f;
        m_baseColor[1] = 1.0f;
        m_baseColor[2] = 1.0f;
        m_baseColor[3] = 1.0f;
        return packSrvHeap(renderer.device());
    }

    bool Material::createFromAlbedoTexture(Renderer& renderer, std::shared_ptr<Texture2D> albedo, float r, float g, float b, float a)
    {
        type = AssetType::Material;
        if (!albedo || !albedo->valid())
            return false;
        m_albedo       = std::move(albedo);
        m_baseColor[0] = r;
        m_baseColor[1] = g;
        m_baseColor[2] = b;
        m_baseColor[3] = a;
        return packSrvHeap(renderer.device());
    }

    void Material::setMetallicRoughness(float metallic, float roughness)
    {
        m_metallic  = metallic;
        m_roughness = roughness;
    }

    void Material::bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const
    {
        if (m_gpu)
            m_gpu->bind(cmd, albedoSrvRootIndex);
    }

    void Material::setBaseColor(float r, float g, float b, float a)
    {
        m_baseColor[0] = r;
        m_baseColor[1] = g;
        m_baseColor[2] = b;
        m_baseColor[3] = a;
    }

    uint64_t Material::sortKey() const
    {
        // Low bits: asset id. High bits reserved for future pipeline / blend mode.
        return id;
    }

} // namespace Dark
