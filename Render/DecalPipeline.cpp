#include "Render/DecalPipeline.h"
#include "Render/EnhancedBarrierEmitter.h"
#include "Render/DecalGpuList.h"
#include "Render/DepthState.h"
#include "Render/Mesh.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

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
    } // namespace

    void DecalPipeline::reset()
    {
        m_rootSignature.Reset();
        m_psoOutside.Reset();
        m_psoInside.Reset();
        m_uavHeap.Reset();
        if (m_cbUpload && m_cbMapped)
            m_cbUpload->Unmap(0, nullptr);
        m_cbUpload.Reset();
        m_cbMapped  = nullptr;
        m_cbGpu     = 0;
        m_srvIncr   = 0;
        m_frameSlot = 0;
        m_albedoRes = nullptr;
        m_attribRes = nullptr;
        m_uavsReady = false;
    }

    bool DecalPipeline::isValid() const
    {
        return m_rootSignature && m_psoOutside && m_psoInside && m_uavHeap && m_cbUpload && m_cbMapped;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DecalPipeline::cpuUav(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_uavHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    bool DecalPipeline::create(ID3D12Device* device)
    {
        reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "DecalPipeline::create: null device");
            return false;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (FailedHr(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "CheckFeatureSupport D3D12_OPTIONS"))
            return false;
        // Created at D3D_FEATURE_LEVEL_11_0. That level does not imply ROVsSupported.
        if (!options.ROVsSupported)
        {
            DE_LOG_WARN(LogCategory::Render, "DecalPipeline: ROVsSupported is false; decals disabled");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE depthRange{};
        depthRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors                    = 1;
        depthRange.BaseShaderRegister                = 0;
        depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE texRange{};
        texRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texRange.NumDescriptors                    = 2;
        texRange.BaseShaderRegister                = 1;
        texRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors                    = kUavCount;
        uavRange.BaseShaderRegister                = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[5]{};
        params[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootCbv].Descriptor.ShaderRegister = 0;

        params[kRootDepth].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootDepth].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootDepth].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootDepth].DescriptorTable.pDescriptorRanges   = &depthRange;

        params[kRootTextures].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootTextures].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootTextures].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootTextures].DescriptorTable.pDescriptorRanges   = &texRange;

        params[kRootUav].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootUav].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootUav].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootUav].DescriptorTable.pDescriptorRanges   = &uavRange;

        params[kRootInstances].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootInstances].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootInstances].Descriptor.ShaderRegister = 3;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 5;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &samp;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (decals)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Decal RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (decals)"))
            return false;

        ComPtr<ID3DBlob> vsMesh;
        ComPtr<ID3DBlob> vsFs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Decal.hlsl", "VSMain", ShaderStage::Vertex, vsMesh)
            || !compileShaderFromContent("shaders/Decal.hlsl", "VSFullscreen", ShaderStage::Vertex, vsFs)
            || !compileShaderFromContent("shaders/Decal.hlsl", "PSMain", ShaderStage::Pixel, ps))
        {
            reset();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature                        = m_rootSignature.Get();
        pso.VS                                    = { vsMesh->GetBufferPointer(), vsMesh->GetBufferSize() };
        pso.PS                                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask                            = UINT_MAX;
        pso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode              = D3D12_CULL_MODE_FRONT;
        pso.RasterizerState.FrontCounterClockwise = TRUE;
        pso.RasterizerState.DepthClipEnable       = TRUE;
        pso.DepthStencilState.DepthEnable         = TRUE;
        pso.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc           = sceneDepthFunc();
        pso.DepthStencilState.StencilEnable       = FALSE;
        pso.InputLayout                           = { inputLayout, _countof(inputLayout) };
        pso.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                      = 0;
        pso.DSVFormat                             = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc                            = { 1, 0 };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoOutside)), "CreateGraphicsPipelineState (decal outside)"))
        {
            reset();
            return false;
        }

        pso.VS                                    = { vsFs->GetBufferPointer(), vsFs->GetBufferSize() };
        pso.InputLayout                           = { nullptr, 0 };
        pso.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.FrontCounterClockwise = FALSE;
        pso.DepthStencilState.DepthEnable         = FALSE;
        pso.DSVFormat                             = DXGI_FORMAT_UNKNOWN;
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoInside)), "CreateGraphicsPipelineState (decal inside)"))
        {
            reset();
            return false;
        }

        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC uavHeap{};
        uavHeap.NumDescriptors = kUavCount;
        uavHeap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&uavHeap, IID_PPV_ARGS(&m_uavHeap)), "CreateDescriptorHeap decal UAV"))
        {
            reset();
            return false;
        }

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = static_cast<UINT64>(kCbBytes) * kFrameCount;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbUpload)),
                     "CreateCommittedResource decal pass CB"))
        {
            reset();
            return false;
        }
        if (FailedHr(m_cbUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped)), "Map decal pass CB"))
        {
            reset();
            return false;
        }
        std::memset(m_cbMapped, 0, static_cast<size_t>(kCbBytes) * kFrameCount);
        m_cbGpu = m_cbUpload->GetGPUVirtualAddress();

        DE_LOG_INFO(LogCategory::Render, "DecalPipeline: ready (ROV yes, cap {})", DecalGpuList::kCapacity);
        return true;
    }

    bool DecalPipeline::syncTargets(ID3D12Device* device, ID3D12Resource* albedo, ID3D12Resource* attrib)
    {
        if (!device || !m_uavHeap)
            return false;
        if (!albedo || !attrib)
        {
            m_albedoRes = nullptr;
            m_attribRes = nullptr;
            m_uavsReady = false;
            return false;
        }
        if (m_uavsReady && albedo == m_albedoRes && attrib == m_attribRes)
            return true;

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format        = DXGI_FORMAT_R32_UINT;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(albedo, nullptr, &desc, cpuUav(0));
        device->CreateUnorderedAccessView(attrib, nullptr, &desc, cpuUav(1));
        m_albedoRes = albedo;
        m_attribRes = attrib;
        m_uavsReady = true;
        return true;
    }

    void DecalPipeline::uploadPass(uint32_t frameIndex, const DecalPassConstants& cb)
    {
        if (!m_cbMapped)
            return;
        m_frameSlot = frameIndex % kFrameCount;
        std::memcpy(m_cbMapped + static_cast<size_t>(m_frameSlot) * kCbBytes, &cb, sizeof(cb));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DecalPipeline::albedoUavCpu() const
    {
        return m_uavsReady ? cpuUav(0) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DecalPipeline::attribUavCpu() const
    {
        return m_uavsReady ? cpuUav(1) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    bool DecalPipeline::bind(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, D3D12_GPU_VIRTUAL_ADDRESS instances, DecalDefId id) const
    {
        if (!cmd || !isValid() || instances == 0 || static_cast<UINT>(id) >= static_cast<UINT>(DecalDefId::Count))
            return false;
        const D3D12_GPU_DESCRIPTOR_HANDLE textures = tables.textures[static_cast<UINT>(id)];
        if (tables.depth.ptr == 0 || tables.uavs.ptr == 0 || textures.ptr == 0)
            return false;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, m_cbGpu + static_cast<UINT64>(m_frameSlot) * kCbBytes);
        cmd->SetGraphicsRootDescriptorTable(kRootDepth, tables.depth);
        cmd->SetGraphicsRootDescriptorTable(kRootTextures, textures);
        cmd->SetGraphicsRootDescriptorTable(kRootUav, tables.uavs);
        cmd->SetGraphicsRootShaderResourceView(kRootInstances, instances);
        return true;
    }

    void DecalPipeline::drawOutside(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, const Mesh& cube, D3D12_GPU_VIRTUAL_ADDRESS instances, uint32_t count, DecalDefId id) const
    {
        if (!cmd || count == 0 || !cube.valid() || !bind(cmd, tables, instances, id))
            return;
        cmd->SetPipelineState(m_psoOutside.Get());
        cube.drawInstanced(cmd, count);
    }

    void DecalPipeline::drawInside(ID3D12GraphicsCommandList* cmd, const DecalTables& tables, D3D12_GPU_VIRTUAL_ADDRESS instances, uint32_t count, DecalDefId id) const
    {
        if (!cmd || count == 0 || !bind(cmd, tables, instances, id))
            return;
        cmd->SetPipelineState(m_psoInside.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, count, 0, 0);
    }

    void DecalPipeline::uavBarrier(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_albedoRes || !m_attribRes)
            return;
        EnhancedBarrierList bars(cmd);
        bars.uav(m_albedoRes, Rg::ResourceKind::Texture);
        bars.uav(m_attribRes, Rg::ResourceKind::Texture);
    }

} // namespace Dark
