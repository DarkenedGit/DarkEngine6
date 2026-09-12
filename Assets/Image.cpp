#include "Assets/Image.h"
#include "Core/Log.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace Dark
{
    using Microsoft::WRL::ComPtr;

    namespace
    {
        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void EnsureCom()
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            (void)hr;
        }

        bool DecodeWicFrame(IWICBitmapFrameDecode* frame, std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outRowPitch)
        {
            ComPtr<IWICImagingFactory> factory;
            if (FailedHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance WICImagingFactory"))
                return false;
            ComPtr<IWICFormatConverter> converter;
            if (FailedHr(factory->CreateFormatConverter(&converter), "WIC CreateFormatConverter"))
                return false;
            if (FailedHr(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "WIC FormatConverter Initialize"))
                return false;
            UINT w = 0;
            UINT h = 0;
            if (FailedHr(converter->GetSize(&w, &h), "WIC GetSize"))
                return false;
            if (w == 0 || h == 0)
                return false;
            const uint32_t rowPitch = w * 4u;
            const size_t   bytes    = static_cast<size_t>(rowPitch) * static_cast<size_t>(h);
            outPixels.resize(bytes);
            if (FailedHr(converter->CopyPixels(nullptr, rowPitch, static_cast<UINT>(bytes), outPixels.data()), "WIC CopyPixels"))
                return false;
            outWidth    = w;
            outHeight   = h;
            outRowPitch = rowPitch;
            return true;
        }
    } // namespace

    Image::Image()
    {
        type = AssetType::Texture2D;
    }

    bool Image::setPixels(const void* data, uint32_t width, uint32_t height, uint32_t rowPitchBytes, ImageFormat format, uint32_t bytesPerPixel)
    {
        if (!data || width == 0 || height == 0 || bytesPerPixel == 0 || rowPitchBytes < width * bytesPerPixel)
        {
            DE_LOG_ERROR("Image: invalid pixel data");
            return false;
        }
        m_width    = width;
        m_height   = height;
        m_rowPitch = rowPitchBytes;
        m_format   = format;
        const size_t bytes = static_cast<size_t>(rowPitchBytes) * static_cast<size_t>(height);
        m_pixels.resize(bytes);
        std::memcpy(m_pixels.data(), data, bytes);
        return true;
    }

    bool Image::valid() const
    {
        if (m_width == 0 || m_height == 0)
            return false;
        const size_t need = static_cast<size_t>(m_rowPitch) * static_cast<size_t>(m_height);
        return m_pixels.size() >= need;
    }

    bool Image::createFromFile(const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            DE_LOG_ERROR("Image: file not found '{}'", path.string());
            return false;
        }
        EnsureCom();
        ComPtr<IWICImagingFactory> factory;
        if (FailedHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance WICImagingFactory"))
            return false;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FailedHr(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "WIC CreateDecoderFromFilename"))
            return false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FailedHr(decoder->GetFrame(0, &frame), "WIC GetFrame"))
            return false;
        std::vector<uint8_t> pixels;
        uint32_t             w = 0, h = 0, pitch = 0;
        if (!DecodeWicFrame(frame.Get(), pixels, w, h, pitch))
            return false;
        if (!setPixels(pixels.data(), w, h, pitch, ImageFormat::RGBA8, 4u))
            return false;
        DE_LOG_INFO("Image: loaded '{}' ({}x{})", path.string(), w, h);
        return true;
    }

    bool Image::createFromMemory(const void* bytes, size_t byteCount)
    {
        if (!bytes || byteCount == 0)
            return false;
        EnsureCom();
        ComPtr<IWICImagingFactory> factory;
        if (FailedHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance WICImagingFactory"))
            return false;
        ComPtr<IWICStream> stream;
        if (FailedHr(factory->CreateStream(&stream), "WIC CreateStream"))
            return false;
        if (FailedHr(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(bytes)), static_cast<DWORD>(byteCount)), "WIC InitializeFromMemory"))
            return false;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FailedHr(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "WIC CreateDecoderFromStream"))
            return false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FailedHr(decoder->GetFrame(0, &frame), "WIC GetFrame"))
            return false;
        std::vector<uint8_t> pixels;
        uint32_t             w = 0, h = 0, pitch = 0;
        if (!DecodeWicFrame(frame.Get(), pixels, w, h, pitch))
        {
            DE_LOG_ERROR("Image: failed to decode {} bytes", byteCount);
            return false;
        }
        return setPixels(pixels.data(), w, h, pitch, ImageFormat::RGBA8, 4u);
    }

    bool Image::createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const uint8_t px[4] = { r, g, b, a };
        return setPixels(px, 1, 1, 4, ImageFormat::RGBA8, 4u);
    }

    bool Image::createSoftCircle(uint32_t size)
    {
        if (size < 4)
            size = 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4u);
        const float cx     = (static_cast<float>(size) - 1.0f) * 0.5f;
        const float radius = cx;
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cx;
                const float d  = std::sqrt(dx * dx + dy * dy) / radius;
                float       a  = 1.0f - d;
                if (a < 0.0f)
                    a = 0.0f;
                a                   = a * a * (3.0f - 2.0f * a);
                const uint8_t alpha = static_cast<uint8_t>(a * 255.0f + 0.5f);
                const size_t  i     = (static_cast<size_t>(y) * size + x) * 4u;
                pixels[i + 0]       = 255;
                pixels[i + 1]       = 255;
                pixels[i + 2]       = 255;
                pixels[i + 3]       = alpha;
            }
        }
        return setPixels(pixels.data(), size, size, size * 4u, ImageFormat::RGBA8, 4u);
    }

    bool Image::createSoftStreak(uint32_t size)
    {
        if (size < 4)
            size = 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4u);
        const float inv = 1.0f / (static_cast<float>(size) - 1.0f);
        for (uint32_t y = 0; y < size; ++y)
        {
            const float v      = static_cast<float>(y) * inv;
            float       across = 1.0f - std::fabs(v * 2.0f - 1.0f);
            across             = across * across * (3.0f - 2.0f * across);
            for (uint32_t x = 0; x < size; ++x)
            {
                const uint8_t alpha = static_cast<uint8_t>(across * 255.0f + 0.5f);
                const size_t  i     = (static_cast<size_t>(y) * size + x) * 4u;
                pixels[i + 0]       = 255;
                pixels[i + 1]       = 255;
                pixels[i + 2]       = 255;
                pixels[i + 3]       = alpha;
            }
        }
        return setPixels(pixels.data(), size, size, size * 4u, ImageFormat::RGBA8, 4u);
    }

    bool Image::createFromRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        return setPixels(rgba, width, height, rowPitchBytes, ImageFormat::RGBA8, 4u);
    }

    bool Image::createFromR32Float(const float* samples, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        return setPixels(samples, width, height, rowPitchBytes, ImageFormat::R32F, static_cast<uint32_t>(sizeof(float)));
    }

} // namespace Dark
