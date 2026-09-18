#include "Assets/Image.h"
#include "Core/Log.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
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

        constexpr uint32_t kMaxHdrWidth       = 4096;
        constexpr uint32_t kMaxHdrHeight      = 2048;
        constexpr size_t   kMaxHdrFileBytes   = 32u * 1024u * 1024u;
        constexpr size_t   kMaxHdrDecodeBytes = static_cast<size_t>(kMaxHdrWidth) * static_cast<size_t>(kMaxHdrHeight) * 16u;
        constexpr size_t   kMaxHdrLineBytes   = 1024;
        constexpr int      kMaxHdrHeaderLines = 256;

        struct ByteCursor
        {
            const uint8_t* p   = nullptr;
            const uint8_t* end = nullptr;
        };

        bool IsHdrExtension(const std::filesystem::path& path)
        {
            std::string ext = path.extension().string();
            for (char& ch : ext)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return ext == ".hdr";
        }

        bool LooksLikeRadianceHdr(const void* bytes, size_t byteCount)
        {
            static constexpr char kRadiance[] = "#?RADIANCE";
            static constexpr char kRgbe[]     = "#?RGBE";
            const auto*           p           = static_cast<const char*>(bytes);
            if (byteCount >= sizeof(kRadiance) - 1 && std::memcmp(p, kRadiance, sizeof(kRadiance) - 1) == 0)
                return true;
            if (byteCount >= sizeof(kRgbe) - 1 && std::memcmp(p, kRgbe, sizeof(kRgbe) - 1) == 0)
                return true;
            return false;
        }

        bool IeEquals(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                const unsigned char ca = static_cast<unsigned char>(a[i]);
                const unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb))
                    return false;
            }
            return true;
        }

        std::string TrimCopy(std::string_view s)
        {
            size_t begin = 0;
            size_t end   = s.size();
            while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
                ++begin;
            while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t'))
                --end;
            return std::string(s.substr(begin, end - begin));
        }

        bool ReadHdrLine(ByteCursor& c, std::string& out)
        {
            out.clear();
            if (c.p >= c.end)
                return false;
            while (c.p < c.end)
            {
                const uint8_t ch = *c.p++;
                if (ch == '\n')
                    break;
                if (ch == '\r')
                {
                    if (c.p < c.end && *c.p == '\n')
                        ++c.p;
                    break;
                }
                if (out.size() >= kMaxHdrLineBytes)
                    return false;
                out.push_back(static_cast<char>(ch));
            }
            return true;
        }

        bool ReadHdrBytes(ByteCursor& c, void* dst, size_t n)
        {
            if (n == 0)
                return true;
            if (c.p >= c.end || n > static_cast<size_t>(c.end - c.p))
                return false;
            std::memcpy(dst, c.p, n);
            c.p += n;
            return true;
        }

        bool ParseUnsigned(const char*& s, unsigned& out)
        {
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s < '0' || *s > '9')
                return false;
            unsigned v = 0;
            while (*s >= '0' && *s <= '9')
            {
                const unsigned digit = static_cast<unsigned>(*s - '0');
                if (v > (4294967295u - digit) / 10u)
                    return false;
                v = v * 10u + digit;
                ++s;
            }
            out = v;
            return true;
        }

        bool ParseHdrResolution(const std::string& line, uint32_t& outWidth, uint32_t& outHeight)
        {
            const char* s = line.c_str();
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s != '-')
                return false;
            ++s;
            if (*s != 'Y')
                return false;
            ++s;
            unsigned height = 0;
            unsigned width  = 0;
            if (!ParseUnsigned(s, height))
                return false;
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s != '+')
                return false;
            ++s;
            if (*s != 'X')
                return false;
            ++s;
            if (!ParseUnsigned(s, width))
                return false;
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s != '\0')
                return false;
            if (height == 0 || width == 0)
                return false;
            outHeight = height;
            outWidth  = width;
            return true;
        }

        bool ParseHdrHeader(ByteCursor& c, uint32_t& width, uint32_t& height)
        {
            std::string line;
            if (!ReadHdrLine(c, line) || (!line.starts_with("#?RADIANCE") && !line.starts_with("#?RGBE")))
            {
                DE_LOG_ERROR("Image: not a Radiance RGBE header");
                return false;
            }

            bool sawXyze  = false;
            bool sawRgbe  = false;
            bool sawBlank = false;
            for (int i = 0; i < kMaxHdrHeaderLines; ++i)
            {
                if (!ReadHdrLine(c, line))
                {
                    DE_LOG_ERROR("Image: truncated Radiance header");
                    return false;
                }
                if (line.empty())
                {
                    sawBlank = true;
                    break;
                }
                const std::string trimmed = TrimCopy(line);
                if (trimmed.size() >= 7 && IeEquals(trimmed.substr(0, 7), "FORMAT="))
                {
                    const std::string val = TrimCopy(trimmed.substr(7));
                    if (IeEquals(val, "32-bit_rle_xyze"))
                        sawXyze = true;
                    else if (IeEquals(val, "32-bit_rle_rgbe"))
                        sawRgbe = true;
                    else
                    {
                        DE_LOG_ERROR("Image: unsupported Radiance FORMAT '{}'", val);
                        return false;
                    }
                }
            }
            if (!sawBlank)
            {
                DE_LOG_ERROR("Image: Radiance header too long or missing blank line");
                return false;
            }
            if (sawXyze)
            {
                DE_LOG_ERROR("Image: Radiance XYZE is not supported");
                return false;
            }
            if (!sawRgbe)
            {
                DE_LOG_ERROR("Image: Radiance header missing FORMAT=32-bit_rle_rgbe");
                return false;
            }

            if (!ReadHdrLine(c, line))
            {
                DE_LOG_ERROR("Image: missing Radiance resolution line");
                return false;
            }
            if (!ParseHdrResolution(line, width, height))
            {
                DE_LOG_ERROR("Image: HDR unknown resolution layout (expected -Y height +X width)");
                return false;
            }
            if (width > kMaxHdrWidth || height > kMaxHdrHeight)
            {
                DE_LOG_ERROR("Image: HDR dimensions {}x{} exceed cap {}x{}", width, height, kMaxHdrWidth, kMaxHdrHeight);
                return false;
            }
            const size_t decodeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 16u;
            if (decodeBytes > kMaxHdrDecodeBytes)
            {
                DE_LOG_ERROR("Image: HDR decode size {} exceeds cap {}", decodeBytes, kMaxHdrDecodeBytes);
                return false;
            }
            return true;
        }

        void RgbeToRgba32f(const uint8_t rgbe[4], float out[4])
        {
            if (rgbe[3] == 0)
            {
                out[0] = 0.0f;
                out[1] = 0.0f;
                out[2] = 0.0f;
                out[3] = 1.0f;
                return;
            }
            const float f = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - (128 + 8));
            out[0]        = static_cast<float>(rgbe[0]) * f;
            out[1]        = static_cast<float>(rgbe[1]) * f;
            out[2]        = static_cast<float>(rgbe[2]) * f;
            out[3]        = 1.0f;
        }

        bool DecodeHdrScanline(ByteCursor& c, uint32_t width, uint8_t* rgbeRow)
        {
            uint8_t head[4];
            if (!ReadHdrBytes(c, head, 4))
                return false;

            if (head[0] == 2 && head[1] == 2 && head[2] < 128)
            {
                const uint32_t scanW = (static_cast<uint32_t>(head[2]) << 8) | static_cast<uint32_t>(head[3]);
                if (scanW != width)
                    return false;
                for (uint32_t ch = 0; ch < 4; ++ch)
                {
                    uint32_t x = 0;
                    while (x < width)
                    {
                        uint8_t count = 0;
                        if (!ReadHdrBytes(c, &count, 1))
                            return false;
                        if (count > 128)
                        {
                            const uint32_t run = static_cast<uint32_t>(count) - 128u;
                            uint8_t        val = 0;
                            if (!ReadHdrBytes(c, &val, 1))
                                return false;
                            if (run == 0 || run > width - x)
                                return false;
                            for (uint32_t i = 0; i < run; ++i)
                                rgbeRow[(x + i) * 4u + ch] = val;
                            x += run;
                        }
                        else
                        {
                            if (count == 0 || static_cast<uint32_t>(count) > width - x)
                                return false;
                            for (uint32_t i = 0; i < count; ++i)
                            {
                                uint8_t val = 0;
                                if (!ReadHdrBytes(c, &val, 1))
                                    return false;
                                rgbeRow[(x + i) * 4u + ch] = val;
                            }
                            x += count;
                        }
                    }
                }
                return true;
            }

            std::memcpy(rgbeRow, head, 4);
            if (width > 1)
            {
                if (!ReadHdrBytes(c, rgbeRow + 4, static_cast<size_t>(width - 1u) * 4u))
                    return false;
            }
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
        if (path.empty())
        {
            DE_LOG_ERROR("Image: file not found '{}'", path.string());
            return false;
        }
        std::error_code existsEc;
        if (!std::filesystem::exists(path, existsEc) || existsEc)
        {
            DE_LOG_ERROR("Image: file not found '{}'", path.string());
            return false;
        }
        if (IsHdrExtension(path))
            return createFromHdrFile(path);
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
        m_colorSpace          = Color::ColorSpace::Unknown;
        m_colorSpaceDefaulted = true;
        DE_LOG_INFO("Image: loaded '{}' ({}x{})", path.string(), w, h);
        return true;
    }

    bool Image::createFromMemory(const void* bytes, size_t byteCount)
    {
        if (!bytes || byteCount == 0)
            return false;
        if (LooksLikeRadianceHdr(bytes, byteCount))
            return createFromHdrMemory(bytes, byteCount);
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
        if (!setPixels(pixels.data(), w, h, pitch, ImageFormat::RGBA8, 4u))
            return false;
        m_colorSpace          = Color::ColorSpace::Unknown;
        m_colorSpaceDefaulted = true;
        return true;
    }

    bool Image::createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const uint8_t px[4] = { r, g, b, a };
        if (!setPixels(px, 1, 1, 4, ImageFormat::RGBA8, 4u))
            return false;
        m_colorSpace          = Color::ColorSpace::Unknown;
        m_colorSpaceDefaulted = true;
        return true;
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
        if (!setPixels(pixels.data(), size, size, size * 4u, ImageFormat::RGBA8, 4u))
            return false;
        m_colorSpace          = Color::ColorSpace::Linear;
        m_colorSpaceDefaulted = false;
        return true;
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
        if (!setPixels(pixels.data(), size, size, size * 4u, ImageFormat::RGBA8, 4u))
            return false;
        m_colorSpace          = Color::ColorSpace::Linear;
        m_colorSpaceDefaulted = false;
        return true;
    }

    bool Image::createFromRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        if (!setPixels(rgba, width, height, rowPitchBytes, ImageFormat::RGBA8, 4u))
            return false;
        m_colorSpace          = Color::ColorSpace::Unknown;
        m_colorSpaceDefaulted = true;
        return true;
    }

    bool Image::createFromR32Float(const float* samples, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        if (!setPixels(samples, width, height, rowPitchBytes, ImageFormat::R32F, static_cast<uint32_t>(sizeof(float))))
            return false;
        m_colorSpace          = Color::ColorSpace::Linear;
        m_colorSpaceDefaulted = false;
        return true;
    }

    bool Image::createFromRgba32f(const float* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        if (!setPixels(rgba, width, height, rowPitchBytes, ImageFormat::RGBA32F, 16u))
            return false;
        m_colorSpace          = Color::ColorSpace::Linear;
        m_colorSpaceDefaulted = false;
        return true;
    }

    bool Image::createFromHdrMemory(const void* bytes, size_t byteCount)
    {
        if (byteCount > kMaxHdrFileBytes)
        {
            DE_LOG_ERROR("Image: HDR exceeds 32 MB cap ({} bytes)", byteCount);
            return false;
        }
        if (!bytes || byteCount == 0)
        {
            DE_LOG_ERROR("Image: empty HDR data");
            return false;
        }

        ByteCursor cursor{ static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + byteCount };
        uint32_t   width  = 0;
        uint32_t   height = 0;
        if (!ParseHdrHeader(cursor, width, height))
            return false;

        std::vector<uint8_t> scan(static_cast<size_t>(width) * 4u);
        std::vector<float>   rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        for (uint32_t y = 0; y < height; ++y)
        {
            if (!DecodeHdrScanline(cursor, width, scan.data()))
            {
                DE_LOG_ERROR("Image: HDR decode failed at row {}", y);
                return false;
            }
            float* dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
            for (uint32_t x = 0; x < width; ++x)
                RgbeToRgba32f(scan.data() + static_cast<size_t>(x) * 4u, dst + static_cast<size_t>(x) * 4u);
        }

        if (!setPixels(rgba.data(), width, height, width * 16u, ImageFormat::RGBA32F, 16u))
            return false;
        m_colorSpace          = Color::ColorSpace::Linear;
        m_colorSpaceDefaulted = false;
        return true;
    }

    bool Image::createFromHdrFile(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            DE_LOG_ERROR("Image: file not found '{}'", path.string());
            return false;
        }
        std::error_code existsEc;
        if (!std::filesystem::exists(path, existsEc) || existsEc)
        {
            DE_LOG_ERROR("Image: file not found '{}'", path.string());
            return false;
        }

        std::error_code      sizeEc;
        const std::uintmax_t fileBytes = std::filesystem::file_size(path, sizeEc);
        if (sizeEc)
        {
            DE_LOG_ERROR("Image: failed to stat HDR '{}': {}", path.string(), sizeEc.message());
            return false;
        }
        if (fileBytes == 0)
        {
            DE_LOG_ERROR("Image: empty HDR '{}'", path.string());
            return false;
        }
        if (fileBytes > kMaxHdrFileBytes)
        {
            DE_LOG_ERROR("Image: HDR '{}' exceeds 32 MB cap ({} bytes)", path.string(), static_cast<size_t>(fileBytes));
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("Image: failed to open HDR '{}'", path.string());
            return false;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(fileBytes));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fileBytes));
        if (!in || in.gcount() != static_cast<std::streamsize>(fileBytes))
        {
            DE_LOG_ERROR("Image: failed to read HDR '{}'", path.string());
            return false;
        }
        if (!createFromHdrMemory(bytes.data(), bytes.size()))
            return false;
        DE_LOG_INFO("Image: loaded '{}' ({}x{})", path.string(), width(), height());
        return true;
    }

    void Image::setColorSpace(Color::ColorSpace space)
    {
        m_colorSpace          = space;
        m_colorSpaceDefaulted = false;
    }

} // namespace Dark
