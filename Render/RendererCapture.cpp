#include "Render/Renderer.h"
#include "Render/EnhancedBarrierEmitter.h"

#include "Assets/Image.h"
#include "Core/Log.h"

#include <cstring>
#include <limits>

namespace Dark
{
    namespace
    {
        bool captureHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return true;
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: {} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return false;
        }
    } // namespace

    void Renderer::requestBackBufferCapture(const std::filesystem::path& png)
    {
        m_capturePath    = png;
        m_captureWritten = false;
        m_capturePending = !png.empty();
    }

    bool Renderer::takeBackBufferCaptureResult()
    {
        const bool result = m_captureWritten;
        m_captureWritten = false;
        return result;
    }

    void Renderer::recordBackBufferCapture()
    {
        m_captureRecorded = false;
        if (!m_capturePending)
            return;
        m_capturePending = false;
        if (!m_device || !m_commandList || m_frameIndex >= kFrameCount || !m_renderTargets[m_frameIndex])
            return;
        ID3D12Resource* backBuffer = m_renderTargets[m_frameIndex].Get();
        const D3D12_RESOURCE_DESC desc = backBuffer->GetDesc();
        if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.SampleDesc.Count != 1)
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: only the RGBA8 single-sample swap chain is supported");
            return;
        }
        UINT64 bytes = 0;
        m_device->GetCopyableFootprints(&desc, 0, 1, 0, &m_captureFootprint, nullptr, nullptr, &bytes);
        // RowPitch * (Height - 1) + rowBytes: the last row's pitch padding is not part of the buffer.
        m_captureBytes = bytes;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width            = bytes;
        buffer.Height           = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels        = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!captureHr(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&m_captureReadback)), "create readback buffer"))
            return;
        m_captureReadback->SetName(L"DE.FrameCapture.Readback");

        recordTransitionTo(m_commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource        = backBuffer;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource       = m_captureReadback.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = m_captureFootprint;
        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        recordTransitionTo(m_commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_captureRecorded = true;
    }

    void Renderer::writeBackBufferCapture()
    {
        m_captureRecorded = false;
        if (!m_device || !m_commandQueue || !m_captureReadback)
            return;
        // A private fence avoids changing any of the per-slot allocator fence values.
        ComPtr<ID3D12Fence> fence;
        if (!captureHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "create capture fence"))
            return;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: CreateEventW failed ({})", GetLastError());
            return;
        }
        bool ok = captureHr(m_commandQueue->Signal(fence.Get(), 1), "signal capture fence");
        if (ok && fence->GetCompletedValue() < 1)
        {
            ok = captureHr(fence->SetEventOnCompletion(1, event), "capture SetEventOnCompletion");
            if (ok)
                ok = WaitForSingleObjectEx(event, INFINITE, FALSE) == WAIT_OBJECT_0;
        }
        CloseHandle(event);
        if (!ok || fence->GetCompletedValue() == UINT64_MAX || FAILED(m_device->GetDeviceRemovedReason()))
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: GPU readback did not complete");
            logDeviceRemoved("frame capture");
            return; // Keep the readback resource alive; the device may still reference it.
        }
        const UINT width  = m_captureFootprint.Footprint.Width;
        const UINT height = m_captureFootprint.Footprint.Height;
        const UINT pitch  = m_captureFootprint.Footprint.RowPitch;
        const UINT packedPitch = width * 4;
        // GetCopyableFootprints' total already counts the footprint offset (BaseOffset 0 here).
        if (width == 0 || height == 0 || pitch < packedPitch
            || m_captureBytes < m_captureFootprint.Offset + static_cast<uint64_t>(pitch) * (height - 1) + packedPitch)
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: unexpected readback footprint (pitch {}, width {}, bytes {})", pitch, width, m_captureBytes);
            return;
        }
        const uint64_t end = m_captureBytes;
        if (end > (std::numeric_limits<SIZE_T>::max)())
            return;
        D3D12_RANGE range{ static_cast<SIZE_T>(m_captureFootprint.Offset), static_cast<SIZE_T>(end) };
        void*      mapped = nullptr;
        if (!captureHr(m_captureReadback->Map(0, &range, &mapped), "map readback"))
            return;
        // Copy to packed rows and force opaque alpha: swap-chain blending need not preserve it.
        std::vector<uint8_t> pixels(static_cast<size_t>(packedPitch) * height);
        const uint8_t* src = static_cast<const uint8_t*>(mapped) + m_captureFootprint.Offset;
        for (UINT y = 0; y < height; ++y)
        {
            uint8_t* row = pixels.data() + static_cast<size_t>(y) * packedPitch;
            std::memcpy(row, src + static_cast<size_t>(y) * pitch, packedPitch);
            for (UINT x = 0; x < width; ++x)
                row[x * 4 + 3] = 255;
        }
        const D3D12_RANGE noWrite{ 0, 0 };
        m_captureReadback->Unmap(0, &noWrite);
        m_captureReadback.Reset();
        m_captureWritten = writeRgbaPng(m_capturePath, pixels.data(), width, height, packedPitch);
        if (m_captureWritten)
            DE_LOG_INFO(LogCategory::Render, "Frame capture: wrote '{}' ({}x{})", m_capturePath.string(), width, height);
    }

} // namespace Dark
