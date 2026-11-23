#include <gtest/gtest.h>

#include "Render/DepthState.h"
#include "Terrain/GrassPipeline.h"
#include "Render/ShaderCompile.h"
#include "Render/RhiDevice.h"

#include <d3d11shader.h>
#include <d3d12.h>
#include <d3d12shader.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

using namespace Dark;

namespace
{

ComPtr<ID3D12Device> tryWarpDevice()
{
    return Dark::createWarpTestDevice();
}

bool cbufferBytes(ID3DBlob* blob, const char* name, UINT& outBytes)
{
    ComPtr<ID3D12ShaderReflection> reflect;
    if (!blob || !Dark::reflectShader(blob, reflect))
        return false;
    D3D12_SHADER_DESC desc{};
    if (FAILED(reflect->GetDesc(&desc)))
        return false;
    for (UINT i = 0; i < desc.ConstantBuffers; ++i)
    {
        ID3D12ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex(i);
        D3D12_SHADER_BUFFER_DESC              cbDesc{};
        if (!cb || FAILED(cb->GetDesc(&cbDesc)) || !cbDesc.Name)
            return false;
        if (std::strcmp(cbDesc.Name, name) == 0)
        {
            outBytes = cbDesc.Size;
            return true;
        }
    }
    return false;
}

} // namespace

TEST(GrassCull, ShadersCompile)
{
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", ShaderStage::Pixel, ps));

    const char* lods[] = { "0", "1", "2", "3" };
    for (const char* lod : lods)
    {
        D3D_SHADER_MACRO macros[2]{};
        macros[0].Name       = "GRASS_LOD";
        macros[0].Definition = lod;
        ComPtr<ID3DBlob> vs;
        ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", ShaderStage::Vertex, vs, macros));

        UINT drawBytes = 0;
        ASSERT_TRUE(cbufferBytes(vs.Get(), "GrassDrawConstants", drawBytes));
        EXPECT_EQ(drawBytes, GrassPipeline::kRootBaseDwords * static_cast<UINT>(sizeof(uint32_t)));

        UINT frameBytes = 0;
        ASSERT_TRUE(cbufferBytes(vs.Get(), "GrassFrameConstants", frameBytes));
        EXPECT_EQ(frameBytes, Terrain::kGrassFrameCbBytes);

        ComPtr<ID3D12ShaderReflection> reflect;
        ASSERT_TRUE(Dark::reflectShader(vs.Get(), reflect));
        D3D12_SHADER_INPUT_BIND_DESC bind{};
        EXPECT_TRUE(SUCCEEDED(reflect->GetResourceBindingDescByName("gIndices", &bind)));
        EXPECT_EQ(bind.BindPoint, 2u);
    }
}

TEST(GrassCull, GraphicsPsoAcceptsBaseConstant)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    D3D12_ROOT_PARAMETER params[5]{};
    params[GrassPipeline::kRootCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[GrassPipeline::kRootCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[GrassPipeline::kRootCb].Descriptor.ShaderRegister = 0;

    params[GrassPipeline::kRootBlades].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootBlades].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootBlades].Descriptor.ShaderRegister = 0;

    params[GrassPipeline::kRootWind].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootWind].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootWind].Descriptor.ShaderRegister = 1;

    params[GrassPipeline::kRootBase].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[GrassPipeline::kRootBase].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootBase].Constants.ShaderRegister = 1;
    params[GrassPipeline::kRootBase].Constants.Num32BitValues = GrassPipeline::kRootBaseDwords;

    params[GrassPipeline::kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootIndices].Descriptor.ShaderRegister = 2;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 5;
    rootDesc.pParameters   = params;
    rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    ASSERT_TRUE(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)));
    ComPtr<ID3D12RootSignature> root;
    ASSERT_TRUE(SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&root))));

    D3D_SHADER_MACRO macros[2]{};
    macros[0].Name       = "GRASS_LOD";
    macros[0].Definition = "0";
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", ShaderStage::Vertex, vs, macros));
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", ShaderStage::Pixel, ps));

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature                                   = root.Get();
    psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask                                       = UINT_MAX;
    psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
    psoDesc.RasterizerState.DepthClipEnable                  = TRUE;
    psoDesc.DepthStencilState.DepthEnable                    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc                      = sceneDepthFunc();
    psoDesc.InputLayout                                      = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets                                 = 4;
    psoDesc.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.RTVFormats[1]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[2]                                    = DXGI_FORMAT_R16G16_FLOAT;
    psoDesc.RTVFormats[3]                                    = DXGI_FORMAT_R8_UNORM;
    psoDesc.DSVFormat                                        = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc                                       = { 1, 0 };

    ComPtr<ID3D12PipelineState> pso;
    const HRESULT               hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    EXPECT_EQ(hr, S_OK);
}
