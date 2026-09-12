#pragma once

#include "Assets/AssetHandle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Dark
{

    enum class ImageFormat : uint8_t
    {
        RGBA8 = 0,
        R32F  = 1,
    };

    // CPU pixels. No D3D12. type = AssetType::Texture2D (v1 enum).
    class Image : public Asset
    {
    public:
        Image();

        bool createFromFile(const std::filesystem::path& path);
        bool createFromMemory(const void* bytes, size_t byteCount);
        bool createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        bool createSoftCircle(uint32_t size = 64);
        bool createSoftStreak(uint32_t size = 64);
        bool createFromRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes);
        bool createFromR32Float(const float* samples, uint32_t width, uint32_t height, uint32_t rowPitchBytes);

        bool valid() const;
        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }
        uint32_t rowPitchBytes() const { return m_rowPitch; }
        ImageFormat format() const { return m_format; }
        const uint8_t* pixels() const { return m_pixels.empty() ? nullptr : m_pixels.data(); }
        uint32_t bytesPerPixel() const { return m_format == ImageFormat::R32F ? 4u : 4u; }

    private:
        bool setPixels(const void* data, uint32_t width, uint32_t height, uint32_t rowPitchBytes, ImageFormat format, uint32_t bytesPerPixel);

        std::vector<uint8_t> m_pixels;
        uint32_t             m_width    = 0;
        uint32_t             m_height   = 0;
        uint32_t             m_rowPitch = 0;
        ImageFormat          m_format   = ImageFormat::RGBA8;
    };

} // namespace Dark
