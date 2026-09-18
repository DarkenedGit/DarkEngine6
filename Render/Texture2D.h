#pragma once

#include "Assets/Image.h"
#include "Math/Color.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // srvFmt is the eventual sampling format (UNORM_SRGB for sRGB 8-bit). Footprint is never TYPELESS.
    bool resolveTextureFormats(Color::ColorSpace space, ImageFormat imgFmt, DXGI_FORMAT& resourceFmt, DXGI_FORMAT& srvFmt, DXGI_FORMAT& footprintFmt);

    // GPU texture (default-heap) + SRV.
    // cpuHandle() is on a non-shader-visible heap so it is a legal CopyDescriptors source.
    // gpuHandle()/bind() use a shader-visible heap (those heaps are CPU write-only).
    // TYPELESS color FLAG_NONE layout: slot 0 UNORM (cpuHandleRaw), slot 1 UNORM_SRGB (m_cpuHandleSrgb, private).
    // cpuHandle() is the sampling view: slot 1 _SRGB when present, else slot 0 UNORM.
    // Shader-visible heap is 1 slot matching cpuHandle(). Hud/Data have no sRGB view.
    // Loaded from common image formats via WIC (PNG, JPEG, BMP, etc.).
    class Texture2D
    {
    public:
        Texture2D() = default;

        Texture2D(Texture2D&&) noexcept            = default;
        Texture2D& operator=(Texture2D&&) noexcept = default;

        Texture2D(const Texture2D&)            = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        // Upload CPU pixels. Decode is Image (WIC lives there).
        bool createFromImage(Renderer& renderer, const Image& image, Color::TextureUsage usage);

        // HUD/particles: stack Image then createFromImage. Not interned in GpuResourceCache.
        bool createFromFile(Renderer& renderer, const std::filesystem::path& path, Color::TextureUsage usage);
        bool createFromMemory(Renderer& renderer, const void* bytes, size_t byteCount, Color::TextureUsage usage);
        bool createSolidColor(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a, Color::TextureUsage usage);
        bool createSoftCircle(Renderer& renderer, uint32_t size = 64, Color::TextureUsage usage = Color::TextureUsage::Data);
        bool createSoftStreak(Renderer& renderer, uint32_t size = 64, Color::TextureUsage usage = Color::TextureUsage::Data);
        bool createFromRGBA(Renderer& renderer, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes, Color::TextureUsage usage);

        // Raw R32_FLOAT height field (rowPitchBytes usually width*4). No WIC. Always Linear.
        bool createFromR32Float(Renderer& renderer, const float* samples, uint32_t width, uint32_t height, uint32_t rowPitchBytes);

        // RG float pairs uploaded as R16G16_FLOAT (BRDF LUT / IBL dummy). Always Linear.
        bool createFromRgFloat(Renderer& renderer, const float* rg, uint32_t width, uint32_t height, uint32_t rowPitchBytes);

        // SetDescriptorHeaps + SetGraphicsRootDescriptorTable for this SRV.
        void bind(ID3D12GraphicsCommandList* cmd, UINT rootParameterIndex) const;

        bool valid() const
        {
            return m_resource != nullptr;
        }
        uint32_t width() const
        {
            return m_width;
        }
        uint32_t height() const
        {
            return m_height;
        }

        ID3D12Resource* resource() const
        {
            return m_resource.Get();
        }
        ID3D12DescriptorHeap* srvHeap() const
        {
            return m_srvHeap.Get();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle() const
        {
            return m_cpuHandle;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleRaw() const
        {
            return m_cpuHandleRaw;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle() const
        {
            return m_gpuHandle;
        }

    private:
        bool createFromRaw(Renderer& renderer, const void* data, uint32_t width, uint32_t height, uint32_t rowPitchBytes, DXGI_FORMAT resourceFormat, DXGI_FORMAT srvFormat,
                           DXGI_FORMAT footprintFormat, uint32_t bytesPerPixel);

        ComPtr<ID3D12Resource>       m_resource;
        ComPtr<ID3D12DescriptorHeap> m_cpuSrvHeap; // FLAG_NONE — CopyDescriptors source (2 slots when TYPELESS color)
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;    // SHADER_VISIBLE — bind / GPU handle (1 slot = cpuHandle)
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuHandleRaw{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuHandleSrgb{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuHandle{};
        uint32_t                     m_width  = 0;
        uint32_t                     m_height = 0;
    };

} // namespace Dark
