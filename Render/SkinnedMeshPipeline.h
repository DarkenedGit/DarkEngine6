#pragma once

#include "Render/DebugRenderState.h"
#include "Render/MeshPipeline.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{
	using Microsoft::WRL::ComPtr;

	enum class SkinnedMeshPass : uint8_t
	{
		GBuffer = 0,
		Forward,
		ForwardTransparent,
		Shadow
	};

	class SkinnedMeshPipeline
	{
	public:
		static constexpr UINT kRootConstants = 0;
		static constexpr UINT kRootAlbedoSrv = 1;
		static constexpr UINT kRootShadowCbv = 2;
		static constexpr UINT kRootBoneCbv   = 3;

		bool create(ID3D12Device* device, SkinnedMeshPass pass, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

		void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
		void setConstants(ID3D12GraphicsCommandList* cmd, const MeshFrameConstants& constants) const;
		void setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshGBufferConstants& constants) const;
		void setWvp(ID3D12GraphicsCommandList* cmd, const float wvp[16]) const;
		void setBoneCbv(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS va) const;
		void setShadowCbv(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS va) const;

		bool isValid() const { return m_psoSolid != nullptr && m_rootSignature != nullptr; }
		bool isShadow() const { return m_shadow; }

	private:
		ComPtr<ID3D12RootSignature> m_rootSignature;
		ComPtr<ID3D12PipelineState> m_psoSolid;
		ComPtr<ID3D12PipelineState> m_psoWire;
		ComPtr<ID3D12PipelineState> m_psoPoint;
		bool                        m_gbuffer = false;
		bool                        m_shadow  = false;
	};
}
