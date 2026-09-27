#include <gtest/gtest.h>

#include "Render/CamouflagePipeline.h"
#include "Render/ShaderCompile.h"

#include <d3d12.h>
#include <wrl/client.h>

using Dark::compileShaderFromContent;
using Dark::CamouflageConstants;
using Microsoft::WRL::ComPtr;

TEST(Camouflage, ConstantsSize)
{
    EXPECT_EQ(sizeof(CamouflageConstants), 40u * sizeof(float));
}

TEST(Camouflage, ShaderCompiles)
{
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> vsStatic;
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/Camouflage.hlsl", "VSMain", "vs_5_0", vs));
    ASSERT_TRUE(compileShaderFromContent("shaders/Camouflage.hlsl", "VSMainStatic", "vs_5_0", vsStatic));
    ASSERT_TRUE(compileShaderFromContent("shaders/Camouflage.hlsl", "PSMain", "ps_5_0", ps));
    EXPECT_NE(vs.Get(), nullptr);
    EXPECT_NE(vsStatic.Get(), nullptr);
    EXPECT_NE(ps.Get(), nullptr);
}
