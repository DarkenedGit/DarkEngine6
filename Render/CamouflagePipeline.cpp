#include "Render/CamouflagePipeline.h"
#include "Render/Profile.h"

#include "Assets/Model.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/DepthState.h"
#include "Render/GpuModel.h"
#include "Render/GraphTargets.h"
#include "Render/GpuResourceCache.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Render/SkinningUploadRing.h"

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

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            m.store(dst);
        }

        void fillPsoCommon(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso, ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT colorFormat)
        {
            pso                                              = {};
            pso.pRootSignature                               = rs;
            pso.VS                                           = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS                                           = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.SampleMask                                   = UINT_MAX;
            pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pso.RasterizerState.FillMode                     = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode                     = D3D12_CULL_MODE_BACK;
            pso.RasterizerState.FrontCounterClockwise        = TRUE;
            pso.RasterizerState.DepthClipEnable              = TRUE;
            pso.DepthStencilState.DepthEnable                = TRUE;
            pso.DepthStencilState.DepthWriteMask             = D3D12_DEPTH_WRITE_MASK_ALL;
            pso.DepthStencilState.DepthFunc                  = sceneDepthFunc();
            pso.PrimitiveTopologyType                        = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets                             = 1;
            pso.RTVFormats[0]                                = colorFormat;
            pso.DSVFormat                                    = DXGI_FORMAT_D32_FLOAT;
            pso.SampleDesc                                   = { 1, 0 };
        }
    } // namespace

    CamouflagePipeline::~CamouflagePipeline()
    {
        release();
    }

    void CamouflagePipeline::release()
    {
        if (m_targets)
            m_targets->unregisterTarget(m_copy);
        m_copy    = {};
        m_targets = nullptr;
        m_rootSignature.Reset();
        m_psoSkinned.Reset();
        m_psoStatic.Reset();
        m_srvHeap.Reset();
        m_srvGpu = {};
    }

    bool CamouflagePipeline::create(ID3D12Device* device, DXGI_FORMAT colorFormat)
    {
        release();
        m_format = colorFormat;
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "CamouflagePipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE sceneRange{};
        sceneRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneRange.NumDescriptors                    = 1;
        sceneRange.BaseShaderRegister                = 0;
        sceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(CamouflageConstants) / 4);

        params[kRootSceneSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSceneSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSceneSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSceneSrv].DescriptorTable.pDescriptorRanges   = &sceneRange;

        params[kRootBoneCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootBoneCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootBoneCbv].Descriptor.ShaderRegister = 1;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = 0.0f;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 3;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &samp;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (camouflage)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Camouflage root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (camouflage)"))
            return false;

        ComPtr<ID3DBlob> vsSkinned;
        ComPtr<ID3DBlob> vsStatic;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Camouflage.hlsl", "VSMain", ShaderStage::Vertex, vsSkinned)
            || !compileShaderFromContent("shaders/Camouflage.hlsl", "VSMainStatic", ShaderStage::Vertex, vsStatic)
            || !compileShaderFromContent("shaders/Camouflage.hlsl", "PSMain", ShaderStage::Pixel, ps))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC skinnedLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_INPUT_ELEMENT_DESC staticLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        fillPsoCommon(pso, m_rootSignature.Get(), vsSkinned.Get(), ps.Get(), colorFormat);
        pso.InputLayout = { skinnedLayout, _countof(skinnedLayout) };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoSkinned)), "CreateGraphicsPipelineState (camouflage skinned)"))
        {
            m_rootSignature.Reset();
            return false;
        }
        fillPsoCommon(pso, m_rootSignature.Get(), vsStatic.Get(), ps.Get(), colorFormat);
        pso.InputLayout = { staticLayout, _countof(staticLayout) };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoStatic)), "CreateGraphicsPipelineState (camouflage static)"))
        {
            m_rootSignature.Reset();
            m_psoSkinned.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap (camouflage scene)"))
        {
            m_rootSignature.Reset();
            m_psoSkinned.Reset();
            m_psoStatic.Reset();
            return false;
        }
        m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        DE_LOG_INFO(LogCategory::Render, "CamouflagePipeline: ready");
        return true;
    }

    bool CamouflagePipeline::ensureCopy(Renderer& renderer)
    {
        // Registered on first use; Renderer::resize keeps it at the output size (no lazy resize wait).
        if (!m_copy.id.valid())
        {
            GraphTargets* targets = renderer.targets();
            if (!targets || !targets->valid() || !m_srvHeap)
                return false;
            m_targets = targets;
            m_copy    = targets->registerTarget("DE.Camouflage.SceneCopy", copyTargetDesc(m_format, D3D12_RESOURCE_STATE_COMMON));
            if (!m_copy.id.valid())
                return false;
            m_copy.generation = 0; // write the static SRV below
        }
        // The table's one SRV is static. A new generation only happens inside Renderer::resize's
        // drained window, so rewriting it here races no in-flight list.
        if (m_targets->refresh(m_copy))
            m_targets->device()->CopyDescriptorsSimple(1, m_srvHeap->GetCPUDescriptorHandleForHeapStart(), m_targets->srv(m_copy.id),
                                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return m_targets->resource(m_copy.id) != nullptr;
    }

    bool CamouflagePipeline::captureScene(ID3D12GraphicsCommandList* cmd, Renderer& renderer)
    {
        if (!cmd || !isValid() || !m_targets || !m_targets->resource(m_copy.id))
            return false;
        ID3D12Resource* hdr = renderer.hdr();
        if (!hdr)
            return false;

        cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_targets->transition(cmd, m_copy, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(m_targets->resource(m_copy.id), hdr);
        m_targets->transition(cmd, m_copy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer.bindHdr(true);
        return true;
    }

    bool CamouflagePipeline::bindDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, ID3D12PipelineState* pso) const
    {
        if (!cmd || !pso || !m_rootSignature || !m_srvHeap)
            return false;
        (void)renderer;
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootDescriptorTable(kRootSceneSrv, m_srvGpu);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        return true;
    }

    bool CamouflagePipeline::drawModel(ID3D12GraphicsCommandList* cmd, Renderer& renderer, GpuResourceCache& gpu, SkinningUploadRing& ring, const Model& model,
                                       const AnimPose* pose, const Math::Matrix4f& world, const Math::Matrix4f& viewProj, const Math::Vector3f& cameraPos,
                                       float time, DebugFill fill)
    {
        (void)fill;
        GpuModel* gm = gpu.model(model.id);
        if (!cmd || !isValid() || !gm || !gm->hasOpaque())
            return false;
        if (!ensureCopy(renderer))
            return false;
        const GpuScope camouflage(cmd, "Camouflage", ProfileColor::Camouflage);
        // Depth is writable after this. A later skipped part does not undo that.
        if (!captureScene(cmd, renderer))
            return false;

        CamouflageConstants cb{};
        cb.cameraPos[0]  = cameraPos.x;
        cb.cameraPos[1]  = cameraPos.y;
        cb.cameraPos[2]  = cameraPos.z;
        cb.time          = time;
        cb.invSize[0]    = 1.0f / static_cast<float>(Dark::Math::Max(m_targets->width(m_copy.id), 1u));
        cb.invSize[1]    = 1.0f / static_cast<float>(Dark::Math::Max(m_targets->height(m_copy.id), 1u));
        cb.distort       = 0.028f;
        cb.rimStrength   = 1.15f;

        bool boundSkinned = false;
        bool boundStatic  = false;
        for (const GpuModel::Part& part : gm->opaque())
        {
            if (!part.mesh.valid())
                continue;
            const Math::Matrix4f w = part.localToRoot * world;
            copyMatrix(cb.worldViewProj, w * viewProj);
            copyMatrix(cb.world, w);

            if (part.skinned)
            {
                if (!pose || !m_psoSkinned)
                    continue;
                const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(*pose);
                if (bones == 0)
                    continue;
                if (!boundSkinned)
                {
                    if (!bindDraw(cmd, renderer, m_psoSkinned.Get()))
                        return true;
                    boundSkinned = true;
                    boundStatic  = false;
                }
                cmd->SetGraphicsRootConstantBufferView(kRootBoneCbv, bones);
                cmd->SetGraphicsRoot32BitConstants(kRootConstants, sizeof(CamouflageConstants) / 4, &cb, 0);
                part.mesh.draw(cmd);
            }
            else
            {
                if (!m_psoStatic)
                    continue;
                if (!boundStatic)
                {
                    if (!bindDraw(cmd, renderer, m_psoStatic.Get()))
                        return true;
                    boundStatic  = true;
                    boundSkinned = false;
                }
                cmd->SetGraphicsRoot32BitConstants(kRootConstants, sizeof(CamouflageConstants) / 4, &cb, 0);
                part.mesh.draw(cmd);
            }
        }
        return true;
    }

} // namespace Dark
