#pragma once

#include "Assets/AssetHandle.h"
#include "Render/Texture2D.h"
#include "Render/MeshPipeline.h"

#include <cstdint>
#include <string>

namespace Dark
{

class Renderer;
class AssetManager;

// Shared surface recipe: albedo texture + tint params.
// Does not own world transforms or lights — those stay per-draw / per-frame.
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
    bool createFromAlbedoPath(
        Renderer& renderer,
        AssetManager& assets,
        const std::string& virtualAlbedoPath,
        uint8_t fallbackR = 64,
        uint8_t fallbackG = 166,
        uint8_t fallbackB = 242,
        uint8_t fallbackA = 255);

    // Solid-color material (1x1 albedo).
    bool createSolid(
        Renderer& renderer,
        uint8_t r,
        uint8_t g,
        uint8_t b,
        uint8_t a = 255);

    // Bind albedo SRV (descriptor heap + root table).
    void bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const;

    // Write surface tint into frame constants (color slot used by BasicMesh).
    void applySurface(MeshFrameConstants& constants) const;

    void setBaseColor(float r, float g, float b, float a = 1.0f);

    bool     isValid()  const { return m_albedo.valid(); }
    uint64_t sortKey()  const;
    Texture2D&       albedo()       { return m_albedo; }
    const Texture2D& albedo() const { return m_albedo; }

    const float* baseColor() const { return m_baseColor; }

private:
    Texture2D m_albedo;
    float     m_baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace Dark
