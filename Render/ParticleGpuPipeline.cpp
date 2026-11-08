#include "Render/ParticleGpuPipeline.h"
#include "Render/DepthState.h"
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

        void fillBlend(D3D12_RENDER_TARGET_BLEND_DESC& rt, bool additive)
        {
            rt.BlendEnable           = TRUE;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            rt.BlendOp               = D3D12_BLEND_OP_ADD;
            rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
            if (additive)
            {
                rt.DestBlend      = D3D12_BLEND_ONE;
                rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ONE;
            }
            else
            {
                rt.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
                rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            }
        }

    } // namespace

    bool ParticleGpuPipeline::create(ID3D12Device* device, DXGI_FORMAT colorFormat)
    {
        destroy();
        if (!device)
            return false;

        D3D12_ROOT_PARAMETER simParams[4]{};
        simParams[kSimCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        simParams[kSimCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        simParams[kSimCb].Descriptor.ShaderRegister = 0;
        simParams[kSimPrev].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        simParams[kSimPrev].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        simParams[kSimPrev].Descriptor.ShaderRegister = 0;
        simParams[kSimCurr].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        simParams[kSimCurr].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        simParams[kSimCurr].Descriptor.ShaderRegister = 0;
        simParams[kSimStep].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        simParams[kSimStep].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        simParams[kSimStep].Constants.ShaderRegister = 1;
        simParams[kSimStep].Constants.Num32BitValues = 4;

        D3D12_ROOT_SIGNATURE_DESC simDesc{};
        simDesc.NumParameters = 4;
        simDesc.pParameters   = simParams;

        ComPtr<ID3DBlob> simBlob;
        ComPtr<ID3DBlob> simErr;
        if (FailedHr(D3D12SerializeRootSignature(&simDesc, D3D_ROOT_SIGNATURE_VERSION_1, &simBlob, &simErr), "Particle GPU sim RS serialize"))
        {
            if (simErr)
                DE_LOG_ERROR(LogCategory::Render, "Particle GPU sim root signature: {}", static_cast<const char*>(simErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, simBlob->GetBufferPointer(), simBlob->GetBufferSize(), IID_PPV_ARGS(&m_simRoot)), "Particle GPU sim CreateRootSignature"))
            return false;

        ComPtr<ID3DBlob> csCopy;
        ComPtr<ID3DBlob> csInPlace;
        if (!compileShaderFromContent("shaders/ParticleSimulate.hlsl", "CSSimulate", "cs_5_0", csCopy)
            || !compileShaderFromContent("shaders/ParticleSimulate.hlsl", "CSInPlace", "cs_5_0", csInPlace))
        {
            destroy();
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC simPso{};
        simPso.pRootSignature = m_simRoot.Get();
        simPso.CS             = { csCopy->GetBufferPointer(), csCopy->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&simPso, IID_PPV_ARGS(&m_simCopy)), "Particle GPU CSSimulate PSO"))
        {
            destroy();
            return false;
        }
        simPso.CS = { csInPlace->GetBufferPointer(), csInPlace->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&simPso, IID_PPV_ARGS(&m_simInPlace)), "Particle GPU CSInPlace PSO"))
        {
            destroy();
            return false;
        }

        D3D12_DESCRIPTOR_RANGE sprite{};
        sprite.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sprite.NumDescriptors                    = 1;
        sprite.BaseShaderRegister                = 0;
        sprite.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER drawParams[3]{};
        drawParams[kDrawCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        drawParams[kDrawCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        drawParams[kDrawCb].Descriptor.ShaderRegister = 0;
        drawParams[kDrawParticles].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        drawParams[kDrawParticles].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        drawParams[kDrawParticles].Descriptor.ShaderRegister = 0;
        drawParams[kDrawSprite].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        drawParams[kDrawSprite].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        drawParams[kDrawSprite].DescriptorTable.NumDescriptorRanges = 1;
        drawParams[kDrawSprite].DescriptorTable.pDescriptorRanges   = &sprite;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC drawDesc{};
        drawDesc.NumParameters     = 3;
        drawDesc.pParameters       = drawParams;
        drawDesc.NumStaticSamplers = 1;
        drawDesc.pStaticSamplers   = &samp;

        ComPtr<ID3DBlob> drawBlob;
        ComPtr<ID3DBlob> drawErr;
        if (FailedHr(D3D12SerializeRootSignature(&drawDesc, D3D_ROOT_SIGNATURE_VERSION_1, &drawBlob, &drawErr), "Particle GPU draw RS serialize"))
        {
            if (drawErr)
                DE_LOG_ERROR(LogCategory::Render, "Particle GPU draw root signature: {}", static_cast<const char*>(drawErr->GetBufferPointer()));
            destroy();
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, drawBlob->GetBufferPointer(), drawBlob->GetBufferSize(), IID_PPV_ARGS(&m_drawRoot)), "Particle GPU draw CreateRootSignature"))
        {
            destroy();
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        D3D_SHADER_MACRO encodeMacros[2];
        makeEncodeSrgbMacros(encodeSrgbForColorFormat(colorFormat), encodeMacros);
        if (!compileShaderFromContent("shaders/ParticleBillboard.hlsl", "VSMain", "vs_5_0", vs, encodeMacros)
            || !compileShaderFromContent("shaders/Particle.hlsl", "PSMain", "ps_5_0", ps, encodeMacros))
        {
            destroy();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_drawRoot.Get();
        pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask     = UINT_MAX;
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable   = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc     = sceneDepthFuncGreaterEqual();
        pso.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                = 1;
        pso.RTVFormats[0]                   = colorFormat;
        pso.DSVFormat                       = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc                      = { 1, 0 };

        fillBlend(pso.BlendState.RenderTarget[0], true);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_drawAdditive)), "Particle GPU additive PSO"))
        {
            destroy();
            return false;
        }
        fillBlend(pso.BlendState.RenderTarget[0], false);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_drawAlpha)), "Particle GPU alpha PSO"))
        {
            destroy();
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "ParticleGpuPipeline: ready");
        return true;
    }

    void ParticleGpuPipeline::destroy()
    {
        m_drawAdditive.Reset();
        m_drawAlpha.Reset();
        m_drawRoot.Reset();
        m_simCopy.Reset();
        m_simInPlace.Reset();
        m_simRoot.Reset();
    }

    void ParticleGpuPipeline::bindSimulate(ID3D12GraphicsCommandList* cmd, bool inPlace) const
    {
        if (!cmd || !m_simRoot)
            return;
        cmd->SetComputeRootSignature(m_simRoot.Get());
        cmd->SetPipelineState(inPlace ? m_simInPlace.Get() : m_simCopy.Get());
    }

    void ParticleGpuPipeline::bindDraw(ID3D12GraphicsCommandList* cmd, bool additive) const
    {
        if (!cmd || !m_drawRoot)
            return;
        cmd->SetGraphicsRootSignature(m_drawRoot.Get());
        cmd->SetPipelineState(additive ? m_drawAdditive.Get() : m_drawAlpha.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

} // namespace Dark
