#include "Render/Material.h"
#include "Assets/AssetManager.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <cstring>

namespace Dark
{

Material::Material()
{
    type = AssetType::Material;
}

bool Material::createFromAlbedoPath(
    Renderer& renderer,
    AssetManager& assets,
    const std::string& virtualAlbedoPath,
    uint8_t fallbackR,
    uint8_t fallbackG,
    uint8_t fallbackB,
    uint8_t fallbackA)
{
    type = AssetType::Material;

    const auto path = assets.resolve(virtualAlbedoPath);
    if (!path.empty() && m_albedo.createFromFile(renderer, path))
    {
        m_baseColor[0] = 1.0f;
        m_baseColor[1] = 1.0f;
        m_baseColor[2] = 1.0f;
        m_baseColor[3] = 1.0f;
        DE_LOG_INFO("Material: albedo '{}'", virtualAlbedoPath);
        return true;
    }

    DE_LOG_WARN(
        "Material: failed to load albedo '{}' — solid fallback ({},{},{},{})",
        virtualAlbedoPath,
        fallbackR,
        fallbackG,
        fallbackB,
        fallbackA);

    if (!m_albedo.createSolidColor(renderer, fallbackR, fallbackG, fallbackB, fallbackA))
    {
        DE_LOG_ERROR("Material: solid fallback create failed");
        return false;
    }

    m_baseColor[0] = 1.0f;
    m_baseColor[1] = 1.0f;
    m_baseColor[2] = 1.0f;
    m_baseColor[3] = 1.0f;
    return true;
}

bool Material::createSolid(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    type = AssetType::Material;
    if (!m_albedo.createSolidColor(renderer, r, g, b, a))
        return false;

    m_baseColor[0] = 1.0f;
    m_baseColor[1] = 1.0f;
    m_baseColor[2] = 1.0f;
    m_baseColor[3] = 1.0f;
    return true;
}

void Material::bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const
{
    if (!cmd || !m_albedo.valid())
        return;
    m_albedo.bind(cmd, albedoSrvRootIndex);
}

void Material::applySurface(MeshFrameConstants& constants) const
{
    std::memcpy(constants.color, m_baseColor, sizeof(m_baseColor));
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
