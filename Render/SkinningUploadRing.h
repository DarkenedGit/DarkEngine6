#pragma once

#include "Animation/Pose.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{
	using Microsoft::WRL::ComPtr;

	struct BonePaletteCB
	{
		float curr[64][16];
		float prev[64][16];
	};
	static_assert(sizeof(BonePaletteCB) == 8192, "bone CBV size");

	class SkinningUploadRing
	{
	public:
		static constexpr uint32_t kFrameCount   = 2;
		static constexpr uint32_t kMaxInstances = 32;

		bool create(ID3D12Device* device);
		void beginFrame(uint32_t frameIndex);

		// Returns 0 if the ring is full this frame (skip the draw).
		D3D12_GPU_VIRTUAL_ADDRESS alloc(const AnimPose& pose);
		D3D12_GPU_VIRTUAL_ADDRESS dummyGpuVa() const { return m_dummyGpu; }

		bool isValid() const { return m_buffer && m_mapped && m_dummy; }

	private:
		static constexpr UINT64 kStride = sizeof(BonePaletteCB);

		ComPtr<ID3D12Resource>    m_buffer;
		ComPtr<ID3D12Resource>    m_dummy;
		UINT8*                    m_mapped = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS m_gpu    = 0;
		D3D12_GPU_VIRTUAL_ADDRESS m_dummyGpu = 0;
		uint32_t                  m_frameSlot = 0;
		uint32_t                  m_used      = 0;
		bool                      m_warnedOverflow = false;
	};
}
