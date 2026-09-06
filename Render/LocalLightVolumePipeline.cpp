#include "Render/LocalLightVolumePipeline.h"
#include "Render/Camera3D.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/Frustum3f.h"
#include "Render/LocalLightGather.h"
#include "Render/LocalLightGpuList.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "ECS/World.h"

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

        void fillBlendAdditive(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
            rt.BlendEnable                     = TRUE;
            rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
            rt.SrcBlend                        = D3D12_BLEND_ONE;
            rt.DestBlend                       = D3D12_BLEND_ONE;
            rt.BlendOp                         = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha                   = D3D12_BLEND_ZERO;
            rt.DestBlendAlpha                  = D3D12_BLEND_ONE;
            rt.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;
        }
    } // namespace

    bool LocalLightVolumePipeline::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_psoMesh.Reset();
        m_psoFullscreen.Reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "LocalLightVolumePipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 3;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE heightRange{};
        heightRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        heightRange.NumDescriptors                    = 1;
        heightRange.BaseShaderRegister                = 6;
        heightRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[5]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(LocalLightPassConstants) / 4);

        params[kRootSrvTable].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrvTable].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrvTable].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrvTable].DescriptorTable.pDescriptorRanges   = &srvRange;

        params[kRootLightsSrv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootLightsSrv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootLightsSrv].Descriptor.ShaderRegister = 4;

        params[kRootWorldSrv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootWorldSrv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootWorldSrv].Descriptor.ShaderRegister = 5;

        params[kRootHeightSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootHeightSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootHeightSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootHeightSrv].DescriptorTable.pDescriptorRanges   = &heightRange;

        D3D12_STATIC_SAMPLER_DESC heightSamp{};
        heightSamp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        heightSamp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        heightSamp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        heightSamp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        heightSamp.MaxLOD           = D3D12_FLOAT32_MAX;
        heightSamp.ShaderRegister   = 0;
        heightSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 5;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &heightSamp;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (local lights)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "LocalLight RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (local lights)"))
            return false;

        ComPtr<ID3DBlob> vsMesh;
        ComPtr<ID3DBlob> vsFs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/LocalLightVolume.hlsl", "VSMain", "vs_5_0", vsMesh)
            || !compileShaderFromContent("shaders/LocalLightVolume.hlsl", "VSFullscreen", "vs_5_0", vsFs)
            || !compileShaderFromContent("shaders/LocalLightVolume.hlsl", "PSMain", "ps_5_0", ps))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature                  = m_rootSignature.Get();
        pso.PS                              = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask                      = UINT_MAX;
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable   = FALSE;
        pso.DepthStencilState.StencilEnable = FALSE;
        pso.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                = 1;
        pso.RTVFormats[0]                   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.DSVFormat                       = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc                      = { 1, 0 };
        fillBlendAdditive(pso);

        pso.VS                                    = { vsMesh->GetBufferPointer(), vsMesh->GetBufferSize() };
        pso.InputLayout                           = { inputLayout, _countof(inputLayout) };
        pso.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        pso.RasterizerState.FrontCounterClockwise = TRUE;
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoMesh)), "CreateGraphicsPipelineState (local light mesh)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        pso.VS                                    = { vsFs->GetBufferPointer(), vsFs->GetBufferSize() };
        pso.InputLayout                           = { nullptr, 0 };
        pso.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.FrontCounterClockwise = FALSE;
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoFullscreen)), "CreateGraphicsPipelineState (local light fullscreen)"))
        {
            m_psoMesh.Reset();
            m_rootSignature.Reset();
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "LocalLightVolumePipeline: ready");
        return true;
    }

    bool LocalLightVolumePipeline::bindCommon(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const LocalLightPassConstants& cb) const
    {
        ID3D12DescriptorHeap* heap = renderer.lightingHeap();
        const D3D12_GPU_DESCRIPTOR_HANDLE table = renderer.lightingTableGpu();
        if (!heap || table.ptr == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "LocalLightVolumePipeline: no lighting heap");
            return false;
        }

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { heap };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(LocalLightPassConstants) / 4), &cb, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootSrvTable, table);
        cmd->SetGraphicsRootShaderResourceView(kRootLightsSrv, gpuList.lightsGpuVa());
        cmd->SetGraphicsRootShaderResourceView(kRootWorldSrv, gpuList.volumeWorldGpuVa());
        const D3D12_GPU_DESCRIPTOR_HANDLE height = renderer.heightTableGpu();
        if (height.ptr != 0)
            cmd->SetGraphicsRootDescriptorTable(kRootHeightSrv, height);
        return true;
    }

    void LocalLightVolumePipeline::drawInstanced(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const Mesh& volume,
                                                 uint32_t instanceCount, uint32_t baseIndex, const LocalLightPassConstants& cb) const
    {
        if (!cmd || !isValid() || !gpuList.isValid() || !volume.valid() || instanceCount == 0)
            return;

        LocalLightPassConstants local = cb;
        local.baseIndex               = baseIndex;
        local.lightIndex              = 0;
        if (!bindCommon(cmd, renderer, gpuList, local))
            return;

        cmd->SetPipelineState(m_psoMesh.Get());
        volume.drawInstanced(cmd, instanceCount);
    }

    void LocalLightVolumePipeline::drawFullscreenScissor(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList,
                                                         const D3D12_RECT& scissor, uint32_t lightIndex, const LocalLightPassConstants& cb) const
    {
        if (!cmd || !isValid() || !gpuList.isValid())
            return;
        if (scissor.left >= scissor.right || scissor.top >= scissor.bottom)
            return;

        LocalLightPassConstants local = cb;
        local.baseIndex               = lightIndex;
        local.lightIndex              = lightIndex;
        if (!bindCommon(cmd, renderer, gpuList, local))
            return;

        cmd->SetPipelineState(m_psoFullscreen.Get());
        cmd->RSSetScissorRects(1, &scissor);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void LocalLightVolumePipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, LocalLightGpuList& gpuList, const Mesh& sphere,
                                        const Mesh& cone, const Camera3D& camera, const Math::Matrix4f& viewProj, const LightingConstants& lighting) const
    {
        if (!cmd || !isValid() || !gpuList.isValid() || !sphere.valid() || !cone.valid())
            return;
        if (!renderer.debugState().localLights || lighting.lighting < 0.5f)
            return;

        const Frustum3f     frustum(viewProj);
        LocalLightCullInput in{};
        in.frustum    = &frustum;
        in.cameraPos  = camera.GetPosition();
        in.cameraLook = camera.GetLook();
        in.nearZ      = camera.GetNearZ();
        in.viewportW  = renderer.width();
        in.viewportH  = renderer.height();
        in.viewProj   = &viewProj;

        LocalLightDrawLists lists{};
        if (!gatherLocalLights(world, in, lists) || lists.count == 0)
            return;

        gpuList.upload(renderer.frameIndex(), lists);

        LocalLightPassConstants cb{};
        std::memcpy(cb.invViewProj, lighting.invViewProj, sizeof(cb.invViewProj));
        std::memcpy(cb.viewProj, viewProj.m_afEntry, sizeof(cb.viewProj));
        std::memcpy(cb.cameraPos, lighting.cameraPos, sizeof(cb.cameraPos));
        cb.fogDensity = lighting.fogDensity;
        std::memcpy(cb.fogColor, lighting.fogColor, sizeof(cb.fogColor));
        cb.lighting              = lighting.lighting;
        cb.viewportW             = static_cast<float>(renderer.width());
        cb.viewportH             = static_cast<float>(renderer.height());
        cb.heightFogDensity      = lighting.heightFogDensity;
        cb.heightFogFalloff      = lighting.heightFogFalloff;
        cb.heightFogHeight       = lighting.heightFogHeight;
        cb.volumetricFogDensity  = lighting.volumetricFogDensity;
        cb.waterLevel            = lighting.waterLevel;
        cb.volumetricHeight      = lighting.volumetricHeight;
        cb.heightOriginX         = lighting.heightOriginX;
        cb.heightOriginZ         = lighting.heightOriginZ;
        cb.heightCellSize        = lighting.heightCellSize;
        cb.heightWorldSizeX      = lighting.heightWorldSizeX;
        cb.heightWorldSizeZ      = lighting.heightWorldSizeZ;

        drawInstanced(cmd, renderer, gpuList, sphere, lists.pointOutCount, 0, cb);
        drawInstanced(cmd, renderer, gpuList, cone, lists.spotOutCount, lists.pointOutCount, cb);
        const uint32_t insideBase = lists.pointOutCount + lists.spotOutCount;
        for (uint32_t i = 0; i < lists.insideCount; ++i)
            drawFullscreenScissor(cmd, renderer, gpuList, lists.insideScissor[i], insideBase + i, cb);
        const D3D12_RECT sc = renderer.scissor();
        cmd->RSSetScissorRects(1, &sc);
    }

} // namespace Dark
