#include "Render/WalkabilityDebug.h"

#include "Core/Log.h"
#include "Render/DebugRenderState.h"
#include "Render/DepthState.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

#include <d3dcompiler.h>
#include <cstring>

namespace Dark
{
    namespace
    {
        struct WalkConstants
        {
            float worldViewProj[16];
        };

        static_assert(sizeof(WalkConstants) == 16 * sizeof(float), "walk mesh root constant size");

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heapType;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = size;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&res)), "CreateCommittedResource walk mesh"))
                return {};
            return res;
        }
    } // namespace

    void WalkabilityDebug::releaseMesh(Renderer& renderer)
    {
        if (m_vb)
            renderer.deferRelease(m_vb.Get());
        if (m_ib)
            renderer.deferRelease(m_ib.Get());
        m_vb.Reset();
        m_ib.Reset();
        m_vbv = {};
        m_ibv = {};
        m_indexCount = 0;
    }

    bool WalkabilityDebug::ensurePipeline(Renderer& renderer)
    {
        if (m_pso)
            return true;
        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "WalkabilityDebug: no device");
            return false;
        }

        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rootParam.Constants.ShaderRegister = 0;
        rootParam.Constants.RegisterSpace  = 0;
        rootParam.Constants.Num32BitValues = static_cast<UINT>(sizeof(WalkConstants) / 4);

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters   = &rootParam;
        rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "Walk mesh D3D12SerializeRootSignature"))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "Walk mesh CreateRootSignature"))
            return false;

        Microsoft::WRL::ComPtr<ID3DBlob> vs;
        Microsoft::WRL::ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Walkability.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/Walkability.hlsl", "PSMain", "ps_5_0", ps))
            return false;

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = m_root.Get();
        psoDesc.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.SampleMask     = UINT_MAX;

        D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
        rt.BlendEnable                     = TRUE;
        rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
        rt.SrcBlend                        = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend                       = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp                         = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha                   = D3D12_BLEND_ONE;
        rt.DestBlendAlpha                  = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;

        psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable       = TRUE;
        psoDesc.RasterizerState.SlopeScaledDepthBias  = 1.5f;

        psoDesc.DepthStencilState.DepthEnable    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc      = sceneDepthFuncGreaterEqual();

        psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = renderer.sceneColorFormat();
        psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)), "Walk mesh CreateGraphicsPipelineState"))
        {
            m_root.Reset();
            return false;
        }
        DE_LOG_INFO(LogCategory::Render, "WalkabilityDebug: pipeline ready");
        return true;
    }

    void WalkabilityDebug::sync(Renderer& renderer, const DebugMeshSource& source)
    {
        if (source.serial == 0 || !source.build)
        {
            if (m_indexCount > 0 || m_serial != 0)
                releaseMesh(renderer);
            m_serial = 0;
            return;
        }
        if (source.serial == m_serial)
            return;

        DebugColorMesh cpu;
        if (!source.build(source.user, cpu) || cpu.indices.empty())
        {
            releaseMesh(renderer);
            m_serial = source.serial;
            return;
        }

        ID3D12Device* device = renderer.device();
        const uint64_t                         vbBytes  = cpu.vertices.size() * sizeof(DebugColorVertex);
        const uint64_t ibBytes = cpu.indices.size() * sizeof(uint32_t);
        Microsoft::WRL::ComPtr<ID3D12Resource> vb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        Microsoft::WRL::ComPtr<ID3D12Resource> ib = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadVb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadIb = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!vb || !ib || !uploadVb || !uploadIb)
        {
            m_serial = source.serial;
            return;
        }

        void* mapped = nullptr;
        if (FailedHr(uploadVb->Map(0, nullptr, &mapped), "Map walk mesh VB"))
        {
            m_serial = source.serial;
            return;
        }
        std::memcpy(mapped, cpu.vertices.data(), static_cast<size_t>(vbBytes));
        uploadVb->Unmap(0, nullptr);
        if (FailedHr(uploadIb->Map(0, nullptr, &mapped), "Map walk mesh IB"))
        {
            m_serial = source.serial;
            return;
        }
        std::memcpy(mapped, cpu.indices.data(), static_cast<size_t>(ibBytes));
        uploadIb->Unmap(0, nullptr);

        if (!renderer.submitBufferCopies(vb.Get(), uploadVb.Get(), vbBytes, ib.Get(), uploadIb.Get(), ibBytes))
        {
            m_serial = source.serial;
            return;
        }
        renderer.deferRelease(uploadVb.Get());
        renderer.deferRelease(uploadIb.Get());

        releaseMesh(renderer);
        m_vb = vb;
        m_ib = ib;
        m_indexCount = static_cast<uint32_t>(cpu.indices.size());
        m_vbv.BufferLocation = m_vb->GetGPUVirtualAddress();
        m_vbv.StrideInBytes  = sizeof(DebugColorVertex);
        m_vbv.SizeInBytes    = static_cast<UINT>(vbBytes);
        m_ibv.BufferLocation = m_ib->GetGPUVirtualAddress();
        m_ibv.Format         = DXGI_FORMAT_R32_UINT;
        m_ibv.SizeInBytes    = static_cast<UINT>(ibBytes);
        m_serial             = source.serial;
        DE_LOG_INFO(LogCategory::Render, "Walkability mesh: {} quads", m_indexCount / 6);
    }

    void WalkabilityDebug::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const DebugMeshSource& source, const Math::Matrix4f& viewProj)
    {
        if (!renderer.debugState().showWalkability)
        {
            m_pipelineFailed = false;
            return;
        }
        if (!cmd || m_pipelineFailed)
            return;
        if (!ensurePipeline(renderer))
        {
            m_pipelineFailed = true;
            m_root.Reset();
            m_pso.Reset();
            DE_LOG_ERROR(LogCategory::Render, "WalkabilityDebug: pipeline failed; the walk overlay is off until it is toggled");
            return;
        }
        sync(renderer, source);
        if (!m_pso || m_indexCount == 0)
            return;

        const GpuScope scope(cmd, "Walkable Mesh", ProfileColor::DebugOverlay);
        WalkConstants constants{};
        viewProj.store(constants.worldViewProj);
        cmd->SetGraphicsRootSignature(m_root.Get());
        cmd->SetPipelineState(m_pso.Get());
        cmd->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(sizeof(WalkConstants) / 4), &constants, 0);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &m_vbv);
        cmd->IASetIndexBuffer(&m_ibv);
        cmd->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
    }
} // namespace Dark
