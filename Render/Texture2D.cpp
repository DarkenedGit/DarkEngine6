#include "Render/Texture2D.h"
#include "Assets/Image.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <cstring>
#include <vector>

namespace Dark
{
    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        bool IsTypeless(DXGI_FORMAT format)
        {
            return format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        }

        // IEEE-754 binary32 → binary16, round-to-nearest (half up). Overflow → Inf.
        uint16_t FloatToHalf(float value)
        {
            uint32_t f = 0;
            std::memcpy(&f, &value, sizeof(f));
            const uint32_t sign     = (f >> 16) & 0x8000u;
            const int32_t  exponent = static_cast<int32_t>((f >> 23) & 0xffu) - 127 + 15;
            uint32_t       mantissa = f & 0x7fffffu;

            if (exponent <= 0)
            {
                if (exponent < -10)
                    return static_cast<uint16_t>(sign);
                mantissa = (mantissa | 0x800000u) >> (1 - exponent);
                if (mantissa & 0x1000u)
                    mantissa += 0x2000u;
                return static_cast<uint16_t>(sign | (mantissa >> 13));
            }
            if (((f >> 23) & 0xffu) == 0xffu)
            {
                if (mantissa == 0)
                    return static_cast<uint16_t>(sign | 0x7c00u);
                return static_cast<uint16_t>(sign | 0x7e00u);
            }
            if (exponent > 30)
                return static_cast<uint16_t>(sign | 0x7c00u);
            if (mantissa & 0x1000u)
            {
                mantissa += 0x2000u;
                if (mantissa & 0x800000u)
                {
                    mantissa = 0;
                    if (exponent + 1 > 30)
                        return static_cast<uint16_t>(sign | 0x7c00u);
                    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent + 1) << 10));
                }
            }
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
        }

        void CreateTexSrv(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format                  = format;
            srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(resource, &srvDesc, dest);
        }

        void CreateTexUav(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format               = format;
            uavDesc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice   = 0;
            uavDesc.Texture2D.PlaneSlice = 0;
            device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, dest);
        }

    } // namespace

    bool resolveTextureFormats(Color::ColorSpace space, ImageFormat imgFmt, DXGI_FORMAT& resourceFmt, DXGI_FORMAT& srvFmt, DXGI_FORMAT& footprintFmt)
    {
        if (imgFmt == ImageFormat::RGBA8)
        {
            if (space == Color::ColorSpace::sRGB)
            {
                resourceFmt  = DXGI_FORMAT_R8G8B8A8_TYPELESS;
                srvFmt       = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                footprintFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
                return true;
            }
            if (space == Color::ColorSpace::Linear)
            {
                resourceFmt  = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvFmt       = DXGI_FORMAT_R8G8B8A8_UNORM;
                footprintFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
                return true;
            }
            DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: ColorSpace::Unknown is not a GPU format");
            return false;
        }

        if (imgFmt == ImageFormat::R32F)
        {
            if (space == Color::ColorSpace::sRGB)
            {
                DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: sRGB is invalid for R32F");
                return false;
            }
            if (space == Color::ColorSpace::Linear)
            {
                resourceFmt  = DXGI_FORMAT_R32_FLOAT;
                srvFmt       = DXGI_FORMAT_R32_FLOAT;
                footprintFmt = DXGI_FORMAT_R32_FLOAT;
                return true;
            }
            DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: ColorSpace::Unknown is not a GPU format");
            return false;
        }

        if (imgFmt == ImageFormat::RGBA32F)
        {
            if (space == Color::ColorSpace::sRGB)
            {
                DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: sRGB is invalid for RGBA32F");
                return false;
            }
            if (space == Color::ColorSpace::Linear)
            {
                resourceFmt  = DXGI_FORMAT_R16G16B16A16_FLOAT;
                srvFmt       = DXGI_FORMAT_R16G16B16A16_FLOAT;
                footprintFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
                return true;
            }
            DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: ColorSpace::Unknown is not a GPU format");
            return false;
        }

        DE_LOG_ERROR(LogCategory::Render, "resolveTextureFormats: unsupported ImageFormat");
        return false;
    }

    bool Texture2D::createFromImage(Renderer& renderer, const Image& image, Color::TextureUsage usage)
    {
        if (!image.valid() || !image.pixels())
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: invalid Image");
            return false;
        }

        const Color::ColorSpace gpuSpace = Color::resolveGpuColorSpace(usage, image.colorSpace());
        if (image.colorSpace() == Color::ColorSpace::Unknown && Color::inferColorSpaceForUsage(usage) == Color::ColorSpace::sRGB)
        {
            static bool s_loggedUnknownDefault = false;
            if (!s_loggedUnknownDefault)
            {
                DE_LOG_INFO(LogCategory::Render, "Texture2D: Image ColorSpace::Unknown defaulted to sRGB (Albedo/Emissive)");
                s_loggedUnknownDefault = true;
            }
        }

        DXGI_FORMAT resourceFmt{};
        DXGI_FORMAT srvFmt{};
        DXGI_FORMAT footprintFmt{};
        if (!resolveTextureFormats(gpuSpace, image.format(), resourceFmt, srvFmt, footprintFmt))
            return false;

        if (image.format() == ImageFormat::RGBA32F)
        {
            const uint32_t        w        = image.width();
            const uint32_t        h        = image.height();
            const uint32_t        srcPitch = image.rowPitchBytes();
            const uint32_t        dstPitch = w * 8u;
            std::vector<uint16_t> half(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
            for (uint32_t y = 0; y < h; ++y)
            {
                const uint8_t* srcRow = image.pixels() + static_cast<size_t>(y) * srcPitch;
                uint16_t*      dstRow = half.data() + static_cast<size_t>(y) * w * 4u;
                for (uint32_t x = 0; x < w; ++x)
                {
                    float rgba[4];
                    std::memcpy(rgba, srcRow + static_cast<size_t>(x) * 16u, sizeof(rgba));
                    dstRow[x * 4u + 0] = FloatToHalf(rgba[0]);
                    dstRow[x * 4u + 1] = FloatToHalf(rgba[1]);
                    dstRow[x * 4u + 2] = FloatToHalf(rgba[2]);
                    dstRow[x * 4u + 3] = FloatToHalf(rgba[3]);
                }
            }
            return createFromRaw(renderer, half.data(), w, h, dstPitch, resourceFmt, srvFmt, footprintFmt, 8u);
        }

        return createFromRaw(renderer, image.pixels(), image.width(), image.height(), image.rowPitchBytes(), resourceFmt, srvFmt, footprintFmt, image.bytesPerPixel());
    }

    bool Texture2D::createFromFile(Renderer& renderer, const std::filesystem::path& path, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createFromFile(path))
            return false;
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createFromMemory(Renderer& renderer, const void* bytes, size_t byteCount, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createFromMemory(bytes, byteCount))
            return false;
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createSolidColor(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createSolidColor(r, g, b, a))
            return false;
        img.setColorSpace(Color::resolveGpuColorSpace(usage, img.colorSpace()));
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createSoftCircle(Renderer& renderer, uint32_t size, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createSoftCircle(size))
            return false;
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createSoftStreak(Renderer& renderer, uint32_t size, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createSoftStreak(size))
            return false;
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createFromRGBA(Renderer& renderer, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes, Color::TextureUsage usage)
    {
        Image img;
        if (!img.createFromRGBA(rgba, width, height, rowPitchBytes))
            return false;
        return createFromImage(renderer, img, usage);
    }

    bool Texture2D::createFromR32Float(Renderer& renderer, const float* samples, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        DXGI_FORMAT resourceFmt{};
        DXGI_FORMAT srvFmt{};
        DXGI_FORMAT footprintFmt{};
        if (!resolveTextureFormats(Color::ColorSpace::Linear, ImageFormat::R32F, resourceFmt, srvFmt, footprintFmt))
            return false;
        return createFromRaw(renderer, samples, width, height, rowPitchBytes, resourceFmt, srvFmt, footprintFmt, static_cast<uint32_t>(sizeof(float)));
    }

    bool Texture2D::createUavR32Float(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        m_resource.Reset();
        m_cpuSrvHeap.Reset();
        m_srvHeap.Reset();
        m_cpuHandle     = {};
        m_cpuHandleRaw  = {};
        m_cpuHandleSrgb = {};
        m_cpuHandleUav  = {};
        m_gpuHandle     = {};
        m_width         = 0;
        m_height        = 0;

        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D::createUavR32Float: null device");
            return false;
        }
        if (width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D::createUavR32Float: invalid size {}x{}", width, height);
            return false;
        }

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_R32_FLOAT;
        texDesc.SampleDesc       = { 1, 0 };
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        if (FailedHr(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_resource)),
                     "CreateCommittedResource UAV R32F"))
            return false;

        // Slot 0 SRV, slot 1 UAV — FLAG_NONE so CopyDescriptors into the bake heap is legal.
        D3D12_DESCRIPTOR_HEAP_DESC cpuHeapDesc{};
        cpuHeapDesc.NumDescriptors = 2;
        cpuHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuHeapDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap UAV R32F CPU"))
        {
            m_resource.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC gpuHeapDesc = cpuHeapDesc;
        gpuHeapDesc.NumDescriptors             = 1;
        gpuHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&gpuHeapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap UAV R32F GPU SRV"))
        {
            m_cpuSrvHeap.Reset();
            m_resource.Reset();
            return false;
        }

        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_cpuHandleRaw  = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();
        m_cpuHandle     = m_cpuHandleRaw;
        m_cpuHandleUav  = m_cpuHandleRaw;
        m_cpuHandleUav.ptr += incr;
        m_gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        CreateTexSrv(device, m_resource.Get(), DXGI_FORMAT_R32_FLOAT, m_cpuHandle);
        CreateTexUav(device, m_resource.Get(), DXGI_FORMAT_R32_FLOAT, m_cpuHandleUav);
        CreateTexSrv(device, m_resource.Get(), DXGI_FORMAT_R32_FLOAT, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        m_width  = width;
        m_height = height;
        return true;
    }

    bool Texture2D::createFromRgFloat(Renderer& renderer, const float* rg, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        if (!rg || width == 0 || height == 0 || rowPitchBytes < width * 8u)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: invalid RG float data");
            return false;
        }
        const uint32_t        dstPitch = width * 4u;
        std::vector<uint16_t> half(static_cast<size_t>(width) * static_cast<size_t>(height) * 2u);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t* srcRow = reinterpret_cast<const uint8_t*>(rg) + static_cast<size_t>(y) * rowPitchBytes;
            uint16_t*      dstRow = half.data() + static_cast<size_t>(y) * width * 2u;
            for (uint32_t x = 0; x < width; ++x)
            {
                float pair[2];
                std::memcpy(pair, srcRow + static_cast<size_t>(x) * 8u, sizeof(pair));
                dstRow[x * 2u + 0] = FloatToHalf(pair[0]);
                dstRow[x * 2u + 1] = FloatToHalf(pair[1]);
            }
        }
        return createFromRaw(renderer, half.data(), width, height, dstPitch, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, 4u);
    }

    bool Texture2D::createFromRaw(Renderer& renderer, const void* data, uint32_t width, uint32_t height, uint32_t rowPitchBytes, DXGI_FORMAT resourceFormat, DXGI_FORMAT srvFormat,
                                  DXGI_FORMAT footprintFormat, uint32_t bytesPerPixel)
    {
        if (!data || width == 0 || height == 0 || bytesPerPixel == 0 || rowPitchBytes < width * bytesPerPixel)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: invalid pixel data");
            return false;
        }
        if (resourceFormat == DXGI_FORMAT_UNKNOWN || srvFormat == DXGI_FORMAT_UNKNOWN || footprintFormat == DXGI_FORMAT_UNKNOWN || IsTypeless(srvFormat) || IsTypeless(footprintFormat))
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: footprint/SRV format cannot be TYPELESS or UNKNOWN");
            return false;
        }
        if (srvFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && resourceFormat != DXGI_FORMAT_R8G8B8A8_TYPELESS)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: _SRGB SRV requires R8G8B8A8_TYPELESS resource");
            return false;
        }

        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: null device");
            return false;
        }

        // Reset previous GPU objects if reloading.
        m_resource.Reset();
        m_cpuSrvHeap.Reset();
        m_srvHeap.Reset();
        m_cpuHandle     = {};
        m_cpuHandleRaw  = {};
        m_cpuHandleSrgb = {};
        m_cpuHandleUav  = {};
        m_gpuHandle     = {};
        m_width         = width;
        m_height        = height;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = resourceFormat;
        texDesc.SampleDesc       = { 1, 0 };
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        if (FailedHr(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource)),
                     "CreateCommittedResource texture"))
        {
            return false;
        }

        // GetCopyableFootprints rejects TYPELESS; copy uses a typed UNORM (or float) desc.
        D3D12_RESOURCE_DESC footprintDesc = texDesc;
        footprintDesc.Format              = footprintFormat;

        UINT64                             uploadBytes = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT                               numRows = 0;
        UINT64                             rowSize = 0;
        device->GetCopyableFootprints(&footprintDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadBytes);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width            = uploadBytes;
        uploadDesc.Height           = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels        = 1;
        uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
        uploadDesc.SampleDesc       = { 1, 0 };
        uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
                     "CreateCommittedResource texture upload"))
        {
            m_resource.Reset();
            return false;
        }

        uint8_t* mapped = nullptr;
        if (FailedHr(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map texture upload"))
        {
            m_resource.Reset();
            return false;
        }

        for (UINT y = 0; y < numRows; ++y)
        {
            uint8_t*       dst = mapped + footprint.Offset + y * footprint.Footprint.RowPitch;
            const uint8_t* src = static_cast<const uint8_t*>(data) + y * rowPitchBytes;
            memcpy(dst, src, static_cast<size_t>(width) * bytesPerPixel);
        }
        upload->Unmap(0, nullptr);

        ComPtr<ID3D12CommandAllocator>    alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (texture upload)") ||
            FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (texture upload)"))
        {
            m_resource.Reset();
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = m_resource.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = upload.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;

        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = m_resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);

        if (FailedHr(list->Close(), "Close texture upload list"))
        {
            m_resource.Reset();
            return false;
        }

        ID3D12CommandList* lists[] = { list.Get() };
        renderer.queue()->ExecuteCommandLists(1, lists);
        renderer.waitForGpu();

        // Staging heap is CPU-readable (legal CopyDescriptors source). Shader-visible heaps
        // are CPU write-only — CreateSRV into them is fine, copying FROM them is not (#654).
        const bool                 typelessColor = IsTypeless(resourceFormat);
        D3D12_DESCRIPTOR_HEAP_DESC cpuHeapDesc{};
        cpuHeapDesc.NumDescriptors = typelessColor ? 2u : 1u;
        cpuHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuHeapDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap texture CPU SRV"))
        {
            m_resource.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC gpuHeapDesc = cpuHeapDesc;
        gpuHeapDesc.NumDescriptors             = 1;
        gpuHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&gpuHeapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap texture GPU SRV"))
        {
            m_cpuSrvHeap.Reset();
            m_resource.Reset();
            return false;
        }

        m_cpuHandleRaw = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpuHandle    = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        if (typelessColor)
        {
            // Slot 0 UNORM (raw), slot 1 UNORM_SRGB. Sampling is slot 1; debug flag copies slot 0.
            const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_cpuHandleSrgb = m_cpuHandleRaw;
            m_cpuHandleSrgb.ptr += incr;
            CreateTexSrv(device, m_resource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, m_cpuHandleRaw);
            CreateTexSrv(device, m_resource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_cpuHandleSrgb);
            m_cpuHandle = m_cpuHandleSrgb;
        }
        else
        {
            m_cpuHandle     = m_cpuHandleRaw;
            m_cpuHandleSrgb = {};
            CreateTexSrv(device, m_resource.Get(), srvFormat, m_cpuHandle);
        }
        CreateTexSrv(device, m_resource.Get(), srvFormat, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        return true;
    }

    void Texture2D::bind(ID3D12GraphicsCommandList* cmd, UINT rootParameterIndex) const
    {
        if (!cmd || !valid() || !m_srvHeap)
            return;

        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(rootParameterIndex, m_gpuHandle);
    }

} // namespace Dark
