#include "Render/SkinnedMeshPipeline.h"
#include "Render/PsoUtil.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <d3dcompiler.h>

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
	} // namespace

	bool SkinnedMeshPipeline::create(ID3D12Device* device, SkinnedMeshPass pass, DXGI_FORMAT colorFormat)
	{
		m_rootSignature.Reset();
		m_psoSolid.Reset();
		m_psoWire.Reset();
		m_psoPoint.Reset();
		m_gbuffer = pass == SkinnedMeshPass::GBuffer;
		m_shadow  = pass == SkinnedMeshPass::Shadow;
		const bool transparent = pass == SkinnedMeshPass::ForwardTransparent;

		if (!device)
		{
			DE_LOG_ERROR(LogCategory::Render, "SkinnedMeshPipeline::create: null device");
			return false;
		}

		D3D12_DESCRIPTOR_RANGE srvRange{};
		srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors                    = m_gbuffer ? 1u : 2u;
		srvRange.BaseShaderRegister                = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParams[4]{};
		rootParams[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParams[kRootConstants].ShaderVisibility         = m_shadow ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_ALL;
		rootParams[kRootConstants].Constants.ShaderRegister = 0;
		rootParams[kRootConstants].Constants.Num32BitValues =
			m_shadow ? 16u
					 : (m_gbuffer ? static_cast<UINT>(sizeof(MeshGBufferConstants) / 4)
								  : static_cast<UINT>(sizeof(MeshFrameConstants) / 4));

		rootParams[kRootAlbedoSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[kRootAlbedoSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParams[kRootAlbedoSrv].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[kRootAlbedoSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

		rootParams[kRootShadowCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[kRootShadowCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParams[kRootShadowCbv].Descriptor.ShaderRegister = 1;

		rootParams[kRootBoneCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[kRootBoneCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParams[kRootBoneCbv].Descriptor.ShaderRegister = 2;

		D3D12_STATIC_SAMPLER_DESC samps[2]{};
		samps[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samps[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samps[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samps[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samps[0].MaxLOD           = D3D12_FLOAT32_MAX;
		samps[0].ShaderRegister   = 0;
		samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samps[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samps[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samps[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samps[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samps[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samps[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		samps[1].MaxLOD           = D3D12_FLOAT32_MAX;
		samps[1].ShaderRegister   = 1;
		samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rsDesc{};
		rsDesc.NumParameters     = 4;
		rsDesc.pParameters       = rootParams;
		rsDesc.NumStaticSamplers = m_shadow ? 0u : (m_gbuffer ? 1u : 2u);
		rsDesc.pStaticSamplers   = samps;
		rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> rsBlob;
		ComPtr<ID3DBlob> rsErr;
		if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (skinned)"))
		{
			if (rsErr)
				DE_LOG_ERROR(LogCategory::Render, "Skinned root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
			return false;
		}
		if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
				"CreateRootSignature (skinned)"))
			return false;

		const char* shader = m_shadow ? "shaders/SkinnedShadowDepth.hlsl"
									  : (m_gbuffer ? "shaders/SkinnedMeshGBuffer.hlsl" : "shaders/SkinnedMesh.hlsl");
		ComPtr<ID3DBlob> vs;
		ComPtr<ID3DBlob> ps;
		if (!compileShaderFromContent(shader, "VSMain", "vs_5_0", vs))
			return false;
		if (!m_shadow && !compileShaderFromContent(shader, "PSMain", "ps_5_0", ps))
			return false;

		D3D12_INPUT_ELEMENT_DESC colorLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		D3D12_INPUT_ELEMENT_DESC shadowLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
		if (!m_shadow)
			psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
		psoDesc.RasterizerState.DepthClipEnable       = TRUE;
		psoDesc.DepthStencilState.DepthEnable    = TRUE;
		psoDesc.DepthStencilState.DepthWriteMask = (transparent && !m_shadow) ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.PrimitiveTopologyType            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc                       = { 1, 0 };

		if (m_shadow)
		{
			psoDesc.RasterizerState.DepthBias            = 4000;
			psoDesc.RasterizerState.SlopeScaledDepthBias = 2.5f;
			psoDesc.InputLayout                          = { shadowLayout, _countof(shadowLayout) };
			psoDesc.NumRenderTargets                     = 0;
			if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoSolid)), "CreateGraphicsPipelineState (skinned shadow)"))
			{
				m_rootSignature.Reset();
				return false;
			}
			DE_LOG_INFO(LogCategory::Render, "SkinnedMeshPipeline: ready (shadow)");
			return true;
		}

		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.InputLayout = { colorLayout, _countof(colorLayout) };
		if (transparent)
		{
			D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
			rt.BlendEnable           = TRUE;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOp               = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
			rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
		}
		if (m_gbuffer)
		{
			psoDesc.NumRenderTargets = 3;
			psoDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.RTVFormats[1]    = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.RTVFormats[2]    = DXGI_FORMAT_R16G16_FLOAT;
			psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		}
		else
		{
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0]    = transparent ? colorFormat : DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		if (!createFillVariantPsos(device, psoDesc, m_psoSolid, m_psoWire, m_psoPoint))
		{
			m_rootSignature.Reset();
			return false;
		}
		const char* passName = m_gbuffer ? "GBuffer" : (transparent ? "ForwardTransparent" : "Forward");
		DE_LOG_INFO(LogCategory::Render, "SkinnedMeshPipeline: ready ({})", passName);
		return true;
	}

	void SkinnedMeshPipeline::bind(ID3D12GraphicsCommandList* cmd, DebugFill fill) const
	{
		if (!cmd || !m_rootSignature)
			return;
		ID3D12PipelineState* pso = m_shadow ? m_psoSolid.Get()
											: selectFillPso(fill, m_psoSolid.Get(), m_psoWire.Get(), m_psoPoint.Get());
		if (!pso)
			return;
		cmd->SetGraphicsRootSignature(m_rootSignature.Get());
		cmd->SetPipelineState(pso);
	}

	void SkinnedMeshPipeline::setConstants(ID3D12GraphicsCommandList* cmd, const MeshFrameConstants& constants) const
	{
		if (!cmd || !m_rootSignature)
			return;
		cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(MeshFrameConstants) / 4), &constants, 0);
	}

	void SkinnedMeshPipeline::setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshGBufferConstants& constants) const
	{
		if (!cmd || !m_rootSignature)
			return;
		cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(MeshGBufferConstants) / 4), &constants, 0);
	}

	void SkinnedMeshPipeline::setWvp(ID3D12GraphicsCommandList* cmd, const float wvp[16]) const
	{
		if (!cmd || !m_rootSignature || !wvp)
			return;
		cmd->SetGraphicsRoot32BitConstants(kRootConstants, 16, wvp, 0);
	}

	void SkinnedMeshPipeline::setBoneCbv(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS va) const
	{
		if (!cmd || !m_rootSignature || va == 0)
			return;
		cmd->SetGraphicsRootConstantBufferView(kRootBoneCbv, va);
	}

	void SkinnedMeshPipeline::setShadowCbv(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS va) const
	{
		if (!cmd || !m_rootSignature || va == 0)
			return;
		cmd->SetGraphicsRootConstantBufferView(kRootShadowCbv, va);
	}
}
