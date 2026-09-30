#include "Render/CloudVolumePipeline.h"

#include "Core/Log.h"
#include "ECS/World.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Sky/CloudVolume.h"

#include <cstring>
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

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }
    } // namespace

    bool CloudVolumePipeline::create(ID3D12Device* device, DXGI_FORMAT colorFormat)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        m_depthHeap.Reset();
        m_depthGpu = {};
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "CloudVolumePipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE depthRange{};
        depthRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors                    = 1;
        depthRange.BaseShaderRegister                = 0;
        depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(CloudVolumePassConstants) / 4);

        params[kRootDepthSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootDepthSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootDepthSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootDepthSrv].DescriptorTable.pDescriptorRanges   = &depthRange;

        params[kRootVolumes].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootVolumes].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootVolumes].Descriptor.ShaderRegister = 1;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 3;
        rsDesc.pParameters   = params;
        rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (clouds)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Cloud volume root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (clouds)"))
        {
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/CloudVolume.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/CloudVolume.hlsl", "PSMain", "ps_5_0", ps))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask     = UINT_MAX;
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable    = FALSE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.StencilEnable  = FALSE;
        pso.InputLayout           = { nullptr, 0 };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets      = 1;
        pso.RTVFormats[0]         = colorFormat;
        pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc            = { 1, 0 };

        D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
        rt.BlendEnable           = TRUE;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        rt.SrcBlend              = D3D12_BLEND_ONE;
        rt.DestBlend             = D3D12_BLEND_SRC_ALPHA;
        rt.BlendOp               = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha         = D3D12_BLEND_ZERO;
        rt.DestBlendAlpha        = D3D12_BLEND_SRC_ALPHA;
        rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;

        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (clouds)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_depthHeap)), "CreateDescriptorHeap (cloud depth)"))
        {
            m_rootSignature.Reset();
            m_pso.Reset();
            return false;
        }
        m_depthGpu = m_depthHeap->GetGPUDescriptorHandleForHeapStart();

        DE_LOG_INFO(LogCategory::Render, "CloudVolumePipeline: ready");
        return true;
    }

    void CloudVolumePipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, CloudVolumeGpuList& gpuList, const Camera3D& camera,
                                   const Math::Matrix4f& viewProj, const CloudVolumeFrame& frame) const
    {
        if (!cmd || !isValid() || !gpuList.isValid())
            return;
        if (!renderer.debugState().clouds)
            return;
        if (!renderer.hasGBuffer() || renderer.depthSrvCpu().ptr == 0)
            return;

        const Frustum3f frustum(camera.GetCullViewProj());
        CloudVolumeDrawList lists{};
        gatherCloudVolumes(world, &frustum, lists);
        if (lists.count == 0)
            return;

        gpuList.upload(renderer.frameIndex(), lists);

        ID3D12Device* device = renderer.device();
        if (!device)
            return;
        device->CopyDescriptorsSimple(1, m_depthHeap->GetCPUDescriptorHandleForHeapStart(), renderer.depthSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        CloudVolumePassConstants cb{};
        copyMatrix(cb.invViewProj, viewProj.Inverse());
        const Math::Vector3f cam = camera.GetPosition();
        cb.cameraPos[0]          = cam.x;
        cb.cameraPos[1]          = cam.y;
        cb.cameraPos[2]          = cam.z;
        cb.time                  = frame.time;
        Math::Vector3f sun       = frame.sunDir;
        if (sun.MagnitudeSqrd() > 1.0e-8f)
            sun.Normalize();
        cb.sunDir[0]        = sun.x;
        cb.sunDir[1]        = sun.y;
        cb.sunDir[2]        = sun.z;
        cb.volumeCount      = static_cast<float>(lists.count);
        cb.sunColor[0]      = frame.sunColor.x;
        cb.sunColor[1]      = frame.sunColor.y;
        cb.sunColor[2]      = frame.sunColor.z;
        cb.nearZ            = camera.GetNearZ();
        cb.ambientColor[0]  = frame.ambientColor.x;
        cb.ambientColor[1]  = frame.ambientColor.y;
        cb.ambientColor[2]  = frame.ambientColor.z;
        CloudLodSettings lod = frame.lod;
        sanitizeCloudLod(lod);
        cb.lodEnabled    = lod.enabled ? 1.0f : 0.0f;
        cb.lodDetailDist = lod.detailDistance;
        cb.lodFadeDist   = lod.fadeDistance;
        cb.lodNearStep   = lod.nearStep;
        cb.lodFarStep    = lod.farStep;
        cb.lodMaxNear    = static_cast<float>(lod.maxNearSteps);
        cb.lodMaxFar     = static_cast<float>(lod.maxFarSteps);
        cb.lodNearLight  = static_cast<float>(lod.nearLightSteps);
        cb.lodFarLight   = static_cast<float>(lod.farLightSteps);

        ID3D12DescriptorHeap* heaps[] = { m_depthHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, sizeof(CloudVolumePassConstants) / 4, &cb, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootDepthSrv, m_depthGpu);
        cmd->SetGraphicsRootShaderResourceView(kRootVolumes, gpuList.volumesGpuVa());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
