#include "Render/IblBakePipeline.h"
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

        void FillBakePso(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso, ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps)
        {
            pso                                              = {};
            pso.pRootSignature                               = rs;
            pso.VS                                           = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS                                           = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.SampleMask                                   = UINT_MAX;
            pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pso.RasterizerState.FillMode                     = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode                     = D3D12_CULL_MODE_NONE;
            pso.RasterizerState.DepthClipEnable              = TRUE;
            pso.DepthStencilState.DepthEnable                = FALSE;
            pso.DepthStencilState.StencilEnable              = FALSE;
            pso.InputLayout                                  = { nullptr, 0 };
            pso.PrimitiveTopologyType                        = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets                             = 1;
            pso.RTVFormats[0]                                = DXGI_FORMAT_R16G16B16A16_FLOAT;
            pso.DSVFormat                                    = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc                                   = { 1, 0 };
        }
    } // namespace

    bool IblBakePipeline::create(ID3D12Device* device)
    {
        if (valid() && m_rtvHeap && m_srvHeap)
            return true;
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "IblBakePipeline::create: null device");
            return false;
        }

        m_rootSignature.Reset();
        m_psoEquirect.Reset();
        m_psoIrradiance.Reset();
        m_psoPrefilter.Reset();
        m_rtvHeap.Reset();
        m_srvHeap.Reset();
        m_allocator.Reset();
        m_list.Reset();
        m_rtvStart    = {};
        m_srvCpuStart = {};
        m_srvGpuStart = {};

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = 4;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &samp;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (ibl bake)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "IblBake RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (ibl bake)"))
            return false;

        if (!createPsos(device))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = kRtvCount;
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap (ibl bake RTV)"))
        {
            m_rootSignature.Reset();
            m_psoEquirect.Reset();
            m_psoIrradiance.Reset();
            m_psoPrefilter.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kSrvCount;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap (ibl bake SRV)"))
        {
            m_rtvHeap.Reset();
            m_rootSignature.Reset();
            m_psoEquirect.Reset();
            m_psoIrradiance.Reset();
            m_psoPrefilter.Reset();
            return false;
        }

        m_rtvIncr     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_rtvStart    = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvCpuStart = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpuStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    bool IblBakePipeline::createPsos(ID3D12Device* device)
    {
        D3D_SHADER_MACRO equirectMacros[] = { { "IBL_SRC_EQUIRECT", "1" }, { nullptr, nullptr } };
        D3D_SHADER_MACRO cubeMacros[]     = { { "IBL_SRC_CUBE", "1" }, { nullptr, nullptr } };

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> psEquirect;
        ComPtr<ID3DBlob> psIrr;
        ComPtr<ID3DBlob> psPref;
        // Load-time convolution loops: never SKIP_OPTIMIZATION (Debug hitch).
        if (!compileShaderFromContent("shaders/IblBake.hlsl", "VSMain", "vs_5_0", vs, cubeMacros, true)
            || !compileShaderFromContent("shaders/IblBake.hlsl", "PSEquirect", "ps_5_0", psEquirect, equirectMacros, true)
            || !compileShaderFromContent("shaders/IblBake.hlsl", "PSIrradiance", "ps_5_0", psIrr, cubeMacros, true)
            || !compileShaderFromContent("shaders/IblBake.hlsl", "PSPrefilter", "ps_5_0", psPref, cubeMacros, true))
            return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        FillBakePso(pso, m_rootSignature.Get(), vs.Get(), psEquirect.Get());
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoEquirect)), "CreateGraphicsPipelineState (ibl equirect)"))
            return false;

        FillBakePso(pso, m_rootSignature.Get(), vs.Get(), psIrr.Get());
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoIrradiance)), "CreateGraphicsPipelineState (ibl irradiance)"))
        {
            m_psoEquirect.Reset();
            return false;
        }

        FillBakePso(pso, m_rootSignature.Get(), vs.Get(), psPref.Get());
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoPrefilter)), "CreateGraphicsPipelineState (ibl prefilter)"))
        {
            m_psoEquirect.Reset();
            m_psoIrradiance.Reset();
            return false;
        }
        return true;
    }

    bool IblBakePipeline::resetCommands(ID3D12Device* device)
    {
        if (!device)
            return false;
        m_list.Reset();
        m_allocator.Reset();
        if (FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)), "CreateCommandAllocator (ibl bake)"))
            return false;
        if (FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_list)),
                     "CreateCommandList (ibl bake)"))
        {
            m_allocator.Reset();
            return false;
        }
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE IblBakePipeline::rtvCpu(uint32_t index) const
    {
        DE_ASSERT(index < kRtvCount);
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvStart;
        h.ptr += static_cast<SIZE_T>(index) * m_rtvIncr;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE IblBakePipeline::srvCpu(uint32_t slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvCpuStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE IblBakePipeline::srvGpu(uint32_t slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpuStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

} // namespace Dark
