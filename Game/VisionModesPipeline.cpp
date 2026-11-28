#include "Game/VisionModesPipeline.h"

#include "Core/Log.h"
#include "Game/GameShaders.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

#include <cstring>

namespace Dark
{

    static_assert(VisionModesPipeline::kFrames >= Renderer::kFrameCount, "one LUT slot per frame in flight");

    namespace
    {
        constexpr uint32_t kVisionProfileColor = 0xFFE65100;

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }
    } // namespace

    VisionModesPipeline::~VisionModesPipeline()
    {
        release();
    }

    void VisionModesPipeline::release()
    {
        if (m_lutUpload && m_lutMapped)
            m_lutUpload->Unmap(0, nullptr);
        m_lutMapped = nullptr;
        m_lutGpu    = 0;
        m_lutUpload.Reset();
        m_pso.Reset();
        m_rootSignature.Reset();
    }

    bool VisionModesPipeline::create(ID3D12Device* device, DXGI_FORMAT hdrFormat)
    {
        release();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "VisionModesPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE gbufferRange{};
        gbufferRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRange.NumDescriptors                    = kGBufferSrvs;
        gbufferRange.BaseShaderRegister                = 0;
        gbufferRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(VisionHeatConstants) / 4);

        params[kRootLut].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootLut].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootLut].Descriptor.ShaderRegister = 1;

        params[kRootGBuffer].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootGBuffer].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootGBuffer].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootGBuffer].DescriptorTable.pDescriptorRanges   = &gbufferRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 3;
        rsDesc.pParameters   = params;

        Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (vision modes)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Vision modes root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (vision modes)"))
            return false;

        Microsoft::WRL::ComPtr<ID3DBlob> vs;
        Microsoft::WRL::ComPtr<ID3DBlob> ps;
        if (!addGameShaderRoot() || !compileShaderFromContent("game/shaders/VisionModes.hlsl", "VSMain", ShaderStage::Vertex, vs) ||
            !compileShaderFromContent("game/shaders/VisionModes.hlsl", "PSMain", ShaderStage::Pixel, ps))
        {
            release();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask     = UINT_MAX;
        // out = heat * factor + hdr * (1 - factor); HDR alpha is kept.
        D3D12_RENDER_TARGET_BLEND_DESC& blend = pso.BlendState.RenderTarget[0];
        blend.BlendEnable                     = TRUE;
        blend.SrcBlend                        = D3D12_BLEND_BLEND_FACTOR;
        blend.DestBlend                       = D3D12_BLEND_INV_BLEND_FACTOR;
        blend.BlendOp                         = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha                   = D3D12_BLEND_ZERO;
        blend.DestBlendAlpha                  = D3D12_BLEND_ONE;
        blend.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable   = TRUE;
        pso.DepthStencilState.DepthEnable     = FALSE;
        pso.PrimitiveTopologyType             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                  = 1;
        pso.RTVFormats[0]                     = hdrFormat;
        pso.SampleDesc                        = { 1, 0 };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (vision modes heat)"))
        {
            release();
            return false;
        }

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = static_cast<UINT64>(kLutBytes) * kFrames;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lutUpload)),
                     "CreateCommittedResource (vision modes LUT)") ||
            FailedHr(m_lutUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_lutMapped)), "Map (vision modes LUT)"))
        {
            release();
            return false;
        }
        m_lutGpu = m_lutUpload->GetGPUVirtualAddress();
        std::memset(m_lutMapped, 0, static_cast<size_t>(kLutBytes) * kFrames);
        DE_LOG_INFO(LogCategory::Render, "VisionModesPipeline: ready");
        return true;
    }

    void VisionModesPipeline::drawHeat(ID3D12GraphicsCommandList* cmd, const Renderer& renderer, D3D12_GPU_DESCRIPTOR_HANDLE gbufferTable, D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv,
                                       const VisionHeatConstants& constants, const float* lut, uint32_t count, float strength)
    {
        if (!cmd || !isValid() || gbufferTable.ptr == 0 || hdrRtv.ptr == 0)
            return;
        // The slot is reused kFrames frames later, after the renderer's frame fence.
        const UINT slot = renderer.frameIndex() % kFrames;
        uint8_t*   dst  = m_lutMapped + static_cast<size_t>(slot) * kLutBytes;
        const UINT n    = lut ? (count < kLutEntries ? count : kLutEntries) : 0u;
        std::memset(dst, 0, kLutBytes);
        if (n)
            std::memcpy(dst, lut, static_cast<size_t>(n) * sizeof(float));

        const float    s          = strength > 0.0f ? (strength < 1.0f ? strength : 1.0f) : 0.0f;
        const float    factor[4]  = { s, s, s, s };
        const float    restore[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const GpuScope heat(cmd, "Vision Heat", kVisionProfileColor);
        cmd->OMSetRenderTargets(1, &hdrRtv, FALSE, nullptr);
        cmd->RSSetViewports(1, &renderer.viewport());
        cmd->RSSetScissorRects(1, &renderer.scissor());
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(VisionHeatConstants) / 4), &constants, 0);
        cmd->SetGraphicsRootConstantBufferView(kRootLut, m_lutGpu + static_cast<UINT64>(slot) * kLutBytes);
        cmd->SetGraphicsRootDescriptorTable(kRootGBuffer, gbufferTable);
        cmd->OMSetBlendFactor(factor);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
        cmd->OMSetBlendFactor(restore);
    }

} // namespace Dark
