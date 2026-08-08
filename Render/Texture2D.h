#pragma once

#include <cstdint>
#include <filesystem>

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Renderer;

using Microsoft::WRL::ComPtr;

// GPU texture (default-heap) + single-slot shader-visible SRV heap.
// Loaded from common image formats via WIC (PNG, JPEG, BMP, etc.).
class Texture2D
{
public:
    Texture2D() = default;

    Texture2D(Texture2D&&) noexcept            = default;
    Texture2D& operator=(Texture2D&&) noexcept = default;

    Texture2D(const Texture2D&)            = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    // Decode image from disk, upload to GPU, create SRV. Returns false on failure.
    bool createFromFile(Renderer& renderer, const std::filesystem::path& path);

    // 1x1 solid color fallback (RGBA 0-255).
    bool createSolidColor(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Soft circular sprite (for particles); size should be power-of-two (e.g. 64).
    bool createSoftCircle(Renderer& renderer, uint32_t size = 64);

    // Raw RGBA8 upload (rowPitchBytes usually width*4).
    bool createFromRGBA(
        Renderer& renderer,
        const uint8_t* rgba,
        uint32_t width,
        uint32_t height,
        uint32_t rowPitchBytes);

    // SetDescriptorHeaps + SetGraphicsRootDescriptorTable for this SRV.
    void bind(ID3D12GraphicsCommandList* cmd, UINT rootParameterIndex) const;

    bool     valid()  const { return m_resource != nullptr; }
    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }

    ID3D12DescriptorHeap*       srvHeap()   const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle() const { return m_gpuHandle; }

private:

    ComPtr<ID3D12Resource>       m_resource;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuHandle{};
    uint32_t                     m_width  = 0;
    uint32_t                     m_height = 0;
};

} // namespace Dark
