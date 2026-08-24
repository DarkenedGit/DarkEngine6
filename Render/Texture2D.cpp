#include "Render/Texture2D.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <wincodec.h>

#include <cmath>
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

        // Ensure COM is initialized for WIC (safe to call multiple times).
        void EnsureCom()
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            // S_OK, S_FALSE (already init), or RPC_E_CHANGED_MODE (different model) are all fine for us.
            (void)hr;
        }

        bool LoadImageRGBA(const std::filesystem::path& path, std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outRowPitch)
        {
            EnsureCom();

            ComPtr<IWICImagingFactory> factory;
            if (FailedHr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance WICImagingFactory"))
            {
                return false;
            }

            ComPtr<IWICBitmapDecoder> decoder;
            if (FailedHr(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "WIC CreateDecoderFromFilename"))
            {
                return false;
            }

            ComPtr<IWICBitmapFrameDecode> frame;
            if (FailedHr(decoder->GetFrame(0, &frame), "WIC GetFrame"))
                return false;

            ComPtr<IWICFormatConverter> converter;
            if (FailedHr(factory->CreateFormatConverter(&converter), "WIC CreateFormatConverter"))
                return false;

            if (FailedHr(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "WIC FormatConverter Initialize"))
            {
                return false;
            }

            UINT w = 0;
            UINT h = 0;
            if (FailedHr(converter->GetSize(&w, &h), "WIC GetSize"))
                return false;
            if (w == 0 || h == 0)
            {
                DE_LOG_ERROR(LogCategory::Render, "Texture2D: empty image '{}'", path.string());
                return false;
            }

            const uint32_t rowPitch = w * 4u;
            const size_t   bytes    = static_cast<size_t>(rowPitch) * static_cast<size_t>(h);
            outPixels.resize(bytes);

            if (FailedHr(converter->CopyPixels(nullptr, rowPitch, static_cast<UINT>(bytes), outPixels.data()), "WIC CopyPixels"))
            {
                return false;
            }

            outWidth    = w;
            outHeight   = h;
            outRowPitch = rowPitch;
            return true;
        }

    } // namespace

    bool Texture2D::createFromFile(Renderer& renderer, const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: file not found '{}'", path.string());
            return false;
        }

        std::vector<uint8_t> pixels;
        uint32_t             width    = 0;
        uint32_t             height   = 0;
        uint32_t             rowPitch = 0;
        if (!LoadImageRGBA(path, pixels, width, height, rowPitch))
            return false;

        if (!createFromRGBA(renderer, pixels.data(), width, height, rowPitch))
            return false;

        DE_LOG_INFO(LogCategory::Render, "Texture2D: loaded '{}' ({}x{})", path.string(), width, height);
        return true;
    }

    bool Texture2D::createSolidColor(Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const uint8_t px[4] = { r, g, b, a };
        if (!createFromRGBA(renderer, px, 1, 1, 4))
            return false;
        DE_LOG_INFO(LogCategory::Render, "Texture2D: solid color ({},{},{},{})", r, g, b, a);
        return true;
    }

    bool Texture2D::createSoftCircle(Renderer& renderer, uint32_t size)
    {
        if (size < 4)
            size = 4;

        std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4u);
        const float          cx     = (static_cast<float>(size) - 1.0f) * 0.5f;
        const float          cy     = cx;
        const float          radius = cx;

        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                const float d  = std::sqrt(dx * dx + dy * dy) / radius;
                float       a  = 1.0f - d;
                if (a < 0.0f)
                    a = 0.0f;
                // Smooth falloff
                a                   = a * a * (3.0f - 2.0f * a);
                const uint8_t alpha = static_cast<uint8_t>(a * 255.0f + 0.5f);
                const size_t  i     = (static_cast<size_t>(y) * size + x) * 4u;
                pixels[i + 0]       = 255;
                pixels[i + 1]       = 255;
                pixels[i + 2]       = 255;
                pixels[i + 3]       = alpha;
            }
        }

        if (!createFromRGBA(renderer, pixels.data(), size, size, size * 4u))
            return false;
        DE_LOG_INFO(LogCategory::Render, "Texture2D: soft circle {}x{}", size, size);
        return true;
    }

    bool Texture2D::createSoftStreak(Renderer& renderer, uint32_t size)
    {
        if (size < 4)
            size = 4;

        std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4u);
        const float          inv = 1.0f / (static_cast<float>(size) - 1.0f);
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

        if (!createFromRGBA(renderer, pixels.data(), size, size, size * 4u))
            return false;
        DE_LOG_INFO(LogCategory::Render, "Texture2D: soft streak {}x{}", size, size);
        return true;
    }

    bool Texture2D::createFromRGBA(Renderer& renderer, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t rowPitchBytes)
    {
        if (!rgba || width == 0 || height == 0 || rowPitchBytes < width * 4u)
        {
            DE_LOG_ERROR(LogCategory::Render, "Texture2D: invalid pixel data");
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
        m_srvHeap.Reset();
        m_width  = width;
        m_height = height;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
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

        UINT64                             uploadBytes = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT                               numRows = 0;
        UINT64                             rowSize = 0;
        device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadBytes);

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
            const uint8_t* src = rgba + y * rowPitchBytes;
            memcpy(dst, src, static_cast<size_t>(width) * 4u);
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

        // One-descriptor shader-visible heap for this texture.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap SRV"))
        {
            m_resource.Reset();
            return false;
        }

        m_cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_cpuHandle);

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
