#pragma once

#include "Assets/AssetHandle.h"
#include "Render/Texture2D.h"
#include "Render/MeshPipeline.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl/client.h>

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
        bool createFromAlbedoPath(Renderer& renderer, AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR = 64, uint8_t fallbackG = 166, uint8_t fallbackB = 242,
                                  uint8_t fallbackA = 255);

        // Solid-color material (1x1 albedo).
        bool createSolid(Renderer& renderer, AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

        // Bind albedo + shadow SRV table (2-slot shader-visible heap).
        void bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const;

        // Copy the CSM array into heap slot 1 (call after create, and after pack).
        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        // Write surface tint into frame constants (color slot used by BasicMesh).
        void applySurface(MeshFrameConstants& constants) const;

        void setBaseColor(float r, float g, float b, float a = 1.0f);

        bool isValid() const
        {
            return m_albedo && m_albedo->valid() && m_srvHeap != nullptr;
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
        bool packSrvHeap(ID3D12Device* device);

        std::shared_ptr<Texture2D>   m_albedo;
        float                        m_baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuHandle{};
    };

} // namespace Dark
