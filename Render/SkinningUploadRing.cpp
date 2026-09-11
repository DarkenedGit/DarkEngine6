#include "Render/SkinningUploadRing.h"
#include "Core/Log.h"
#include "Math/Matrix4f.h"

#include <cstring>

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

		ComPtr<ID3D12Resource> CreateUpload(ID3D12Device* device, UINT64 size, const char* what)
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width            = size;
			desc.Height           = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels        = 1;
			desc.SampleDesc       = { 1, 0 };
			desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ComPtr<ID3D12Resource> res;
			if (FailedHr(
					device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)),
					what))
				return nullptr;
			return res;
		}

		void fillIdentity(float m[16])
		{
			std::memset(m, 0, 16 * sizeof(float));
			m[0] = m[5] = m[10] = m[15] = 1.0f;
		}

		void copyMat(float dst[16], const Math::Matrix4f& src)
		{
			std::memcpy(dst, src.m_afEntry, 16 * sizeof(float));
		}
	} // namespace

	bool SkinningUploadRing::create(ID3D12Device* device)
	{
		m_buffer.Reset();
		m_dummy.Reset();
		m_mapped = nullptr;
		m_gpu = 0;
		m_dummyGpu = 0;
		m_frameSlot = 0;
		m_used = 0;
		if (!device)
		{
			DE_LOG_ERROR(LogCategory::Render, "SkinningUploadRing::create: null device");
			return false;
		}

		m_buffer = CreateUpload(device, kStride * kMaxInstances * kFrameCount, "CreateCommittedResource bone palettes");
		m_dummy  = CreateUpload(device, kStride, "CreateCommittedResource bone dummy");
		if (!m_buffer || !m_dummy)
			return false;

		if (FailedHr(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped)), "Map bone palettes"))
			return false;
		UINT8* dummyMap = nullptr;
		if (FailedHr(m_dummy->Map(0, nullptr, reinterpret_cast<void**>(&dummyMap)), "Map bone dummy"))
			return false;

		std::memset(m_mapped, 0, static_cast<size_t>(kStride * kMaxInstances * kFrameCount));
		auto* dummyCb = reinterpret_cast<BonePaletteCB*>(dummyMap);
		for (uint32_t i = 0; i < 64; ++i)
		{
			fillIdentity(dummyCb->curr[i]);
			fillIdentity(dummyCb->prev[i]);
		}
		m_dummy->Unmap(0, nullptr);

		m_gpu = m_buffer->GetGPUVirtualAddress();
		m_dummyGpu = m_dummy->GetGPUVirtualAddress();
		DE_LOG_INFO(LogCategory::Render, "SkinningUploadRing: ready ({} instances x {} frames)", kMaxInstances, kFrameCount);
		return true;
	}

	void SkinningUploadRing::beginFrame(uint32_t frameIndex)
	{
		m_frameSlot = frameIndex % kFrameCount;
		m_used = 0;
		m_warnedOverflow = false;
	}

	D3D12_GPU_VIRTUAL_ADDRESS SkinningUploadRing::alloc(const AnimPose& pose)
	{
		if (!isValid())
			return 0;
		if (m_used >= kMaxInstances)
		{
			if (!m_warnedOverflow)
			{
				DE_LOG_WARN(LogCategory::Render, "SkinningUploadRing: full this frame, skipping skinned draws");
				m_warnedOverflow = true;
			}
			return 0;
		}

		auto* cb = reinterpret_cast<BonePaletteCB*>(m_mapped + (static_cast<size_t>(m_frameSlot) * kMaxInstances + m_used) * kStride);
		const uint32_t n = pose.boneCount > 64 ? 64 : pose.boneCount;
		for (uint32_t i = 0; i < 64; ++i)
		{
			if (i < n)
			{
				copyMat(cb->curr[i], pose.palette[i]);
				copyMat(cb->prev[i], pose.prevPalette[i]);
			}
			else
			{
				fillIdentity(cb->curr[i]);
				fillIdentity(cb->prev[i]);
			}
		}
		const D3D12_GPU_VIRTUAL_ADDRESS va = m_gpu + (static_cast<UINT64>(m_frameSlot) * kMaxInstances + m_used) * kStride;
		++m_used;
		return va;
	}
}
